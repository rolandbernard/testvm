#include "lexer.hpp"
#include <cctype>

Lexer::Lexer(std::string source) : source(std::move(source)) {}

char Lexer::peek() const {
    if (pos >= source.size()) return '\0';
    return source[pos];
}

char Lexer::advance() {
    if (pos >= source.size()) return '\0';
    char c = source[pos++];
    if (c == '\n') line++;
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos < source.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '/') {
            advance();
            advance();
            while (pos < source.size() && peek() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::nextToken() {
    skipWhitespaceAndComments();
    if (pos >= source.size()) {
        return Token{TokenType::TokEOF, "", 0, line};
    }

    size_t start_line = line;
    char c = advance();

    // Identifiers & Keywords
    if (std::isalpha(c) || c == '_') {
        std::string id;
        id += c;
        while (std::isalnum(peek()) || peek() == '_') {
            id += advance();
        }

        if (id == "fn") return Token{TokenType::KwFn, id, 0, start_line};
        if (id == "class") return Token{TokenType::KwClass, id, 0, start_line};
        if (id == "field") return Token{TokenType::KwField, id, 0, start_line};
        if (id == "let") return Token{TokenType::KwLet, id, 0, start_line};
        if (id == "if") return Token{TokenType::KwIf, id, 0, start_line};
        if (id == "else") return Token{TokenType::KwElse, id, 0, start_line};
        if (id == "while") return Token{TokenType::KwWhile, id, 0, start_line};
        if (id == "return") return Token{TokenType::KwReturn, id, 0, start_line};
        if (id == "this") return Token{TokenType::KwThis, id, 0, start_line};
        if (id == "null") return Token{TokenType::KwNull, id, 0, start_line};

        return Token{TokenType::Identifier, id, 0, start_line};
    }

    // Integers
    if (std::isdigit(c)) {
        std::string numStr;
        numStr += c;
        while (std::isdigit(peek())) {
            numStr += advance();
        }
        int64_t val = std::stoll(numStr);
        return Token{TokenType::IntLiteral, numStr, val, start_line};
    }

    // Operators & Delimiters
    switch (c) {
        case '+': return Token{TokenType::Plus, "+", 0, start_line};
        case '-': return Token{TokenType::Minus, "-", 0, start_line};
        case '*': return Token{TokenType::Star, "*", 0, start_line};
        case '/': return Token{TokenType::Slash, "/", 0, start_line};
        case '%': return Token{TokenType::Percent, "%", 0, start_line};
        case '.': return Token{TokenType::Dot, ".", 0, start_line};
        case ',': return Token{TokenType::Comma, ",", 0, start_line};
        case ';': return Token{TokenType::Semicolon, ";", 0, start_line};
        case '(': return Token{TokenType::LParen, "(", 0, start_line};
        case ')': return Token{TokenType::RParen, ")", 0, start_line};
        case '{': return Token{TokenType::LBrace, "{", 0, start_line};
        case '}': return Token{TokenType::RBrace, "}", 0, start_line};
        case '=':
            if (peek() == '=') {
                advance();
                if (peek() == '=') {
                    advance();
                    return Token{TokenType::RefEqual, "===", 0, start_line};
                }
                return Token{TokenType::EqualEqual, "==", 0, start_line};
            }
            return Token{TokenType::Assign, "=", 0, start_line};
        case '!':
            if (peek() == '=') {
                advance();
                if (peek() == '=') {
                    advance();
                    return Token{TokenType::RefNotEqual, "!==", 0, start_line};
                }
                return Token{TokenType::BangEqual, "!=", 0, start_line};
            }
            return Token{TokenType::TokError, "Unexpected '!'", 0, start_line};
        case '<':
            if (peek() == '=') {
                advance();
                return Token{TokenType::LessEqual, "<=", 0, start_line};
            }
            return Token{TokenType::Less, "<", 0, start_line};
        case '>':
            if (peek() == '=') {
                advance();
                return Token{TokenType::GreaterEqual, ">=", 0, start_line};
            }
            return Token{TokenType::Greater, ">", 0, start_line};
        default:
            break;
    }

    std::string errText(1, c);
    return Token{TokenType::TokError, "Unexpected character: " + errText, 0, start_line};
}
