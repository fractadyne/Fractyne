#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;

    Token *tokens;
    int count;
    int capacity;
} Lexer;

static void push_token(Lexer *lx, TokenType type, const char *text_start,
                        size_t text_len, long int_val, double float_val,
                        int line) {
    if (lx->count == lx->capacity) {
        lx->capacity = lx->capacity == 0 ? 64 : lx->capacity * 2;
        lx->tokens = realloc(lx->tokens, (size_t)lx->capacity * sizeof(Token));
    }
    Token *t = &lx->tokens[lx->count++];
    t->type = type;
    t->int_val = int_val;
    t->float_val = float_val;
    t->line = line;
    if (text_start != NULL) {
        t->text = malloc(text_len + 1);
        memcpy(t->text, text_start, text_len);
        t->text[text_len] = '\0';
    } else {
        t->text = NULL;
    }
}

static int at_end(Lexer *lx) { return lx->pos >= lx->len; }
static char peek(Lexer *lx) { return at_end(lx) ? '\0' : lx->src[lx->pos]; }
static char peek_next(Lexer *lx) {
    return (lx->pos + 1 >= lx->len) ? '\0' : lx->src[lx->pos + 1];
}
static char advance(Lexer *lx) { return lx->src[lx->pos++]; }
static int match(Lexer *lx, char expected) {
    if (peek(lx) != expected) return 0;
    lx->pos++;
    return 1;
}

typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"let", TOK_LET},       {"fixed", TOK_FIXED},   {"fr", TOK_FR},         {"if", TOK_IF},
    {"else", TOK_ELSE},     {"while", TOK_WHILE},   {"for", TOK_FOR},
    {"return", TOK_RETURN}, {"output", TOK_OUTPUT}, {"push", TOK_PUSH},
    {"len", TOK_LEN},       {"break", TOK_BREAK},   {"continue", TOK_CONTINUE},
    {"struct", TOK_STRUCT}, {"involve", TOK_INVOLVE}, {"enum", TOK_ENUM},
    {"sort", TOK_SORT},     {"reverse", TOK_REVERSE}, {"contains", TOK_CONTAINS},
    {"remove", TOK_REMOVE}, {"keys", TOK_KEYS}, {"in", TOK_IN},
    {"true", TOK_TRUE},     {"false", TOK_FALSE},
    {"int", TOK_TYPE_INT},  {"float", TOK_TYPE_FLOAT},
    {"bool", TOK_TYPE_BOOL}, {"string", TOK_TYPE_STRING}, {"list", TOK_TYPE_LIST},
    {"map", TOK_TYPE_MAP},
};
#define KEYWORD_COUNT (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))

static int lex_string(Lexer *lx, Diag *diag) {
    int start_line = lx->line;
    size_t start = lx->pos; /* first char after opening quote */
    while (!at_end(lx) && peek(lx) != '"') {
        if (peek(lx) == '\n') {
            diag_set(diag, start_line, "unterminated string literal");
            return 0;
        }
        if (peek(lx) == '\\' && !at_end(lx)) {
            advance(lx); /* backslash */
            if (!at_end(lx)) advance(lx); /* escaped char, passed through verbatim */
            continue;
        }
        advance(lx);
    }
    if (at_end(lx)) {
        diag_set(diag, start_line, "unterminated string literal");
        return 0;
    }
    size_t text_len = lx->pos - start;
    const char *text_start = lx->src + start;
    advance(lx); /* closing quote */
    push_token(lx, TOK_STRING_LIT, text_start, text_len, 0, 0, start_line);
    return 1;
}

