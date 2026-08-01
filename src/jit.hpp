#ifndef TOY_JIT_HPP
#define TOY_JIT_HPP

#include "class.hpp"
#include "value.hpp"

class VM;

bool init_jit_compiler();
void shutdown_jit_compiler();
bool compile_method_with_jit(VM* vm, Method* method);

#endif // TOY_JIT_HPP
