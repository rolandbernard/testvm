#include "vm.hpp"
#include "compiler.hpp"
#include "interpreter.hpp"
#include "jit.hpp"
#include "EnvironmentBase.hpp"
#include "ObjectAllocationModel.hpp"
#include "omrExampleVM.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

VM::VM() {
    exampleVM._omrVM = nullptr;
    exampleVM._omrVMThread = nullptr;
    exampleVM.rootTable = nullptr;
    exampleVM.objectTable = nullptr;
    exampleVM._vmAccessMutex = nullptr;
    exampleVM._vmExclusiveAccessCount = 0;
}

VM::~VM() {
    shutdown();
}

bool VM::init() {
    omr_error_t rc = OMR_Initialize_VM(&exampleVM._omrVM, &omrVMThread, &exampleVM, NULL);
    if (rc != OMR_ERROR_NONE) {
        std::cerr << "Failed to initialize OMR VM: " << rc << std::endl;
        return false;
    }
    exampleVM._omrVMThread = omrVMThread;

    intptr_t rw_rc = omrthread_rwmutex_init(&exampleVM._vmAccessMutex, 0, "VM exclusive access");
    if (rw_rc != J9THREAD_RWMUTEX_OK) {
        std::cerr << "Failed to initialize VM mutex" << std::endl;
        return false;
    }

    exampleVM.rootTable = hashTableNew(
        exampleVM._omrVM->_runtime->_portLibrary, OMR_GET_CALLSITE(), 0, sizeof(RootEntry), 0, 0, OMRMEM_CATEGORY_MM,
        rootTableHashFn, rootTableHashEqualFn, NULL, NULL);

    exampleVM.objectTable = hashTableNew(
        exampleVM._omrVM->_runtime->_portLibrary, OMR_GET_CALLSITE(), 0, sizeof(ObjectEntry), 0, 0, OMRMEM_CATEGORY_MM,
        objectTableHashFn, objectTableHashEqualFn, NULL, NULL);

    // Initialize JitBuilder JIT compiler subsystem
    if (!init_jit_compiler()) {
        std::cerr << "Warning: Could not initialize JitBuilder JIT compiler" << std::endl;
    }

    return true;
}

void VM::shutdown() {
    if (exampleVM._omrVM != nullptr) {
        shutdown_jit_compiler();

        if (exampleVM.objectTable != nullptr) {
            hashTableFree(exampleVM.objectTable);
            exampleVM.objectTable = nullptr;
        }

        if (exampleVM.rootTable != nullptr) {
            hashTableFree(exampleVM.rootTable);
            exampleVM.rootTable = nullptr;
        }

        if (exampleVM._vmAccessMutex != nullptr) {
            omrthread_rwmutex_destroy(exampleVM._vmAccessMutex);
            exampleVM._vmAccessMutex = nullptr;
        }

        OMR_Shutdown_VM(exampleVM._omrVM, omrVMThread);
        exampleVM._omrVM = nullptr;
        omrVMThread = nullptr;
    }
}

void VM::update_stack_roots() {
    if (!exampleVM.rootTable) return;

    // Clear existing roots in root table
    J9HashTableState state;
    RootEntry *rootEntry = (RootEntry *)hashTableStartDo(exampleVM.rootTable, &state);
    while (rootEntry != nullptr) {
        hashTableDoRemove(&state);
        rootEntry = (RootEntry *)hashTableNextDo(&state);
    }

    // A single contiguous language stack is the source of truth for roots.
    // Do not reuse one formatting buffer: RootEntry retains its name pointer.
    stack_root_names.clear();
    stack_root_names.reserve(stack.size() + temp_roots.size());
    for (size_t i = 0; i < stack.size(); ++i) {
        if (is_obj(stack[i])) {
            stack_root_names.push_back("stack_" + std::to_string(i));
            RootEntry rEntry = { stack_root_names.back().c_str(), (omrobjectptr_t)decode_obj(stack[i]) };
            hashTableAdd(exampleVM.rootTable, &rEntry);
        }
    }

    // Add temporary roots
    for (size_t t = 0; t < temp_roots.size(); ++t) {
        if (is_obj(temp_roots[t])) {
            stack_root_names.push_back("temp_" + std::to_string(t));
            RootEntry rEntry = { stack_root_names.back().c_str(), (omrobjectptr_t)decode_obj(temp_roots[t]) };
            hashTableAdd(exampleVM.rootTable, &rEntry);
        }
    }
}

