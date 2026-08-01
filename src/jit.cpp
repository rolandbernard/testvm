#include "jit.hpp"
#include "vm.hpp"
#include "interpreter.hpp"
#include "bytecode.hpp"
#include "JitBuilder.hpp"
#include "MethodBuilder.hpp"
#include "TypeDictionary.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

static bool jit_initialized = false;

extern "C" {

Value jit_helper_binary_op(void* vm_ptr, Method* method, uint32_t ic_idx, uint32_t op_code, Value left, Value right) {
    VM* vm = static_cast<VM*>(vm_ptr);
    Opcode op = static_cast<Opcode>(op_code);

    // Primitive SmallInt path
    if (is_int(left) && is_int(right)) {
        int64_t l = decode_int(left);
        int64_t r = decode_int(right);
        int64_t res = 0;

        switch (op) {
            case OP_ADD: res = l + r; break;
            case OP_SUB: res = l - r; break;
            case OP_MUL: res = l * r; break;
            case OP_DIV:
                if (r == 0) throw std::runtime_error("Division by zero in JIT execution");
                res = l / r;
                break;
            case OP_REM:
                if (r == 0) throw std::runtime_error("Division by zero in JIT execution");
                res = l % r;
                break;
            case OP_LT: res = (l < r) ? 1 : 0; break;
            case OP_LE: res = (l <= r) ? 1 : 0; break;
            case OP_GT: res = (l > r) ? 1 : 0; break;
            case OP_GE: res = (l >= r) ? 1 : 0; break;
            case OP_EQ: res = (l == r) ? 1 : 0; break;
            case OP_NE: res = (l != r) ? 1 : 0; break;
            default: break;
        }
        return encode_int(res);
    }

    // Object Operator Overloading via Inline Cache
    if (is_obj(left)) {
        ToyObject* obj = decode_obj(left);
        Class* klass = obj->klass;
        InlineCache& ic = method->inline_caches[ic_idx];

        Method* targetMethod = nullptr;
        if (ic.cached_class == klass) {
            ic.hit_count++;
            targetMethod = ic.cached_method;
        } else {
            ic.miss_count++;
            targetMethod = klass->lookup_method(ic.method_name);
            if (!targetMethod) {
                throw std::runtime_error("Class '" + klass->name + "' does not implement operator method '" + ic.method_name + "'");
            }
            if (!ic.is_megamorphic) {
                if (ic.cached_class == nullptr) {
                    ic.cached_class = klass;
                    ic.cached_method = targetMethod;
                } else {
                    ic.is_megamorphic = true;
                    ic.cached_class = nullptr;
                    ic.cached_method = nullptr;
                }
            }
        }

        std::vector<Value> callArgs = { right };
        return vm->call_method(targetMethod, left, callArgs);
    }

    throw std::runtime_error("Invalid binary operation operands in JIT execution");
}

// General method/global call helpers. The JIT compiles the on-stack arguments
// into a small stack-allocated array and hands it to one of these, so any arity
// is supported without emitting a dedicated helper per argument count.
Value jit_helper_call_method(void* vm_ptr, Method* method, uint32_t ic_idx, Value receiver, uint32_t argCount, const Value* args) {
    VM* vm = static_cast<VM*>(vm_ptr);
    if (!is_obj(receiver)) {
        throw std::runtime_error("Attempted method call on non-object receiver in JIT execution");
    }
    ToyObject* obj = decode_obj(receiver);
    Class* klass = obj->klass;
    InlineCache& ic = method->inline_caches[ic_idx];

    Method* targetMethod = nullptr;
    if (ic.cached_class == klass) {
        ic.hit_count++;
        targetMethod = ic.cached_method;
    } else {
        ic.miss_count++;
        targetMethod = klass->lookup_method(ic.method_name);
        if (!targetMethod) {
            throw std::runtime_error("Method '" + ic.method_name + "' not found in class '" + klass->name + "'");
        }
        if (!ic.is_megamorphic) {
            if (ic.cached_class == nullptr) {
                ic.cached_class = klass;
                ic.cached_method = targetMethod;
            } else {
                ic.is_megamorphic = true;
                ic.cached_class = nullptr;
                ic.cached_method = nullptr;
            }
        }
    }
    return vm->call_method(targetMethod, receiver, std::vector<Value>(args, args + argCount));
}

Value jit_helper_call_global(void* vm_ptr, const char* func_name, uint32_t argCount, const Value* args) {
    VM* vm = static_cast<VM*>(vm_ptr);
    Method* globalFunc = vm->getFunction(func_name);
    if (!globalFunc) throw std::runtime_error("Global function not found in JIT execution: " + std::string(func_name));
    return vm->call_global(globalFunc, std::vector<Value>(args, args + argCount));
}

Value jit_helper_allocate_object(void* vm_ptr, const char* class_name) {
    VM* vm = static_cast<VM*>(vm_ptr);
    Class* klass = vm->getClass(class_name);
    if (!klass) throw std::runtime_error("Unknown class in JIT allocation: " + std::string(class_name));
    ToyObject* obj = vm->allocate_object(klass);
    return encode_obj(obj);
}

} // extern "C"

