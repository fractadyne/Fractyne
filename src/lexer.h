#ifndef FRACTYNE_LEXER_H
#define FRACTYNE_LEXER_H

#include "diag.h"

typedef enum {
    TOK_EOF,

    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,
    TOK_IDENT,

    TOK_LET, TOK_FR, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN, TOK_OUTPUT,
    TOK_PUSH, TOK_LEN, TOK_BREAK, TOK_CONTINUE, TOK_STRUCT, TOK_INVOLVE, TOK_ENUM,
    TOK_SORT, TOK_REVERSE, TOK_CONTAINS, TOK_REMOVE, TOK_KEYS, TOK_IN,
    TOK_TRUE, TOK_FALSE,
    TOK_TYPE_INT, TOK_TYPE_FLOAT, TOK_TYPE_BOOL, TOK_TYPE_STRING, TOK_TYPE_LIST, TOK_TYPE_MAP,

    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN, TOK_EQ, TOK_BANG, TOK_NE,
    TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_AND, TOK_OR,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_SHL, TOK_SHR,
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN,

    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMI, TOK_COLON, TOK_ARROW, TOK_DOT, TOK_QUESTION
} TokenType;

typedef struct {
    TokenType type;
    char *text;       /* heap-allocated lexeme; for TOK_STRING_LIT this is the
                          raw bytes between the quotes, escapes untouched */
    long int_val;      /* valid for TOK_INT_LIT */
    double float_val;  /* valid for TOK_FLOAT_LIT */
    int line;
} Token;

typedef struct {
    Token *tokens;
    int count;
} TokenList;

/* Lexes the whole source into a token list (terminated by a TOK_EOF entry).
 * Returns 1 on success, 0 on failure (diag is set). */
int lex(const char *source, TokenList *out, Diag *diag);
void token_list_free(TokenList *list);

const char *token_type_name(TokenType t);

#endif