void VM::synchronize_moved_roots() {
    if (!exampleVM.rootTable) return;

    // OMR's stock scanner forwards the RootEntry copy, not our Value slot.
    // The names created above give us a compact, precise bridge back to the
    // flat VM stack after a collection or evacuation.
    J9HashTableState state;
    RootEntry *entry = (RootEntry *)hashTableStartDo(exampleVM.rootTable, &state);
    while (entry != nullptr) {
        if (entry->rootPtr != nullptr) {
            const char* name = entry->name;
            if (std::strncmp(name, "stack_", 6) == 0) {
                const size_t index = std::strtoull(name + 6, nullptr, 10);
                if (index < stack.size()) stack[index] = encode_obj((ToyObject*)entry->rootPtr);
            } else if (std::strncmp(name, "temp_", 5) == 0) {
                const size_t index = std::strtoull(name + 5, nullptr, 10);
                if (index < temp_roots.size()) temp_roots[index] = encode_obj((ToyObject*)entry->rootPtr);
            }
        }
        entry = (RootEntry *)hashTableNextDo(&state);
    }
}

ToyObject* VM::allocate_object(Class* klass) {
    if (!omrVMThread) {
        throw std::runtime_error("VM thread not initialized");
    }

    update_stack_roots();

    MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(omrVMThread);
    size_t numFields = klass->field_names.size();
    size_t allocSize = ToyObject::allocSize(numFields);

    MM_ObjectAllocationModel allocationModel(env, allocSize, 0);
    ToyObject* obj = (ToyObject*)OMR_GC_AllocateObject(omrVMThread, &allocationModel);
    // Allocation is a safepoint; the collector may have forwarded any object
    // reachable from a language frame.
    synchronize_moved_roots();
    if (!obj) {
        throw std::runtime_error("Out of Memory: OMR GC object allocation failed for class " + klass->name);
    }

    obj->klass = klass;
    for (size_t i = 0; i < numFields; ++i) {
        obj->fields[i] = make_null();
    }

    return obj;
}

void VM::register_root(const char* name, ToyObject* obj) {
    if (exampleVM.rootTable && obj) {
        RootEntry rEntry = { name, (omrobjectptr_t)obj };
        hashTableAdd(exampleVM.rootTable, &rEntry);
    }
}

void VM::unregister_root(const char* name) {
    // Left as no-op or handled during update_stack_roots
}

Class* VM::getClass(const std::string& name) const {
    auto it = class_map.find(name);
    if (it != class_map.end()) return it->second;
    return nullptr;
}

Method* VM::getFunction(const std::string& name) const {
    auto it = func_map.find(name);
    if (it != func_map.end()) return it->second;
    return nullptr;
}

void VM::maybe_jit_compile(Method* method) {
    if (!method->jit_entry && !method->is_compiling && method->invocation_count >= JIT_THRESHOLD) {
        method->is_compiling = true;
        compile_method_with_jit(this, method);
        method->is_compiling = false;
    }
}

Value VM::call_method(Method* method, Value receiver, const std::vector<Value>& args) {
    method->invocation_count++;
    maybe_jit_compile(method);

    if (method->jit_entry) {
        // Fast Path: Execute JIT compiled native code!
        return method->jit_entry(this, receiver, args.data());
    }

    // Bytecode Interpreter fallback
    Interpreter interp(this);
    return interp.execute(method, receiver, args);
}

Value VM::call_global(Method* method, const std::vector<Value>& args) {
    return call_method(method, make_null(), args);
}

Value VM::execute_program(const ProgramAST* ast) {
    Compiler compiler;
    compiler.compileProgram(ast, classes, functions, class_map, func_map);

    Method* mainFunc = getFunction("main");
    if (!mainFunc) {
        throw std::runtime_error("No 'main' function found in program");
    }

    std::vector<Value> args;
    return call_global(mainFunc, args);
}
