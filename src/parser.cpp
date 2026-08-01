#include "parser.hpp"
#include <iostream>

Parser::Parser(Lexer lexer) : lexer(lexer) {
    advance();
}

void Parser::advance() {
    curToken = lexer.nextToken();
}

bool Parser::match(TokenType type) {
    if (curToken.type == type) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& errMsg) {
    if (curToken.type == type) {
        Token tok = curToken;
        advance();
        return tok;
    }
    throw std::runtime_error("Parse Error on line " + std::to_string(curToken.line) + ": " + errMsg + ", got '" + curToken.text + "'");
}

std::unique_ptr<ProgramAST> Parser::parseProgram() {
    std::vector<std::unique_ptr<ClassAST>> classes;
    std::vector<std::unique_ptr<FunctionAST>> functions;

    while (curToken.type != TokenType::TokEOF) {
        if (curToken.type == TokenType::KwClass) {
            classes.push_back(parseClass());
        } else if (curToken.type == TokenType::KwFn) {
            functions.push_back(parseFunction());
        } else {
            throw std::runtime_error("Unexpected top-level token on line " + std::to_string(curToken.line) + ": " + curToken.text);
        }
    }

    return std::make_unique<ProgramAST>(std::move(classes), std::move(functions));
}

std::unique_ptr<ClassAST> Parser::parseClass() {
    consume(TokenType::KwClass, "Expected 'class'");
    Token nameTok = consume(TokenType::Identifier, "Expected class name");
    consume(TokenType::LBrace, "Expected '{' after class name");

    std::vector<std::string> fields;
    std::vector<std::unique_ptr<FunctionAST>> methods;

    while (curToken.type != TokenType::RBrace && curToken.type != TokenType::TokEOF) {
        if (match(TokenType::KwField)) {
            Token fName = consume(TokenType::Identifier, "Expected field name");
            consume(TokenType::Semicolon, "Expected ';' after field declaration");
            fields.push_back(fName.text);
        } else if (curToken.type == TokenType::KwFn) {
            methods.push_back(parseFunction());
        } else {
            throw std::runtime_error("Unexpected token inside class definition on line " + std::to_string(curToken.line) + ": " + curToken.text);
        }
    }

    consume(TokenType::RBrace, "Expected '}' after class body");
    return std::make_unique<ClassAST>(nameTok.text, std::move(fields), std::move(methods));
}

std::unique_ptr<FunctionAST> Parser::parseFunction() {
    consume(TokenType::KwFn, "Expected 'fn'");
    Token nameTok = consume(TokenType::Identifier, "Expected function name");
    consume(TokenType::LParen, "Expected '(' after function name");

    std::vector<std::string> params;
    if (curToken.type != TokenType::RParen) {
        params.push_back(consume(TokenType::Identifier, "Expected parameter name").text);
        while (match(TokenType::Comma)) {
            params.push_back(consume(TokenType::Identifier, "Expected parameter name").text);
        }
    }
    consume(TokenType::RParen, "Expected ')' after parameters");

    auto body = parseBlock();
    return std::make_unique<FunctionAST>(nameTok.text, std::move(params), std::move(body));
}

std::unique_ptr<BlockStmtAST> Parser::parseBlock() {
    consume(TokenType::LBrace, "Expected '{'");
    std::vector<std::unique_ptr<StmtAST>> stmts;
    while (curToken.type != TokenType::RBrace && curToken.type != TokenType::TokEOF) {
        stmts.push_back(parseStatement());
    }
    consume(TokenType::RBrace, "Expected '}'");
    return std::make_unique<BlockStmtAST>(std::move(stmts));
}

std::unique_ptr<StmtAST> Parser::parseStatement() {
    if (match(TokenType::KwLet)) {
        Token varTok = consume(TokenType::Identifier, "Expected variable name after 'let'");
        consume(TokenType::Assign, "Expected '=' in 'let' statement");
        auto initExpr = parseExpression();
        consume(TokenType::Semicolon, "Expected ';' after 'let' statement");
        return std::make_unique<LetStmtAST>(varTok.text, std::move(initExpr));
    }

    if (match(TokenType::KwIf)) {
        auto cond = parseExpression();
        auto thenBr = parseBlock();
        std::unique_ptr<StmtAST> elseBr = nullptr;
        if (match(TokenType::KwElse)) {
            if (curToken.type == TokenType::KwIf) {
                elseBr = parseStatement();
            } else {
                elseBr = parseBlock();
            }
        }
        return std::make_unique<IfStmtAST>(std::move(cond), std::move(thenBr), std::move(elseBr));
    }

    if (match(TokenType::KwWhile)) {
        auto cond = parseExpression();
        auto body = parseBlock();
        return std::make_unique<WhileStmtAST>(std::move(cond), std::move(body));
    }

    if (match(TokenType::KwReturn)) {
        std::unique_ptr<ExprAST> retExpr = nullptr;
        if (curToken.type != TokenType::Semicolon) {
            retExpr = parseExpression();
        }
        consume(TokenType::Semicolon, "Expected ';' after 'return'");
        return std::make_unique<ReturnStmtAST>(std::move(retExpr));
    }

    // Expression statement or assignment
    auto expr = parseExpression();
    if (match(TokenType::Assign)) {
        auto valExpr = parseExpression();
        consume(TokenType::Semicolon, "Expected ';' after assignment");
        return std::make_unique<AssignStmtAST>(std::move(expr), std::move(valExpr));
    }

    consume(TokenType::Semicolon, "Expected ';' after statement");
    return std::make_unique<ExprStmtAST>(std::move(expr));
}

