#ifndef TOY_PARSER_HPP
#define TOY_PARSER_HPP

#include "lexer.hpp"
#include "ast.hpp"
#include <memory>
#include <stdexcept>

class Parser {
public:
    explicit Parser(Lexer lexer);
    std::unique_ptr<ProgramAST> parseProgram();

private:
    Lexer lexer;
    Token curToken;

    void advance();
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& errMsg);

    std::unique_ptr<ClassAST> parseClass();
    std::unique_ptr<FunctionAST> parseFunction();
    std::unique_ptr<BlockStmtAST> parseBlock();
    std::unique_ptr<StmtAST> parseStatement();
    std::unique_ptr<ExprAST> parseExpression();
    std::unique_ptr<ExprAST> parseEquality();
    std::unique_ptr<ExprAST> parseComparison();
    std::unique_ptr<ExprAST> parseTerm();
    std::unique_ptr<ExprAST> parseFactor();
    std::unique_ptr<ExprAST> parseUnary();
    std::unique_ptr<ExprAST> parsePrimary();
    std::unique_ptr<ExprAST> parsePostfix(std::unique_ptr<ExprAST> expr);
};

#endif // TOY_PARSER_HPP
