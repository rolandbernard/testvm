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
#include <set>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <deque>

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
        // All compiled toy methods use this ABI.  ComputedCall below makes a
        // direct compiled-to-compiled call when the target is already JITed;
        // the ordinary helper remains the c2i adapter.
        DefineFunction("toyvm_jit_entry", (char*)__FILE__, (char*)LINETOSTR(__LINE__),
                       (void*)&jit_helper_call_global, Int64, 3, pInt64, Int64, Address);
    }

    virtual bool buildIL() override;

private:
    VM* vm;
    Method* method;
    OMR::JitBuilder::IlType* pInt64 = nullptr;

    // OMR stores the raw name pointer as the symbol key, so dynamically built
    // names must stay alive for the lifetime of the builder. A deque keeps the
    // strings stable (std::string uses SSO, so vector reallocation would move
    // short strings and dangle the returned pointers).
    std::deque<std::string> _nameStorage;
    const char* keep(const std::string& name) {
        _nameStorage.push_back(name);
        return _nameStorage.back().c_str();
    }

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
        const char* locName = keep("loc_" + std::to_string(i));
        DefineLocal(locName, Int64);
        Store(locName, ConstInt64(make_null()));
    }

    // Unpack arguments and receiver into local variables
    size_t localOffset = 0;
    if (!method->params.empty() && method->params[0] == "this") {
        Store("loc_0", Load("receiver"));
        localOffset = 1;
    }

    size_t numArgs = method->params.size() - localOffset;
    for (size_t i = 0; i < numArgs; ++i) {
        const char* locName = keep("loc_" + std::to_string(i + localOffset));
        OMR::JitBuilder::IlValue* argElem = IndexAt(pInt64, Load("args_ptr"), ConstInt32(static_cast<int32_t>(i)));
        Store(locName, LoadAt(pInt64, argElem));
    }

    // ---- Pass 1: discover jump targets and split the bytecode into blocks.
    const uint8_t* bytecode = method->bytecode.data();
    const uint8_t* codeEnd = bytecode + method->bytecode.size();

    std::set<uint32_t> jumpTargets;
    {
        const uint8_t* ip = bytecode;
        while (ip < codeEnd) {
            Opcode op = static_cast<Opcode>(*ip++);
            if (op == OP_JUMP || op == OP_JUMP_IF_FALSE) {
                int32_t offset = read_i32(ip);
                jumpTargets.insert(static_cast<uint32_t>(ip - bytecode) + static_cast<uint32_t>(offset));
            }
        }
    }

    std::vector<uint32_t> blockStarts;
    blockStarts.push_back(0);
    {
        const uint8_t* ip = bytecode;
        while (ip < codeEnd) {
            uint32_t pos = static_cast<uint32_t>(ip - bytecode);
            Opcode op = static_cast<Opcode>(*ip++);
            if (jumpTargets.count(pos) != 0) {
                blockStarts.push_back(pos);
            }
            switch (op) {
                case OP_JUMP:
                case OP_JUMP_IF_FALSE: {
                    (void)read_i32(ip);
                    if (ip < codeEnd) blockStarts.push_back(static_cast<uint32_t>(ip - bytecode));
                    break;
                }
                case OP_RETURN:
                    if (ip < codeEnd) blockStarts.push_back(static_cast<uint32_t>(ip - bytecode));
                    break;
                default:
                    break;
            }
        }
    }
    std::sort(blockStarts.begin(), blockStarts.end());
    blockStarts.erase(std::unique(blockStarts.begin(), blockStarts.end()), blockStarts.end());

    // ---- Pass 2: create one builder per block and wire the CFG in order.
    std::unordered_map<uint32_t, OMR::JitBuilder::IlBuilder*> blockBuilders;
    for (uint32_t start : blockStarts) {
        blockBuilders[start] = OrphanBuilder();
    }

    // All return sites jump here; the method returns the stored value.
    DefineLocal("_retval", Int64);
    Store("_retval", ConstInt64(make_null()));
    // Materialise arithmetic results through one virtual register.  This lets
    // a guard select a fully inlined SmallInt operation or the generic
    // (deoptimising) path without a C++ call on the monomorphic primitive path.
    DefineLocal("_binary_result", Int64);
    Store("_binary_result", ConstInt64(make_null()));
    OMR::JitBuilder::IlBuilder* returnLabel = OrphanBuilder();

    OMR::JitBuilder::IlBuilder* current = this;
    for (size_t bi = 0; bi < blockStarts.size(); ++bi) {
        uint32_t start = blockStarts[bi];
        uint32_t end = (bi + 1 < blockStarts.size()) ? blockStarts[bi + 1]
                                                     : static_cast<uint32_t>(method->bytecode.size());

        OMR::JitBuilder::IlBuilder* cur = blockBuilders[start];
        current->AppendBuilder(cur);
        current = cur;

        std::vector<OMR::JitBuilder::IlValue*> stack;
        const uint8_t* ip = bytecode + start;
        const uint8_t* blockEnd = bytecode + end;
        bool terminated = false;

        while (ip < blockEnd && !terminated) {
            Opcode op = static_cast<Opcode>(*ip++);

            switch (op) {
                case OP_PUSH_CONST: {
                    uint32_t cidx = read_u32(ip);
                    stack.push_back(cur->ConstInt64(method->constants[cidx]));
                    break;
                }
                case OP_PUSH_NULL: {
                    stack.push_back(cur->ConstInt64(make_null()));
                    break;
                }
                case OP_LOAD_LOCAL: {
                    uint32_t lidx = read_u32(ip);
                    stack.push_back(cur->Load(keep("loc_" + std::to_string(lidx))));
                    break;
                }
                case OP_STORE_LOCAL: {
                    uint32_t lidx = read_u32(ip);
                    OMR::JitBuilder::IlValue* val = stack.back(); stack.pop_back();
                    cur->Store(keep("loc_" + std::to_string(lidx)), val);
                    break;
                }
                case OP_LOAD_FIELD: {
                    uint32_t fidx = read_u32(ip);
                    OMR::JitBuilder::IlValue* thisAddr = cur->ConvertTo(Address, cur->Load("loc_0"));
                    size_t offset = sizeof(ObjectHeader) + sizeof(Class*) + fidx * sizeof(Value);
                    OMR::JitBuilder::IlValue* fieldAddr = cur->Add(thisAddr, cur->ConstInt64(offset));
                    stack.push_back(cur->LoadAt(pInt64, fieldAddr));
                    break;
                }
                case OP_STORE_FIELD: {
                    uint32_t fidx = read_u32(ip);
                    OMR::JitBuilder::IlValue* val = stack.back(); stack.pop_back();
                    OMR::JitBuilder::IlValue* thisAddr = cur->ConvertTo(Address, cur->Load("loc_0"));
                    size_t offset = sizeof(ObjectHeader) + sizeof(Class*) + fidx * sizeof(Value);
                    OMR::JitBuilder::IlValue* fieldAddr = cur->Add(thisAddr, cur->ConstInt64(offset));
                    cur->StoreAt(fieldAddr, val);
                    break;
                }
                case OP_NEW_OBJECT: {
                    uint32_t cidx = read_u32(ip);
                    const char* className = reinterpret_cast<const char*>(method->constants[cidx]);
                    OMR::JitBuilder::IlValue* obj = cur->Call("jit_helper_allocate_object", 2,
                        cur->Load("vm_ptr"), cur->ConstInt64(reinterpret_cast<int64_t>(className)));
                    stack.push_back(obj);
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
                    OMR::JitBuilder::IlValue* right = stack.back(); stack.pop_back();
                    OMR::JitBuilder::IlValue* left = stack.back(); stack.pop_back();

                    // The interpreter profiles operators as tagged SmallInts
                    // overwhelmingly often. Keep that case entirely in IL;
                    // objects and unexpected values take the generic path,
                    // which is our deoptimisation boundary.
                    OMR::JitBuilder::IlValue* bothInts = cur->EqualTo(
                        cur->And(left, right), cur->ConstInt64(1));
                    if (op == OP_DIV || op == OP_REM) {
                        OMR::JitBuilder::IlValue* nonZero = cur->NotEqualTo(right, cur->ConstInt64(1));
                        bothInts = cur->And(bothInts, nonZero);
                    }
                    OMR::JitBuilder::IlBuilder* primitive = nullptr;
                    OMR::JitBuilder::IlBuilder* generic = nullptr;
                    cur->IfThenElse(&primitive, &generic, bothInts);

                    OMR::JitBuilder::IlValue* primitiveResult = nullptr;
                    switch (op) {
                        case OP_ADD:
                            primitiveResult = primitive->Sub(primitive->Add(left, right), primitive->ConstInt64(1));
                            break;
                        case OP_SUB:
                            primitiveResult = primitive->Add(primitive->Sub(left, right), primitive->ConstInt64(1));
                            break;
                        case OP_MUL: {
                            auto* product = primitive->Mul(primitive->ShiftR(left, primitive->ConstInt32(1)), primitive->ShiftR(right, primitive->ConstInt32(1)));
                            primitiveResult = primitive->Or(primitive->ShiftL(product, primitive->ConstInt32(1)), primitive->ConstInt64(1));
                            break;
                        }
                        case OP_DIV:
                        case OP_REM: {
                            auto* l = primitive->ShiftR(left, primitive->ConstInt32(1));
                            auto* r = primitive->ShiftR(right, primitive->ConstInt32(1));
                            auto* quotient = op == OP_DIV ? primitive->Div(l, r) : primitive->Rem(l, r);
                            primitiveResult = primitive->Or(primitive->ShiftL(quotient, primitive->ConstInt32(1)), primitive->ConstInt64(1));
                            break;
                        }
                        default: {
                            OMR::JitBuilder::IlValue* comparison = nullptr;
                            if (op == OP_LT) comparison = primitive->LessThan(left, right);
                            else if (op == OP_LE) comparison = primitive->LessOrEqualTo(left, right);
                            else if (op == OP_GT) comparison = primitive->GreaterThan(left, right);
                            else if (op == OP_GE) comparison = primitive->GreaterOrEqualTo(left, right);
                            else if (op == OP_EQ) comparison = primitive->EqualTo(left, right);
                            else comparison = primitive->NotEqualTo(left, right);
                            primitiveResult = primitive->Or(primitive->ShiftL(primitive->ConvertTo(Int64, comparison), primitive->ConstInt32(1)), primitive->ConstInt64(1));
                            break;
                        }
                    }
                    primitive->Store("_binary_result", primitiveResult);
                    generic->Store("_binary_result", generic->Call("jit_helper_binary_op", 6,
                        generic->Load("vm_ptr"),
                        generic->ConstInt64(reinterpret_cast<int64_t>(method)),
                        generic->ConstInt32(ic_idx),
                        generic->ConstInt32(static_cast<int32_t>(op)),
                        left,
                        right));
                    stack.push_back(cur->Load("_binary_result"));
                    break;
                }
                case OP_REF_EQ:
                case OP_REF_NE: {
                    OMR::JitBuilder::IlValue* right = stack.back(); stack.pop_back();
                    OMR::JitBuilder::IlValue* left = stack.back(); stack.pop_back();

                    OMR::JitBuilder::IlValue* eq = cur->EqualTo(left, right);
                    OMR::JitBuilder::IlValue* tagged = cur->Or(
                        cur->ShiftL(cur->ConvertTo(Int64, eq), cur->ConstInt32(1)), cur->ConstInt64(1));
                    if (op == OP_REF_NE) {
                        tagged = cur->Xor(tagged, cur->ConstInt64(2));
                    }
                    stack.push_back(tagged);
                    break;
                }
                case OP_JUMP: {
                    int32_t offset = read_i32(ip);
                    uint32_t target = static_cast<uint32_t>(ip - bytecode) + static_cast<uint32_t>(offset);
                    cur->Goto(blockBuilders[target]);
                    terminated = true;
                    break;
                }
                case OP_JUMP_IF_FALSE: {
                    int32_t offset = read_i32(ip);
                    uint32_t target = static_cast<uint32_t>(ip - bytecode) + static_cast<uint32_t>(offset);
                    OMR::JitBuilder::IlValue* cond = stack.back(); stack.pop_back();

                    // Truthiness: (tag & 1) ? (decode_int(cond) != 0) : (value != null)
                    OMR::JitBuilder::IlValue* isInt = cur->ConvertTo(Int32, cur->And(cond, cur->ConstInt64(1)));
                    OMR::JitBuilder::IlValue* asInt = cur->ShiftR(cond, cur->ConstInt32(1));
                    OMR::JitBuilder::IlValue* intTruthy = cur->NotEqualTo(asInt, cur->ConstInt64(0));
                    OMR::JitBuilder::IlValue* objTruthy = cur->NotEqualTo(cond, cur->ConstInt64(make_null()));
                    OMR::JitBuilder::IlValue* truthy = cur->Select(isInt, intTruthy, objTruthy);

                    // Branch when not truthy; otherwise fall through to the next block.
                    cur->IfCmpEqualZero(blockBuilders[target], truthy);
                    terminated = true;
                    break;
                }
                case OP_CALL_GLOBAL: {
                    uint32_t cidx = read_u32(ip);
                    uint32_t argCount = read_u32(ip);
                    const char* funcName = reinterpret_cast<const char*>(method->constants[cidx]);

                    OMR::JitBuilder::IlValue* argsArr = cur->CreateLocalArray(static_cast<int32_t>(argCount), Int64);
                    for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                        OMR::JitBuilder::IlValue* elemAddr = cur->Add(argsArr, cur->ConstInt64(i * static_cast<int64_t>(sizeof(Value))));
                        cur->StoreAt(elemAddr, stack.back());
                        stack.pop_back();
                    }
                    Method* target = vm->getFunction(funcName);
                    OMR::JitBuilder::IlValue* retVal = nullptr;
                    if (target != nullptr && target->jit_entry != nullptr) {
                        // c2c fast path: no name lookup, vector construction,
                        // or VM dispatch.  The argument array is already the
                        // required ABI representation.
                        retVal = cur->ComputedCall((char*)"toyvm_jit_entry", 4,
                            cur->ConstAddress(reinterpret_cast<void*>(target->jit_entry)),
                            cur->Load("vm_ptr"),
                            cur->ConstInt64(make_null()),
                            argsArr);
                    } else {
                        // This is the c2i adapter and also covers targets that
                        // become compiled after this caller was generated.
                        retVal = cur->Call("jit_helper_call_global", 4,
                            cur->Load("vm_ptr"),
                            cur->ConstInt64(reinterpret_cast<int64_t>(funcName)),
                            cur->ConstInt32(static_cast<int32_t>(argCount)),
                            argsArr);
                    }
                    stack.push_back(retVal);
                    break;
                }
                case OP_CALL_METHOD: {
                    uint32_t ic_idx = read_u32(ip);
                    uint32_t argCount = read_u32(ip);

                    OMR::JitBuilder::IlValue* argsArr = cur->CreateLocalArray(static_cast<int32_t>(argCount), Int64);
                    for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                        OMR::JitBuilder::IlValue* elemAddr = cur->Add(argsArr, cur->ConstInt64(i * static_cast<int64_t>(sizeof(Value))));
                        cur->StoreAt(elemAddr, stack.back());
                        stack.pop_back();
                    }
                    OMR::JitBuilder::IlValue* receiver = stack.back(); stack.pop_back();

                    OMR::JitBuilder::IlValue* retVal = cur->Call("jit_helper_call_method", 6,
                        cur->Load("vm_ptr"),
                        cur->ConstInt64(reinterpret_cast<int64_t>(method)),
                        cur->ConstInt32(ic_idx),
                        receiver,
                        cur->ConstInt32(static_cast<int32_t>(argCount)),
                        argsArr);
                    stack.push_back(retVal);
                    break;
                }
                case OP_RETURN: {
                    OMR::JitBuilder::IlValue* retVal = cur->ConstInt64(make_null());
                    if (!stack.empty()) {
                        retVal = stack.back();
                        stack.pop_back();
                    }
                    cur->Store("_retval", retVal);
                    cur->Goto(returnLabel);
                    terminated = true;
                    break;
                }
                case OP_POP: {
                    if (!stack.empty()) stack.pop_back();
                    break;
                }
                case OP_DUP: {
                    if (!stack.empty()) stack.push_back(stack.back());
                    break;
                }
                default:
                    break;
            }
        }

        if (!terminated) {
            // Fell off the end of the final block: implicit null return.
            cur->Store("_retval", cur->ConstInt64(make_null()));
            cur->Goto(returnLabel);
        }
    }

    current->AppendBuilder(returnLabel);
    returnLabel->Return(returnLabel->Load("_retval"));
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
