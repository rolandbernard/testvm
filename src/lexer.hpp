#ifndef TOY_LEXER_HPP
#define TOY_LEXER_HPP

#include <string>
#include <vector>
#include <cstdint>

enum class TokenType {
    // Keywords
    KwFn,
    KwClass,
    KwField,
    KwLet,
    KwIf,
    KwElse,
    KwWhile,
    KwReturn,
    KwThis,
    KwNull,

    // Identifiers & Literals
    Identifier,
    IntLiteral,

    // Operators
    Plus,          // +
    Minus,         // -
    Star,          // *
    Slash,         // /
    Percent,       // %
    Less,          // <
    LessEqual,     // <=
    Greater,       // >
    GreaterEqual,  // >=
    EqualEqual,    // ==
    BangEqual,     // !=
    RefEqual,      // ===
    RefNotEqual,   // !==
    Assign,        // =

    // Delimiters
    Dot,           // .
    Comma,         // ,
    Semicolon,     // ;
    LParen,        // (
    RParen,        // )
    LBrace,        // {
    RBrace,        // }

    TokEOF,
    TokError
};

struct Token {
    TokenType type;
    std::string text;
    int64_t int_val = 0;
    size_t line = 1;
};

class Lexer {
public:
    explicit Lexer(std::string source);
    Token nextToken();

private:
    std::string source;
    size_t pos = 0;
    size_t line = 1;

    char peek() const;
    char advance();
    void skipWhitespaceAndComments();
};

#endif // TOY_LEXER_HPP
