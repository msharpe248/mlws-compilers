/* The scanner. Errors are reported and skipped, as in jlox, so the parser
 * only ever sees valid tokens. */
#include <string.h>

#include "lox.h"

static const char *start, *cur;
static int line;

void scanner_init(const char *src) {
    start = cur = src;
    line = 1;
}

static bool at_end(void) { return *cur == '\0'; }
static char advance(void) { return *cur++; }
static char peek(void) { return *cur; }
static char peek_next(void) { return at_end() ? '\0' : cur[1]; }

static bool match(char c) {
    if (at_end() || *cur != c) return false;
    cur++;
    return true;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static Token make(TokenType type) {
    return (Token){type, start, (int)(cur - start), line};
}

static TokenType keyword(const char *s, int n) {
    static const struct { const char *word; TokenType type; } kw[] = {
        {"and", T_AND}, {"class", T_CLASS}, {"else", T_ELSE}, {"false", T_FALSE},
        {"for", T_FOR}, {"fun", T_FUN}, {"if", T_IF}, {"nil", T_NIL},
        {"or", T_OR}, {"print", T_PRINT}, {"return", T_RETURN}, {"super", T_SUPER},
        {"this", T_THIS}, {"true", T_TRUE}, {"var", T_VAR}, {"while", T_WHILE},
    };
    for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++)
        if ((int)strlen(kw[i].word) == n && memcmp(kw[i].word, s, (size_t)n) == 0)
            return kw[i].type;
    return T_IDENTIFIER;
}

Token scan_token(void) {
    for (;;) {
        /* whitespace and comments */
        for (;;) {
            char c = peek();
            if (c == ' ' || c == '\r' || c == '\t') cur++;
            else if (c == '\n') { line++; cur++; }
            else if (c == '/' && peek_next() == '/') {
                while (peek() != '\n' && !at_end()) cur++;
            } else break;
        }
        start = cur;
        if (at_end()) return make(T_EOF);

        char c = advance();
        if (is_alpha(c)) {
            while (is_alpha(peek()) || is_digit(peek())) cur++;
            return make(keyword(start, (int)(cur - start)));
        }
        if (is_digit(c)) {
            while (is_digit(peek())) cur++;
            if (peek() == '.' && is_digit(peek_next())) {
                cur++;
                while (is_digit(peek())) cur++;
            }
            return make(T_NUMBER);
        }
        switch (c) {
        case '(': return make(T_LEFT_PAREN);
        case ')': return make(T_RIGHT_PAREN);
        case '{': return make(T_LEFT_BRACE);
        case '}': return make(T_RIGHT_BRACE);
        case ',': return make(T_COMMA);
        case '.': return make(T_DOT);
        case '-': return make(T_MINUS);
        case '+': return make(T_PLUS);
        case ';': return make(T_SEMICOLON);
        case '*': return make(T_STAR);
        case '/': return make(T_SLASH);
        case '!': return make(match('=') ? T_BANG_EQUAL : T_BANG);
        case '=': return make(match('=') ? T_EQUAL_EQUAL : T_EQUAL);
        case '<': return make(match('=') ? T_LESS_EQUAL : T_LESS);
        case '>': return make(match('=') ? T_GREATER_EQUAL : T_GREATER);
        case '"': {
            while (peek() != '"' && !at_end()) {
                if (peek() == '\n') line++;
                cur++;
            }
            if (at_end()) {
                error_line(line, "Unterminated string.");
                continue;
            }
            cur++; /* the closing quote */
            Token t = make(T_STRING);
            return t;
        }
        default:
            error_line(line, "Unexpected character.");
            continue;
        }
    }
}