class ToyJitMethodBuilder : public OMR::JitBuilder::MethodBuilder {
public:
    ToyJitMethodBuilder(OMR::JitBuilder::TypeDictionary *types, VM* vm, Method* method)
        : OMR::JitBuilder::MethodBuilder(types), vm(vm), method(method) {
        DefineLine(LINETOSTR(__LINE__));
        DefineFile(__FILE__);

        pInt64 = types->PointerTo(Int64);

        DefineName(method->name.c_str());
        DefineParameter("vm_ptr", pInt64);
        DefineParameter("receiver", Int64);
        DefineParameter("args_ptr", pInt64);
        DefineReturnType(Int64);

        DefineFunction("jit_helper_binary_op", (char*)__FILE__, (char*)LINETOSTR(__LINE__), (void*)&jit_helper_binary_op, Int64, 6, pInt64, pInt64, Int32, Int32, Int64, Int64);
        DefineFunction("jit_helper_call_method", (char*)__FILE__, (char*)LINETOSTR(__LINE__), (void*)&jit_helper_call_method, Int64, 6, pInt64, pInt64, Int32, Int64, Int32, Address);
        DefineFunction("jit_helper_call_global", (char*)__FILE__, (char*)LINETOSTR(__LINE__), (void*)&jit_helper_call_global, Int64, 4, pInt64, pInt64, Int32, Address);
        DefineFunction("jit_helper_allocate_object", (char*)__FILE__, (char*)LINETOSTR(__LINE__), (void*)&jit_helper_allocate_object, Int64, 2, pInt64, pInt64);
    }

    virtual bool buildIL() override;

private:
    VM* vm;
    Method* method;
    OMR::JitBuilder::IlType* pInt64 = nullptr;

    static uint32_t read_u32(const uint8_t*& ip) {
        uint32_t val;
        std::memcpy(&val, ip, sizeof(uint32_t));
        ip += sizeof(uint32_t);
        return val;
    }

    static int32_t read_i32(const uint8_t*& ip) {
        int32_t val;
        std::memcpy(&val, ip, sizeof(int32_t));
        ip += sizeof(int32_t);
        return val;
    }
};

