#include "vm.hpp"
#include "compiler.hpp"
#include "interpreter.hpp"
#include "jit.hpp"
#include "EnvironmentBase.hpp"
#include "ObjectAllocationModel.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

VM::VM() {
    glueVM._omrVM = nullptr;
    glueVM._omrVMThread = nullptr;
    glueVM.rootTable = nullptr;
    glueVM.objectTable = nullptr;
    glueVM.valueStack = nullptr;
    glueVM.valueStackSize = 0;
    glueVM._vmAccessMutex = nullptr;
    glueVM._vmExclusiveAccessCount = 0;
}

VM::~VM() {
    shutdown();
}

bool VM::init() {
    omr_error_t rc = OMR_Initialize_VM(&glueVM._omrVM, &omrVMThread, &glueVM, NULL);
    if (rc != OMR_ERROR_NONE) {
        std::cerr << "Failed to initialize OMR VM: " << rc << std::endl;
        return false;
    }
    glueVM._omrVMThread = omrVMThread;

    intptr_t rw_rc = omrthread_rwmutex_init(&glueVM._vmAccessMutex, 0, "VM exclusive access");
    if (rw_rc != J9THREAD_RWMUTEX_OK) {
        std::cerr << "Failed to initialize VM mutex" << std::endl;
        return false;
    }

    glueVM.objectTable = hashTableNew(
        glueVM._omrVM->_runtime->_portLibrary, OMR_GET_CALLSITE(), 0, sizeof(ToyObjectEntry), 0, 0, OMRMEM_CATEGORY_MM,
        toyObjectTableHashFn, toyObjectTableHashEqualFn, NULL, NULL);

    // Initialize JitBuilder JIT compiler subsystem
    if (!init_jit_compiler()) {
        std::cerr << "Warning: Could not initialize JitBuilder JIT compiler" << std::endl;
    }

    return true;
}

void VM::shutdown() {
    if (glueVM._omrVM != nullptr) {
        shutdown_jit_compiler();

        if (glueVM.objectTable != nullptr) {
            hashTableFree(glueVM.objectTable);
            glueVM.objectTable = nullptr;
        }

        if (glueVM._vmAccessMutex != nullptr) {
            omrthread_rwmutex_destroy(glueVM._vmAccessMutex);
            glueVM._vmAccessMutex = nullptr;
        }

        OMR_Shutdown_VM(glueVM._omrVM, omrVMThread);
        glueVM._omrVM = nullptr;
        omrVMThread = nullptr;
    }
}

void VM::publish_stack_for_gc() {
    glueVM.valueStack = reinterpret_cast<volatile uintptr_t*>(stack.data());
    glueVM.valueStackSize = stack.size();
}

ToyObject* VM::allocate_object(Class* klass) {
    if (!omrVMThread) {
        throw std::runtime_error("VM thread not initialized");
    }

    publish_stack_for_gc();

    MM_EnvironmentBase *env = MM_EnvironmentBase::getEnvironment(omrVMThread);
    size_t numFields = klass->field_names.size();
    size_t allocSize = ToyObject::allocSize(numFields);

    MM_ObjectAllocationModel allocationModel(env, allocSize, 0);
    ToyObject* obj = (ToyObject*)OMR_GC_AllocateObject(omrVMThread, &allocationModel);
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
    (void)name;
    (void)obj;
    // The VM has no out-of-stack object handles.  Every live language value
    // is on the flat stack and is scanned directly by the custom glue.
}

void VM::unregister_root(const char* name) {
    (void)name;
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