std::unique_ptr<ExprAST> Parser::parseExpression() {
    return parseEquality();
}

std::unique_ptr<ExprAST> Parser::parseEquality() {
    auto left = parseComparison();
    while (curToken.type == TokenType::EqualEqual || curToken.type == TokenType::BangEqual ||
           curToken.type == TokenType::RefEqual || curToken.type == TokenType::RefNotEqual) {
        std::string op = curToken.text;
        advance();
        auto right = parseComparison();
        left = std::make_unique<BinaryExprAST>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExprAST> Parser::parseComparison() {
    auto left = parseTerm();
    while (curToken.type == TokenType::Less || curToken.type == TokenType::LessEqual ||
           curToken.type == TokenType::Greater || curToken.type == TokenType::GreaterEqual) {
        std::string op = curToken.text;
        advance();
        auto right = parseTerm();
        left = std::make_unique<BinaryExprAST>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExprAST> Parser::parseTerm() {
    auto left = parseFactor();
    while (curToken.type == TokenType::Plus || curToken.type == TokenType::Minus) {
        std::string op = curToken.text;
        advance();
        auto right = parseFactor();
        left = std::make_unique<BinaryExprAST>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExprAST> Parser::parseFactor() {
    auto left = parseUnary();
    while (curToken.type == TokenType::Star || curToken.type == TokenType::Slash || curToken.type == TokenType::Percent) {
        std::string op = curToken.text;
        advance();
        auto right = parseUnary();
        left = std::make_unique<BinaryExprAST>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExprAST> Parser::parseUnary() {
    if (match(TokenType::Minus)) {
        auto operand = parseUnary();
        // Translate -x to 0 - x
        return std::make_unique<BinaryExprAST>("-", std::make_unique<IntLiteralAST>(0), std::move(operand));
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<ExprAST> Parser::parsePrimary() {
    if (curToken.type == TokenType::IntLiteral) {
        int64_t val = curToken.int_val;
        advance();
        return std::make_unique<IntLiteralAST>(val);
    }
    if (match(TokenType::KwNull)) {
        return std::make_unique<NullLiteralAST>();
    }
    if (match(TokenType::KwThis)) {
        return std::make_unique<ThisExprAST>();
    }
    if (curToken.type == TokenType::Identifier) {
        std::string id = curToken.text;
        advance();
        return std::make_unique<VarExprAST>(id);
    }
    if (match(TokenType::LParen)) {
        auto expr = parseExpression();
        consume(TokenType::RParen, "Expected ')'");
        return expr;
    }

    throw std::runtime_error("Unexpected expression token on line " + std::to_string(curToken.line) + ": " + curToken.text);
}

std::unique_ptr<ExprAST> Parser::parsePostfix(std::unique_ptr<ExprAST> expr) {
    while (true) {
        if (match(TokenType::Dot)) {
            Token member = consume(TokenType::Identifier, "Expected member name after '.'");
            if (member.text == "new") {
                // ClassName.new() object allocation or receiver.new()
                if (expr->getType() == ASTNodeType::VarExpr) {
                    std::string class_name = static_cast<VarExprAST*>(expr.get())->name;
                    consume(TokenType::LParen, "Expected '(' after 'new'");
                    consume(TokenType::RParen, "Expected ')' after 'new'");
                    expr = std::make_unique<NewExprAST>(class_name);
                    continue;
                }
            }

            if (match(TokenType::LParen)) {
                // Method call: receiver.member(args...)
                std::vector<std::unique_ptr<ExprAST>> args;
                if (curToken.type != TokenType::RParen) {
                    args.push_back(parseExpression());
                    while (match(TokenType::Comma)) {
                        args.push_back(parseExpression());
                    }
                }
                consume(TokenType::RParen, "Expected ')' after method call arguments");
                expr = std::make_unique<CallExprAST>(std::move(expr), member.text, std::move(args));
            } else {
                // Field access: receiver.member
                expr = std::make_unique<FieldAccessAST>(std::move(expr), member.text);
            }
        } else if (expr->getType() == ASTNodeType::VarExpr && match(TokenType::LParen)) {
            // Function call: func_name(args...)
            std::string func_name = static_cast<VarExprAST*>(expr.get())->name;
            std::vector<std::unique_ptr<ExprAST>> args;
            if (curToken.type != TokenType::RParen) {
                args.push_back(parseExpression());
                while (match(TokenType::Comma)) {
                    args.push_back(parseExpression());
                }
            }
            consume(TokenType::RParen, "Expected ')' after function arguments");
            expr = std::make_unique<CallExprAST>(nullptr, func_name, std::move(args));
        } else {
            break;
        }
    }
    return expr;
}