static void lex_number(Lexer *lx) {
    size_t start = lx->pos;
    int line = lx->line;
    int is_float = 0;
    while (isdigit((unsigned char)peek(lx))) advance(lx);
    if (peek(lx) == '.' && isdigit((unsigned char)peek_next(lx))) {
        is_float = 1;
        advance(lx); /* '.' */
        while (isdigit((unsigned char)peek(lx))) advance(lx);
    }
    size_t len = lx->pos - start;
    char buf[64];
    size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, lx->src + start, copy_len);
    buf[copy_len] = '\0';
    if (is_float) {
        push_token(lx, TOK_FLOAT_LIT, lx->src + start, len, 0, atof(buf), line);
    } else {
        push_token(lx, TOK_INT_LIT, lx->src + start, len, atol(buf), 0, line);
    }
}

static void lex_ident_or_keyword(Lexer *lx) {
    size_t start = lx->pos;
    int line = lx->line;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    size_t len = lx->pos - start;
    const char *text = lx->src + start;

    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        size_t kw_len = strlen(KEYWORDS[i].word);
        if (kw_len == len && strncmp(KEYWORDS[i].word, text, len) == 0) {
            push_token(lx, KEYWORDS[i].type, text, len, 0, 0, line);
            return;
        }
    }
    push_token(lx, TOK_IDENT, text, len, 0, 0, line);
}

