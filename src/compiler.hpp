#ifndef TOY_COMPILER_HPP
#define TOY_COMPILER_HPP

#include "ast.hpp"
#include "bytecode.hpp"
#include "class.hpp"
#include "inline_cache.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

class Compiler {
public:
    Compiler();

    void compileProgram(const ProgramAST* program,
                        std::vector<std::unique_ptr<Class>>& out_classes,
                        std::vector<std::unique_ptr<Method>>& out_functions,
                        std::unordered_map<std::string, Class*>& out_class_map,
                        std::unordered_map<std::string, Method*>& out_func_map);

private:
    Class* current_class = nullptr;
    Method* current_method = nullptr;
    std::vector<std::string> local_names;

    uint32_t add_constant(Value val);
    uint32_t add_inline_cache(const std::string& methodName);
    int32_t find_or_add_local(const std::string& name);

    void compileFunction(const FunctionAST* funcAST, Method* method, Class* parentClass = nullptr);
    void compileStatement(const StmtAST* stmt);
    void compileExpression(const ExprAST* expr);
};

#endif // TOY_COMPILER_HPP
