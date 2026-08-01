#ifndef TOY_INTERPRETER_HPP
#define TOY_INTERPRETER_HPP

#include "vm.hpp"
#include "bytecode.hpp"
#include "class.hpp"
#include "value.hpp"

class Interpreter {
public:
    explicit Interpreter(VM* vm);
    Value execute(Method* method, Value receiver, const std::vector<Value>& args);

private:
    VM* vm;

    Value execute_binary_op(Method* method, uint32_t ic_idx, Opcode op, Value left, Value right);
};

#endif // TOY_INTERPRETER_HPP