int lex(const char *source, TokenList *out, Diag *diag) {
    /* Some editors write a UTF-8 byte-order mark at the very start of a
     * file; it's invisible in the editor, so silently skip it rather than
     * fail with a baffling "unexpected character" on line 1. */
    if ((unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        source += 3;
    }

    Lexer lx = {0};
    lx.src = source;
    lx.len = strlen(source);
    lx.line = 1;

    while (!at_end(&lx)) {
        char c = peek(&lx);

        if (c == ' ' || c == '\t' || c == '\r') {
            advance(&lx);
            continue;
        }
        if (c == '\n') {
            lx.line++;
            advance(&lx);
            continue;
        }
        if (c == '/' && peek_next(&lx) == '/') {
            while (!at_end(&lx) && peek(&lx) != '\n') advance(&lx);
            continue;
        }
        if (c == '/' && peek_next(&lx) == '*') {
            int start_line = lx.line;
            advance(&lx); /* '/' */
            advance(&lx); /* '*' */
            while (!at_end(&lx) && !(peek(&lx) == '*' && peek_next(&lx) == '/')) {
                if (peek(&lx) == '\n') lx.line++;
                advance(&lx);
            }
            if (at_end(&lx)) {
                diag_set(diag, start_line, "unterminated block comment");
                free(lx.tokens);
                return 0;
            }
            advance(&lx); /* '*' */
            advance(&lx); /* '/' */
            continue;
        }
        if (c == '"') {
            advance(&lx); /* opening quote */
            if (!lex_string(&lx, diag)) {
                free(lx.tokens);
                return 0;
            }
            continue;
        }
        if (isdigit((unsigned char)c)) {
            lex_number(&lx);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            lex_ident_or_keyword(&lx);
            continue;
        }

        int line = lx.line;
        advance(&lx);
        switch (c) {
            case '+':
                if (match(&lx, '=')) push_token(&lx, TOK_PLUS_ASSIGN, NULL, 0, 0, 0, line);
                else if (match(&lx, '+')) push_token(&lx, TOK_PLUS_PLUS, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_PLUS, NULL, 0, 0, 0, line);
                break;
            case '-':
                if (match(&lx, '>')) push_token(&lx, TOK_ARROW, NULL, 0, 0, 0, line);
                else if (match(&lx, '=')) push_token(&lx, TOK_MINUS_ASSIGN, NULL, 0, 0, 0, line);
                else if (match(&lx, '-')) push_token(&lx, TOK_MINUS_MINUS, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_MINUS, NULL, 0, 0, 0, line);
                break;
            case '*':
                if (match(&lx, '=')) push_token(&lx, TOK_STAR_ASSIGN, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_STAR, NULL, 0, 0, 0, line);
                break;
            case '/':
                if (match(&lx, '=')) push_token(&lx, TOK_SLASH_ASSIGN, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_SLASH, NULL, 0, 0, 0, line);
                break;
            case '%':
                if (match(&lx, '=')) push_token(&lx, TOK_PERCENT_ASSIGN, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_PERCENT, NULL, 0, 0, 0, line);
                break;
            case '=':
                if (match(&lx, '=')) push_token(&lx, TOK_EQ, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_ASSIGN, NULL, 0, 0, 0, line);
                break;
            case '!':
                if (match(&lx, '=')) push_token(&lx, TOK_NE, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_BANG, NULL, 0, 0, 0, line);
                break;
            case '<':
                if (match(&lx, '=')) push_token(&lx, TOK_LE, NULL, 0, 0, 0, line);
                else if (match(&lx, '<')) {
                    if (match(&lx, '=')) push_token(&lx, TOK_SHL_ASSIGN, NULL, 0, 0, 0, line);
                    else push_token(&lx, TOK_SHL, NULL, 0, 0, 0, line);
                } else push_token(&lx, TOK_LT, NULL, 0, 0, 0, line);
                break;
            case '>':
                if (match(&lx, '=')) push_token(&lx, TOK_GE, NULL, 0, 0, 0, line);
                else if (match(&lx, '>')) {
                    if (match(&lx, '=')) push_token(&lx, TOK_SHR_ASSIGN, NULL, 0, 0, 0, line);
                    else push_token(&lx, TOK_SHR, NULL, 0, 0, 0, line);
                } else push_token(&lx, TOK_GT, NULL, 0, 0, 0, line);
                break;
            case '&':
                if (match(&lx, '&')) { push_token(&lx, TOK_AND, NULL, 0, 0, 0, line); break; }
                if (match(&lx, '=')) { push_token(&lx, TOK_AMP_ASSIGN, NULL, 0, 0, 0, line); break; }
                push_token(&lx, TOK_AMP, NULL, 0, 0, 0, line);
                break;
            case '|':
                if (match(&lx, '|')) { push_token(&lx, TOK_OR, NULL, 0, 0, 0, line); break; }
                if (match(&lx, '=')) { push_token(&lx, TOK_PIPE_ASSIGN, NULL, 0, 0, 0, line); break; }
                push_token(&lx, TOK_PIPE, NULL, 0, 0, 0, line);
                break;
            case '^':
                if (match(&lx, '=')) push_token(&lx, TOK_CARET_ASSIGN, NULL, 0, 0, 0, line);
                else push_token(&lx, TOK_CARET, NULL, 0, 0, 0, line);
                break;
            case '~':
                push_token(&lx, TOK_TILDE, NULL, 0, 0, 0, line);
                break;
            case '(': push_token(&lx, TOK_LPAREN, NULL, 0, 0, 0, line); break;
            case ')': push_token(&lx, TOK_RPAREN, NULL, 0, 0, 0, line); break;
            case '{': push_token(&lx, TOK_LBRACE, NULL, 0, 0, 0, line); break;
            case '}': push_token(&lx, TOK_RBRACE, NULL, 0, 0, 0, line); break;
            case '[': push_token(&lx, TOK_LBRACKET, NULL, 0, 0, 0, line); break;
            case ']': push_token(&lx, TOK_RBRACKET, NULL, 0, 0, 0, line); break;
            case ',': push_token(&lx, TOK_COMMA, NULL, 0, 0, 0, line); break;
            case ';': push_token(&lx, TOK_SEMI, NULL, 0, 0, 0, line); break;
            case ':': push_token(&lx, TOK_COLON, NULL, 0, 0, 0, line); break;
            case '?': push_token(&lx, TOK_QUESTION, NULL, 0, 0, 0, line); break;
            case '.': push_token(&lx, TOK_DOT, NULL, 0, 0, 0, line); break;
            default:
                diag_set(diag, line, "unexpected character '%c'", c);
                free(lx.tokens);
                return 0;
        }
    }

    push_token(&lx, TOK_EOF, NULL, 0, 0, 0, lx.line);
    out->tokens = lx.tokens;
    out->count = lx.count;
    return 1;
}

void token_list_free(TokenList *list) {
    for (int i = 0; i < list->count; i++) free(list->tokens[i].text);
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_EOF: return "end of file";
        case TOK_INT_LIT: return "integer literal";
        case TOK_FLOAT_LIT: return "float literal";
        case TOK_STRING_LIT: return "string literal";
        case TOK_IDENT: return "identifier";
        case TOK_LET: return "'let'";
        case TOK_FIXED: return "'fixed'";
        case TOK_FR: return "'fr'";
        case TOK_IF: return "'if'";
        case TOK_ELSE: return "'else'";
        case TOK_WHILE: return "'while'";
        case TOK_RETURN: return "'return'";
        case TOK_OUTPUT: return "'output'";
        case TOK_PUSH: return "'push'";
        case TOK_SORT: return "'sort'";
        case TOK_REVERSE: return "'reverse'";
        case TOK_CONTAINS: return "'contains'";
        case TOK_REMOVE: return "'remove'";
        case TOK_KEYS: return "'keys'";
        case TOK_IN: return "'in'";
        case TOK_LEN: return "'len'";
        case TOK_FOR: return "'for'";
        case TOK_BREAK: return "'break'";
        case TOK_CONTINUE: return "'continue'";
        case TOK_STRUCT: return "'struct'";
        case TOK_INVOLVE: return "'involve'";
        case TOK_ENUM: return "'enum'";
        case TOK_TRUE: return "'true'";
        case TOK_FALSE: return "'false'";
        case TOK_TYPE_INT: return "'int'";
        case TOK_TYPE_FLOAT: return "'float'";
        case TOK_TYPE_BOOL: return "'bool'";
        case TOK_TYPE_STRING: return "'string'";
        case TOK_TYPE_LIST: return "'list'";
        case TOK_TYPE_MAP: return "'map'";
        case TOK_PLUS: return "'+'";
        case TOK_MINUS: return "'-'";
        case TOK_STAR: return "'*'";
        case TOK_SLASH: return "'/'";
        case TOK_PERCENT: return "'%'";
        case TOK_ASSIGN: return "'='";
        case TOK_EQ: return "'=='";
        case TOK_BANG: return "'!'";
        case TOK_NE: return "'!='";
        case TOK_LT: return "'<'";
        case TOK_LE: return "'<='";
        case TOK_GT: return "'>'";
        case TOK_GE: return "'>='";
        case TOK_AND: return "'&&'";
        case TOK_OR: return "'||'";
        case TOK_AMP: return "'&'";
        case TOK_PIPE: return "'|'";
        case TOK_CARET: return "'^'";
        case TOK_TILDE: return "'~'";
        case TOK_SHL: return "'<<'";
        case TOK_SHR: return "'>>'";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_COMMA: return "','";
        case TOK_SEMI: return "';'";
        case TOK_COLON: return "':'";
        case TOK_ARROW: return "'->'";
        case TOK_DOT: return "'.'";
        case TOK_QUESTION: return "'?'";
        case TOK_PLUS_ASSIGN: return "'+='";
        case TOK_MINUS_ASSIGN: return "'-='";
        case TOK_STAR_ASSIGN: return "'*='";
        case TOK_SLASH_ASSIGN: return "'/='";
        case TOK_PERCENT_ASSIGN: return "'%='";
        case TOK_PLUS_PLUS: return "'++'";
        case TOK_MINUS_MINUS: return "'--'";
        case TOK_AMP_ASSIGN: return "'&='";
        case TOK_PIPE_ASSIGN: return "'|='";
        case TOK_CARET_ASSIGN: return "'^='";
        case TOK_SHL_ASSIGN: return "'<<='";
        case TOK_SHR_ASSIGN: return "'>>='";
    }
    return "?";
}