bool ToyJitMethodBuilder::buildIL() {
    // Define local variables for VM method locals
    for (uint32_t i = 0; i < method->num_locals; ++i) {
        std::string locName = "loc_" + std::to_string(i);
        DefineLocal(locName.c_str(), Int64);
        Store(locName.c_str(), ConstInt64(make_null()));
    }

    // Unpack arguments and receiver into local variables
    size_t localOffset = 0;
    if (!method->params.empty() && method->params[0] == "this") {
        Store("loc_0", Load("receiver"));
        localOffset = 1;
    }

    size_t numArgs = method->params.size() - localOffset;
    for (size_t i = 0; i < numArgs; ++i) {
        std::string locName = "loc_" + std::to_string(i + localOffset);
        OMR::JitBuilder::IlValue* argElem = IndexAt(pInt64, Load("args_ptr"), ConstInt32(static_cast<int32_t>(i)));
        Store(locName.c_str(), LoadAt(Int64, argElem));
    }

    // Translate Bytecode instructions
    std::vector<OMR::JitBuilder::IlValue*> evalStack;
    const uint8_t* ip = method->bytecode.data();
    const uint8_t* ipEnd = ip + method->bytecode.size();

    while (ip < ipEnd) {
        Opcode op = static_cast<Opcode>(*ip++);

        switch (op) {
            case OP_PUSH_CONST: {
                uint32_t cidx = read_u32(ip);
                evalStack.push_back(ConstInt64(method->constants[cidx]));
                break;
            }
            case OP_PUSH_NULL: {
                evalStack.push_back(ConstInt64(make_null()));
                break;
            }
            case OP_LOAD_LOCAL: {
                uint32_t lidx = read_u32(ip);
                std::string locName = "loc_" + std::to_string(lidx);
                evalStack.push_back(Load(locName.c_str()));
                break;
            }
            case OP_STORE_LOCAL: {
                uint32_t lidx = read_u32(ip);
                std::string locName = "loc_" + std::to_string(lidx);
                OMR::JitBuilder::IlValue* val = evalStack.back(); evalStack.pop_back();
                Store(locName.c_str(), val);
                break;
            }
            case OP_LOAD_FIELD: {
                uint32_t fidx = read_u32(ip);
                OMR::JitBuilder::IlValue* thisVal = Load("loc_0");
                size_t offset = sizeof(ObjectHeader) + sizeof(Class*) + fidx * sizeof(Value);
                OMR::JitBuilder::IlValue* fieldAddr = Add(thisVal, ConstInt64(offset));
                evalStack.push_back(LoadAt(Int64, fieldAddr));
                break;
            }
            case OP_STORE_FIELD: {
                uint32_t fidx = read_u32(ip);
                OMR::JitBuilder::IlValue* val = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* thisVal = Load("loc_0");
                size_t offset = sizeof(ObjectHeader) + sizeof(Class*) + fidx * sizeof(Value);
                OMR::JitBuilder::IlValue* fieldAddr = Add(thisVal, ConstInt64(offset));
                StoreAt(fieldAddr, val);
                break;
            }
            case OP_NEW_OBJECT: {
                uint32_t cidx = read_u32(ip);
                const char* className = reinterpret_cast<const char*>(method->constants[cidx]);
                OMR::JitBuilder::IlValue* obj = Call("jit_helper_allocate_object", 2, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(className)));
                evalStack.push_back(obj);
                break;
            }
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_REM:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE:
            case OP_EQ:
            case OP_NE: {
                uint32_t ic_idx = read_u32(ip);
                OMR::JitBuilder::IlValue* right = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* left = evalStack.back(); evalStack.pop_back();

                OMR::JitBuilder::IlValue* res = Call("jit_helper_binary_op", 6,
                    Load("vm_ptr"),
                    ConstInt64(reinterpret_cast<int64_t>(method)),
                    ConstInt32(ic_idx),
                    ConstInt32(static_cast<int32_t>(op)),
                    left,
                    right);
                evalStack.push_back(res);
                break;
            }
            case OP_REF_EQ: {
                OMR::JitBuilder::IlValue* right = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* left = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* eq = EqualTo(left, right);
                OMR::JitBuilder::IlValue* taggedEq = Or(ShiftL(eq, ConstInt64(1)), ConstInt64(1));
                evalStack.push_back(taggedEq);
                break;
            }
            case OP_REF_NE: {
                OMR::JitBuilder::IlValue* right = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* left = evalStack.back(); evalStack.pop_back();
                OMR::JitBuilder::IlValue* ne = NotEqualTo(left, right);
                OMR::JitBuilder::IlValue* taggedNe = Or(ShiftL(ne, ConstInt64(1)), ConstInt64(1));
                evalStack.push_back(taggedNe);
                break;
            }
            case OP_JUMP: {
                int32_t offset = read_i32(ip);
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                int32_t offset = read_i32(ip);
                OMR::JitBuilder::IlValue* cond = evalStack.back(); evalStack.pop_back();
                ip += offset;
                break;
            }
            case OP_CALL_GLOBAL: {
                uint32_t cidx = read_u32(ip);
                uint32_t argCount = read_u32(ip);
                const char* funcName = reinterpret_cast<const char*>(method->constants[cidx]);

                std::vector<OMR::JitBuilder::IlValue*> args(argCount);
                for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                    args[i] = evalStack.back();
                    evalStack.pop_back();
                }

                OMR::JitBuilder::IlValue* retVal = nullptr;
                if (argCount == 0) {
                    retVal = Call("jit_helper_call_global_0", 2, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(funcName)));
                } else if (argCount == 1) {
                    retVal = Call("jit_helper_call_global_1", 3, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(funcName)), args[0]);
                } else if (argCount == 2) {
                    retVal = Call("jit_helper_call_global_2", 4, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(funcName)), args[0], args[1]);
                } else {
                    retVal = Call("jit_helper_call_global_0", 2, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(funcName)));
                }
                evalStack.push_back(retVal);
                break;
            }
            case OP_CALL_METHOD: {
                uint32_t ic_idx = read_u32(ip);
                uint32_t argCount = read_u32(ip);

                std::vector<OMR::JitBuilder::IlValue*> args(argCount);
                for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                    args[i] = evalStack.back();
                    evalStack.pop_back();
                }

                OMR::JitBuilder::IlValue* receiver = evalStack.back(); evalStack.pop_back();

                OMR::JitBuilder::IlValue* retVal = nullptr;
                if (argCount == 0) {
                    retVal = Call("jit_helper_call_method_0", 4, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(method)), ConstInt32(ic_idx), receiver);
                } else if (argCount == 1) {
                    retVal = Call("jit_helper_call_method_1", 5, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(method)), ConstInt32(ic_idx), receiver, args[0]);
                } else if (argCount == 2) {
                    retVal = Call("jit_helper_call_method_2", 6, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(method)), ConstInt32(ic_idx), receiver, args[0], args[1]);
                } else {
                    retVal = Call("jit_helper_call_method_0", 4, Load("vm_ptr"), ConstInt64(reinterpret_cast<int64_t>(method)), ConstInt32(ic_idx), receiver);
                }
                evalStack.push_back(retVal);
                break;
            }
            case OP_RETURN: {
                OMR::JitBuilder::IlValue* retVal = ConstInt64(make_null());
                if (!evalStack.empty()) {
                    retVal = evalStack.back();
                }
                Return(retVal);
                return true;
            }
            case OP_POP: {
                if (!evalStack.empty()) evalStack.pop_back();
                break;
            }
            case OP_DUP: {
                if (!evalStack.empty()) evalStack.push_back(evalStack.back());
                break;
            }
            default:
                break;
        }
    }

    Return(ConstInt64(make_null()));
    return true;
}

bool init_jit_compiler() {
    if (!jit_initialized) {
        jit_initialized = initializeJit();
    }
    return jit_initialized;
}

void shutdown_jit_compiler() {
    if (jit_initialized) {
        shutdownJit();
        jit_initialized = false;
    }
}

bool compile_method_with_jit(VM* vm, Method* method) {
    if (!jit_initialized) return false;

    OMR::JitBuilder::TypeDictionary types;
    ToyJitMethodBuilder builder(&types, vm, method);

    void* entry = nullptr;
    int32_t rc = compileMethodBuilder(&builder, &entry);
    if (rc == 0 && entry != nullptr) {
        method->jit_entry = reinterpret_cast<JitFunctionPtr>(entry);
        return true;
    }

    return false;
}
