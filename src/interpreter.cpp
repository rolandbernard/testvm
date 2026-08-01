#include "interpreter.hpp"
#include <stdexcept>
#include <iostream>
#include <cstring>

Interpreter::Interpreter(VM* vm) : vm(vm) {}

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

Value Interpreter::execute_binary_op(Method* method, uint32_t ic_idx, Opcode op, Value left, Value right) {
    // 1. Fast path: Primitive Tagged SmallInt operations
    if (is_int(left) && is_int(right)) {
        int64_t l = decode_int(left);
        int64_t r = decode_int(right);
        int64_t res = 0;

        switch (op) {
            case OP_ADD: res = l + r; break;
            case OP_SUB: res = l - r; break;
            case OP_MUL: res = l * r; break;
            case OP_DIV:
                if (r == 0) throw std::runtime_error("Division by zero");
                res = l / r;
                break;
            case OP_REM:
                if (r == 0) throw std::runtime_error("Division by zero");
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

    // 2. Object Operator Overloading via Inline Cache
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

    throw std::runtime_error("Unsupported operand types for binary operation");
}

Value Interpreter::execute(Method* method, Value receiver, const std::vector<Value>& args) {
    auto& callStack = vm->getCallStack();
    callStack.emplace_back();
    StackFrame& frame = callStack.back();
    frame.method = method;

    // Initialize local variables array
    frame.locals.resize(method->num_locals, make_null());

    // Parameters setup:
    // For methods, param 0 is 'this' (receiver).
    size_t localOffset = 0;
    if (!method->params.empty() && method->params[0] == "this") {
        frame.locals[0] = receiver;
        localOffset = 1;
    }

    for (size_t i = 0; i < args.size() && (i + localOffset) < frame.locals.size(); ++i) {
        frame.locals[i + localOffset] = args[i];
    }

    const uint8_t* ip = method->bytecode.data();
    const uint8_t* ipEnd = ip + method->bytecode.size();

    auto& evalStack = frame.eval_stack;

    while (ip < ipEnd) {
        Opcode op = static_cast<Opcode>(*ip++);

        switch (op) {
            case OP_PUSH_CONST: {
                uint32_t cidx = read_u32(ip);
                evalStack.push_back(method->constants[cidx]);
                break;
            }
            case OP_PUSH_NULL: {
                evalStack.push_back(make_null());
                break;
            }
            case OP_LOAD_LOCAL: {
                uint32_t lidx = read_u32(ip);
                evalStack.push_back(frame.locals[lidx]);
                break;
            }
            case OP_STORE_LOCAL: {
                uint32_t lidx = read_u32(ip);
                Value val = evalStack.back();
                evalStack.pop_back();
                frame.locals[lidx] = val;
                break;
            }
            case OP_LOAD_FIELD: {
                uint32_t fidx = read_u32(ip);
                Value thisVal = frame.locals[0];
                if (!is_obj(thisVal)) {
                    throw std::runtime_error("Attempted field access on non-object 'this'");
                }
                ToyObject* obj = decode_obj(thisVal);
                evalStack.push_back(obj->fields[fidx]);
                break;
            }
            case OP_STORE_FIELD: {
                uint32_t fidx = read_u32(ip);
                Value val = evalStack.back();
                evalStack.pop_back();
                Value thisVal = frame.locals[0];
                if (!is_obj(thisVal)) {
                    throw std::runtime_error("Attempted field assignment on non-object 'this'");
                }
                ToyObject* obj = decode_obj(thisVal);
                obj->fields[fidx] = val;
                break;
            }
            case OP_NEW_OBJECT: {
                uint32_t cidx = read_u32(ip);
                const char* className = reinterpret_cast<const char*>(method->constants[cidx]);
                Class* klass = vm->getClass(className);
                if (!klass) {
                    throw std::runtime_error("Unknown class '" + std::string(className) + "'");
                }
                ToyObject* obj = vm->allocate_object(klass);
                evalStack.push_back(encode_obj(obj));
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
                Value right = evalStack.back(); evalStack.pop_back();
                Value left = evalStack.back(); evalStack.pop_back();
                Value res = execute_binary_op(method, ic_idx, op, left, right);
                evalStack.push_back(res);
                break;
            }
            case OP_REF_EQ: {
                Value right = evalStack.back(); evalStack.pop_back();
                Value left = evalStack.back(); evalStack.pop_back();
                evalStack.push_back(encode_int((left == right) ? 1 : 0));
                break;
            }
            case OP_REF_NE: {
                Value right = evalStack.back(); evalStack.pop_back();
                Value left = evalStack.back(); evalStack.pop_back();
                evalStack.push_back(encode_int((left != right) ? 1 : 0));
                break;
            }
            case OP_JUMP: {
                int32_t offset = read_i32(ip);
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                int32_t offset = read_i32(ip);
                Value cond = evalStack.back(); evalStack.pop_back();
                if (!is_truthy(cond)) {
                    ip += offset;
                }
                break;
            }
            case OP_CALL_GLOBAL: {
                uint32_t cidx = read_u32(ip);
                uint32_t argCount = read_u32(ip);
                const char* funcName = reinterpret_cast<const char*>(method->constants[cidx]);
                Method* globalFunc = vm->getFunction(funcName);
                if (!globalFunc) {
                    throw std::runtime_error("Global function not found: " + std::string(funcName));
                }

                std::vector<Value> callArgs(argCount);
                for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                    callArgs[i] = evalStack.back();
                    evalStack.pop_back();
                }

                Value retVal = vm->call_global(globalFunc, callArgs);
                evalStack.push_back(retVal);
                break;
            }
            case OP_CALL_METHOD: {
                uint32_t ic_idx = read_u32(ip);
                uint32_t argCount = read_u32(ip);

                std::vector<Value> callArgs(argCount);
                for (int i = static_cast<int>(argCount) - 1; i >= 0; --i) {
                    callArgs[i] = evalStack.back();
                    evalStack.pop_back();
                }

                Value targetRecv = evalStack.back();
                evalStack.pop_back();

                if (!is_obj(targetRecv)) {
                    throw std::runtime_error("Attempted method call on non-object receiver");
                }

                ToyObject* obj = decode_obj(targetRecv);
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

                Value retVal = vm->call_method(targetMethod, targetRecv, callArgs);
                evalStack.push_back(retVal);
                break;
            }
            case OP_RETURN: {
                Value retVal = make_null();
                if (!evalStack.empty()) {
                    retVal = evalStack.back();
                }
                callStack.pop_back();
                return retVal;
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
                throw std::runtime_error("Unknown bytecode opcode");
        }
    }

    callStack.pop_back();
    return make_null();
}
