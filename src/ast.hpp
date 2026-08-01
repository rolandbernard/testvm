#ifndef TOY_AST_HPP
#define TOY_AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

enum class ASTNodeType {
    IntLiteral,
    NullLiteral,
    VarExpr,
    ThisExpr,
    FieldAccess,
    NewExpr,
    BinaryExpr,
    CallExpr,

    BlockStmt,
    LetStmt,
    AssignStmt,
    IfStmt,
    WhileStmt,
    ReturnStmt,
    ExprStmt,

    FunctionDecl,
    ClassDecl,
    Program
};

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual ASTNodeType getType() const = 0;
};

struct ExprAST : public ASTNode {};
struct StmtAST : public ASTNode {};

struct IntLiteralAST : public ExprAST {
    int64_t value;
    explicit IntLiteralAST(int64_t val) : value(val) {}
    ASTNodeType getType() const override { return ASTNodeType::IntLiteral; }
};

struct NullLiteralAST : public ExprAST {
    ASTNodeType getType() const override { return ASTNodeType::NullLiteral; }
};

struct VarExprAST : public ExprAST {
    std::string name;
    explicit VarExprAST(std::string name) : name(std::move(name)) {}
    ASTNodeType getType() const override { return ASTNodeType::VarExpr; }
};

struct ThisExprAST : public ExprAST {
    ASTNodeType getType() const override { return ASTNodeType::ThisExpr; }
};

struct FieldAccessAST : public ExprAST {
    std::unique_ptr<ExprAST> target;
    std::string field_name;
    FieldAccessAST(std::unique_ptr<ExprAST> target, std::string field_name)
        : target(std::move(target)), field_name(std::move(field_name)) {}
    ASTNodeType getType() const override { return ASTNodeType::FieldAccess; }
};

struct NewExprAST : public ExprAST {
    std::string class_name;
    explicit NewExprAST(std::string class_name) : class_name(std::move(class_name)) {}
    ASTNodeType getType() const override { return ASTNodeType::NewExpr; }
};

struct BinaryExprAST : public ExprAST {
    std::string op;
    std::unique_ptr<ExprAST> left;
    std::unique_ptr<ExprAST> right;
    BinaryExprAST(std::string op, std::unique_ptr<ExprAST> left, std::unique_ptr<ExprAST> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {}
    ASTNodeType getType() const override { return ASTNodeType::BinaryExpr; }
};

struct CallExprAST : public ExprAST {
    std::unique_ptr<ExprAST> target; // Null for global func call
    std::string func_name;
    std::vector<std::unique_ptr<ExprAST>> args;
    CallExprAST(std::unique_ptr<ExprAST> target, std::string func_name, std::vector<std::unique_ptr<ExprAST>> args)
        : target(std::move(target)), func_name(std::move(func_name)), args(std::move(args)) {}
    ASTNodeType getType() const override { return ASTNodeType::CallExpr; }
};

struct BlockStmtAST : public StmtAST {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    explicit BlockStmtAST(std::vector<std::unique_ptr<StmtAST>> stmts)
        : stmts(std::move(stmts)) {}
    ASTNodeType getType() const override { return ASTNodeType::BlockStmt; }
};

struct LetStmtAST : public StmtAST {
    std::string name;
    std::unique_ptr<ExprAST> init_expr;
    LetStmtAST(std::string name, std::unique_ptr<ExprAST> init_expr)
        : name(std::move(name)), init_expr(std::move(init_expr)) {}
    ASTNodeType getType() const override { return ASTNodeType::LetStmt; }
};

struct AssignStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> target; // VarExprAST or FieldAccessAST
    std::unique_ptr<ExprAST> value;
    AssignStmtAST(std::unique_ptr<ExprAST> target, std::unique_ptr<ExprAST> value)
        : target(std::move(target)), value(std::move(value)) {}
    ASTNodeType getType() const override { return ASTNodeType::AssignStmt; }
};

struct IfStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> then_branch;
    std::unique_ptr<StmtAST> else_branch;
    IfStmtAST(std::unique_ptr<ExprAST> cond, std::unique_ptr<StmtAST> then_br, std::unique_ptr<StmtAST> else_br)
        : condition(std::move(cond)), then_branch(std::move(then_br)), else_branch(std::move(else_br)) {}
    ASTNodeType getType() const override { return ASTNodeType::IfStmt; }
};

struct WhileStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> body;
    WhileStmtAST(std::unique_ptr<ExprAST> cond, std::unique_ptr<StmtAST> body)
        : condition(std::move(cond)), body(std::move(body)) {}
    ASTNodeType getType() const override { return ASTNodeType::WhileStmt; }
};

struct ReturnStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> expr; // May be null
    explicit ReturnStmtAST(std::unique_ptr<ExprAST> expr)
        : expr(std::move(expr)) {}
    ASTNodeType getType() const override { return ASTNodeType::ReturnStmt; }
};

struct ExprStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> expr;
    explicit ExprStmtAST(std::unique_ptr<ExprAST> expr)
        : expr(std::move(expr)) {}
    ASTNodeType getType() const override { return ASTNodeType::ExprStmt; }
};

struct FunctionAST : public ASTNode {
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<BlockStmtAST> body;
    FunctionAST(std::string name, std::vector<std::string> params, std::unique_ptr<BlockStmtAST> body)
        : name(std::move(name)), params(std::move(params)), body(std::move(body)) {}
    ASTNodeType getType() const override { return ASTNodeType::FunctionDecl; }
};

struct ClassAST : public ASTNode {
    std::string name;
    std::vector<std::string> fields;
    std::vector<std::unique_ptr<FunctionAST>> methods;
    ClassAST(std::string name, std::vector<std::string> fields, std::vector<std::unique_ptr<FunctionAST>> methods)
        : name(std::move(name)), fields(std::move(fields)), methods(std::move(methods)) {}
    ASTNodeType getType() const override { return ASTNodeType::ClassDecl; }
};

struct ProgramAST : public ASTNode {
    std::vector<std::unique_ptr<ClassAST>> classes;
    std::vector<std::unique_ptr<FunctionAST>> functions;
    ProgramAST(std::vector<std::unique_ptr<ClassAST>> classes, std::vector<std::unique_ptr<FunctionAST>> functions)
        : classes(std::move(classes)), functions(std::move(functions)) {}
    ASTNodeType getType() const override { return ASTNodeType::Program; }
};

#endif // TOY_AST_HPP
