#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

/* Recursive-descent parser. On any syntax error, diag is set and every
 * parse_* function unwinds by returning NULL without further attempts to
 * free partially-built AST fragments -- the process exits right after
 * main() reports the error, so the OS reclaims that memory. */

typedef struct {
    TokenList *tokens;
    int pos;
    Diag *diag;
    PtrList *struct_names; /* borrowed; names of every `struct X` in the file, pre-scanned */
    PtrList *enum_names;   /* borrowed; names of every `enum X` in the file, pre-scanned */
} Parser;

static int is_struct_name(Parser *p, const char *name) {
    for (int i = 0; i < p->struct_names->count; i++) {
        if (strcmp((char *)p->struct_names->items[i], name) == 0) return 1;
    }
    return 0;
}

static int struct_index_of(Parser *p, const char *name) {
    for (int i = 0; i < p->struct_names->count; i++) {
        if (strcmp((char *)p->struct_names->items[i], name) == 0) return i;
    }
    return -1;
}

static int is_enum_name(Parser *p, const char *name) {
    for (int i = 0; i < p->enum_names->count; i++) {
        if (strcmp((char *)p->enum_names->items[i], name) == 0) return 1;
    }
    return 0;
}

static Token *cur(Parser *p) { return &p->tokens->tokens[p->pos]; }
static Token *previous(Parser *p) { return &p->tokens->tokens[p->pos - 1]; }
static int check(Parser *p, TokenType t) { return cur(p)->type == t; }

/* Peeks `offset` tokens ahead of pos, clamped to the trailing TOK_EOF rather
 * than reading past the end of the array -- offset 1 is always safe since
 * TOK_EOF is a guaranteed sentinel, but offset 2+ (used by foreach-loop
 * lookahead) isn't, on input that ends right at the tokens being peeked past. */
static TokenType peek_type(Parser *p, int offset) {
    int i = p->pos + offset;
    if (i >= p->tokens->count) i = p->tokens->count - 1;
    return p->tokens->tokens[i].type;
}

static Token *advance_tok(Parser *p) {
    if (!check(p, TOK_EOF)) p->pos++;
    return previous(p);
}

static int match_tok(Parser *p, TokenType t) {
    if (!check(p, t)) return 0;
    advance_tok(p);
    return 1;
}

static Token *expect(Parser *p, TokenType t, const char *what) {
    if (check(p, t)) return advance_tok(p);
    diag_set(p->diag, cur(p)->line, "expected %s but found %s", what,
             token_type_name(cur(p)->type));
    return NULL;
}

static Expr *parse_expr(Parser *p);
static Stmt *parse_statement(Parser *p);
static Stmt *parse_block(Parser *p);

/* ---- expressions (precedence climbing) ---- */

static Expr **parse_call_args(Parser *p, int *out_count) {
    PtrList args;
    ptrlist_init(&args);
    if (!check(p, TOK_RPAREN)) {
        do {
            Expr *arg = parse_expr(p);
            if (arg == NULL || p->diag->has_error) return NULL;
            ptrlist_push(&args, arg);
        } while (match_tok(p, TOK_COMMA));
    }
    if (expect(p, TOK_RPAREN, "')' after arguments") == NULL) return NULL;
    *out_count = args.count;
    return (Expr **)args.items;
}

static Expr *parse_primary(Parser *p) {
    Token *t = cur(p);
    switch (t->type) {
        case TOK_INT_LIT:
            advance_tok(p);
            return expr_new_int(t->int_val, t->line);
        case TOK_FLOAT_LIT:
            advance_tok(p);
            return expr_new_float(t->float_val, t->line);
        case TOK_STRING_LIT:
            advance_tok(p);
            return expr_new_string(t->text, t->line);
        case TOK_TRUE:
            advance_tok(p);
            return expr_new_bool(1, t->line);
        case TOK_FALSE:
            advance_tok(p);
            return expr_new_bool(0, t->line);
        case TOK_LPAREN: {
            advance_tok(p);
            Expr *inner = parse_expr(p);
            if (inner == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_RPAREN, "')' after expression") == NULL) return NULL;
            return inner;
        }
        case TOK_IDENT: {
            advance_tok(p);
            if (check(p, TOK_LPAREN)) {
                advance_tok(p); /* '(' */
                int arg_count = 0;
                Expr **args = parse_call_args(p, &arg_count);
                if (p->diag->has_error) return NULL;
                return expr_new_call(t->text, args, arg_count, t->line);
            }
            if (check(p, TOK_DOT) && is_enum_name(p, t->text)) {
                advance_tok(p); /* '.' */
                Token *member = expect(p, TOK_IDENT, "an enum member name");
                if (member == NULL) return NULL;
                return expr_new_enum_member(t->text, member->text, t->line);
            }
            if (check(p, TOK_LBRACE) && is_struct_name(p, t->text)) {
                advance_tok(p); /* '{' */
                PtrList names, values;
                ptrlist_init(&names);
                ptrlist_init(&values);
                if (!check(p, TOK_RBRACE)) {
                    do {
                        Token *fname = expect(p, TOK_IDENT, "a field name");
                        if (fname == NULL) return NULL;
                        if (expect(p, TOK_COLON, "':' after field name") == NULL) return NULL;
                        Expr *val = parse_expr(p);
                        if (val == NULL || p->diag->has_error) return NULL;
                        ptrlist_push(&names, dup_str(fname->text));
                        ptrlist_push(&values, val);
                    } while (match_tok(p, TOK_COMMA));
                }
                if (expect(p, TOK_RBRACE, "'}' after struct fields") == NULL) return NULL;
                return expr_new_struct_lit(t->text, (char **)names.items, (Expr **)values.items,
                                            names.count, t->line);
            }
            return expr_new_var(t->text, t->line);
        }
        case TOK_LBRACKET: {
            advance_tok(p);
            PtrList elems;
            ptrlist_init(&elems);
            if (!check(p, TOK_RBRACKET)) {
                do {
                    Expr *el = parse_expr(p);
                    if (el == NULL || p->diag->has_error) return NULL;
                    ptrlist_push(&elems, el);
                } while (match_tok(p, TOK_COMMA));
            }
            if (expect(p, TOK_RBRACKET, "']' after list elements") == NULL) return NULL;
            return expr_new_list((Expr **)elems.items, elems.count, t->line);
        }
        case TOK_LEN: {
            advance_tok(p);
            if (expect(p, TOK_LPAREN, "'(' after len") == NULL) return NULL;
            Expr *target = parse_expr(p);
            if (target == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_RPAREN, "')' after len argument") == NULL) return NULL;
            return expr_new_len(target, t->line);
        }
        case TOK_KEYS: {
            advance_tok(p);
            if (expect(p, TOK_LPAREN, "'(' after keys") == NULL) return NULL;
            Expr *target = parse_expr(p);
            if (target == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_RPAREN, "')' after keys argument") == NULL) return NULL;
            return expr_new_keys(target, t->line);
        }
        case TOK_CONTAINS: {
            advance_tok(p);
            if (expect(p, TOK_LPAREN, "'(' after contains") == NULL) return NULL;
            Expr *list = parse_expr(p);
            if (list == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_COMMA, "',' after contains list argument") == NULL) return NULL;
            Expr *value = parse_expr(p);
            if (value == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_RPAREN, "')' after contains argument") == NULL) return NULL;
            return expr_new_contains(list, value, t->line);
        }
        case TOK_LBRACE: {
            advance_tok(p);
            PtrList keys, values;
            ptrlist_init(&keys);
            ptrlist_init(&values);
            if (!check(p, TOK_RBRACE)) {
                do {
                    Expr *key = parse_expr(p);
                    if (key == NULL || p->diag->has_error) return NULL;
                    if (expect(p, TOK_COLON, "':' after map key") == NULL) return NULL;
                    Expr *value = parse_expr(p);
                    if (value == NULL || p->diag->has_error) return NULL;
                    ptrlist_push(&keys, key);
                    ptrlist_push(&values, value);
                } while (match_tok(p, TOK_COMMA));
            }
            if (expect(p, TOK_RBRACE, "'}' after map entries") == NULL) return NULL;
            return expr_new_map((Expr **)keys.items, (Expr **)values.items, keys.count, t->line);
        }
        default:
            diag_set(p->diag, t->line, "expected an expression but found %s",
                     token_type_name(t->type));
            return NULL;
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *base = parse_primary(p);
    if (base == NULL || p->diag->has_error) return NULL;
    for (;;) {
        if (check(p, TOK_LBRACKET)) {
            int line = cur(p)->line;
            advance_tok(p); /* '[' */
            Expr *idx = parse_expr(p);
            if (idx == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_RBRACKET, "']' after index") == NULL) return NULL;
            base = expr_new_index(base, idx, line);
        } else if (check(p, TOK_DOT)) {
            int line = cur(p)->line;
            advance_tok(p); /* '.' */
            Token *field = expect(p, TOK_IDENT, "a field name");
            if (field == NULL) return NULL;
            if (check(p, TOK_LPAREN)) {
                /* `recv.name(args)` -- no separate method-declaration syntax:
                 * this is sugar for `name(recv, args)`, so any ordinary `fr`
                 * function can be called this way as long as its first
                 * parameter's type matches recv's. Fractyne has no
                 * function-valued fields to call through, so seeing '(' right
                 * after a dotted name is never ambiguous with a field read. */
                advance_tok(p); /* '(' */
                int arg_count = 0;
                Expr **args = parse_call_args(p, &arg_count);
                if (p->diag->has_error) return NULL;
                Expr **all_args = malloc((size_t)(arg_count + 1) * sizeof(Expr *));
                all_args[0] = base;
                for (int i = 0; i < arg_count; i++) all_args[i + 1] = args[i];
                free(args);
                base = expr_new_call(field->text, all_args, arg_count + 1, line);
            } else {
                base = expr_new_field(base, field->text, line);
            }
        } else {
            return base;
        }
    }
}

static Expr *parse_unary(Parser *p) {
    if (check(p, TOK_BANG) || check(p, TOK_MINUS) || check(p, TOK_TILDE)) {
        Token *op = advance_tok(p);
        Expr *operand = parse_unary(p);
        if (operand == NULL || p->diag->has_error) return NULL;
        return expr_new_unary(op->type, operand, op->line);
    }
    return parse_postfix(p);
}

static Expr *parse_factor(Parser *p) {
    Expr *left = parse_unary(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        Token *op = advance_tok(p);
        Expr *right = parse_unary(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_term(Parser *p) {
    Expr *left = parse_factor(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token *op = advance_tok(p);
        Expr *right = parse_factor(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

/* Shift sits between relational and additive, same as C: `a < b << c` groups
 * as `a < (b << c)`, `b << c + d` groups as `b << (c + d)`. */
static Expr *parse_shift(Parser *p) {
    Expr *left = parse_term(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_SHL) || check(p, TOK_SHR)) {
        Token *op = advance_tok(p);
        Expr *right = parse_term(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_comparison(Parser *p) {
    Expr *left = parse_shift(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_LT) || check(p, TOK_LE) || check(p, TOK_GT) || check(p, TOK_GE)) {
        Token *op = advance_tok(p);
        Expr *right = parse_shift(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_equality(Parser *p) {
    Expr *left = parse_comparison(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_EQ) || check(p, TOK_NE)) {
        Token *op = advance_tok(p);
        Expr *right = parse_comparison(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

/* Bitwise `&`/`^`/`|` sit between equality and logical `&&`, same as C, each
 * its own precedence level so `a & b | c ^ d` groups the way C programmers
 * expect without parens: `(a & b) | (c ^ d)`. */
static Expr *parse_bitand(Parser *p) {
    Expr *left = parse_equality(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_AMP)) {
        Token *op = advance_tok(p);
        Expr *right = parse_equality(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_bitxor(Parser *p) {
    Expr *left = parse_bitand(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_CARET)) {
        Token *op = advance_tok(p);
        Expr *right = parse_bitand(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_bitor(Parser *p) {
    Expr *left = parse_bitxor(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_PIPE)) {
        Token *op = advance_tok(p);
        Expr *right = parse_bitxor(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_and(Parser *p) {
    Expr *left = parse_bitor(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_AND)) {
        Token *op = advance_tok(p);
        Expr *right = parse_bitor(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

static Expr *parse_or(Parser *p) {
    Expr *left = parse_and(p);
    if (left == NULL || p->diag->has_error) return NULL;
    while (check(p, TOK_OR)) {
        Token *op = advance_tok(p);
        Expr *right = parse_and(p);
        if (right == NULL || p->diag->has_error) return NULL;
        left = expr_new_binary(op->type, left, right, op->line);
    }
    return left;
}

/* `cond ? then : else` -- lowest precedence, right-associative (the branches
 * recurse into parse_ternary rather than parse_or, so `a ? b : c ? d : e`
 * groups as `a ? b : (c ? d : e)`). */
static Expr *parse_ternary(Parser *p) {
    Expr *cond = parse_or(p);
    if (cond == NULL || p->diag->has_error) return NULL;
    if (!match_tok(p, TOK_QUESTION)) return cond;
    int line = previous(p)->line;
    Expr *then_val = parse_ternary(p);
    if (then_val == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_COLON, "':' in ternary expression") == NULL) return NULL;
    Expr *else_val = parse_ternary(p);
    if (else_val == NULL || p->diag->has_error) return NULL;
    return expr_new_ternary(cond, then_val, else_val, line);
}

static Expr *parse_expr(Parser *p) { return parse_ternary(p); }

/* ---- statements ---- */

static Stmt *parse_let(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'let' */
    Token *name = expect(p, TOK_IDENT, "a variable name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_ASSIGN, "'=' after variable name") == NULL) return NULL;
    Expr *init = parse_expr(p);
    if (init == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_SEMI, "';' after variable declaration") == NULL) return NULL;
    return stmt_new_let(name->text, init, line);
}

static Stmt *parse_output(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'output' */
    if (expect(p, TOK_LPAREN, "'(' after output") == NULL) return NULL;
    Expr *value = parse_expr(p);
    if (value == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after output argument") == NULL) return NULL;
    if (expect(p, TOK_SEMI, "';' after output statement") == NULL) return NULL;
    return stmt_new_output(value, line);
}

static Stmt *parse_push(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'push' */
    if (expect(p, TOK_LPAREN, "'(' after push") == NULL) return NULL;
    Token *name = expect(p, TOK_IDENT, "a list variable name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_COMMA, "',' after list name") == NULL) return NULL;
    Expr *value = parse_expr(p);
    if (value == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after push argument") == NULL) return NULL;
    if (expect(p, TOK_SEMI, "';' after push statement") == NULL) return NULL;
    return stmt_new_push(name->text, value, line);
}

static Stmt *parse_sort(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'sort' */
    if (expect(p, TOK_LPAREN, "'(' after sort") == NULL) return NULL;
    Token *name = expect(p, TOK_IDENT, "a list variable name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_RPAREN, "')' after sort argument") == NULL) return NULL;
    if (expect(p, TOK_SEMI, "';' after sort statement") == NULL) return NULL;
    return stmt_new_sort(name->text, line);
}

static Stmt *parse_reverse(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'reverse' */
    if (expect(p, TOK_LPAREN, "'(' after reverse") == NULL) return NULL;
    Token *name = expect(p, TOK_IDENT, "a list variable name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_RPAREN, "')' after reverse argument") == NULL) return NULL;
    if (expect(p, TOK_SEMI, "';' after reverse statement") == NULL) return NULL;
    return stmt_new_reverse(name->text, line);
}

static Stmt *parse_remove(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'remove' */
    if (expect(p, TOK_LPAREN, "'(' after remove") == NULL) return NULL;
    Token *name = expect(p, TOK_IDENT, "a list variable name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_COMMA, "',' after list name") == NULL) return NULL;
    Expr *index = parse_expr(p);
    if (index == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after remove argument") == NULL) return NULL;
    if (expect(p, TOK_SEMI, "';' after remove statement") == NULL) return NULL;
    return stmt_new_remove(name->text, index, line);
}

static Stmt *parse_if(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'if' */
    if (expect(p, TOK_LPAREN, "'(' after if") == NULL) return NULL;
    Expr *cond = parse_expr(p);
    if (cond == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after if condition") == NULL) return NULL;
    Stmt *then_branch = parse_block(p);
    if (then_branch == NULL || p->diag->has_error) return NULL;
    Stmt *else_branch = NULL;
    if (match_tok(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) {
            else_branch = parse_if(p);
        } else {
            else_branch = parse_block(p);
        }
        if (else_branch == NULL || p->diag->has_error) return NULL;
    }
    return stmt_new_if(cond, then_branch, else_branch, line);
}

static Stmt *parse_while(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'while' */
    if (expect(p, TOK_LPAREN, "'(' after while") == NULL) return NULL;
    Expr *cond = parse_expr(p);
    if (cond == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after while condition") == NULL) return NULL;
    Stmt *body = parse_block(p);
    if (body == NULL || p->diag->has_error) return NULL;
    return stmt_new_while(cond, body, line);
}

/* Maps a compound-assign token to the binary operator it desugars to, or
 * TOK_EOF for plain '=' (nothing to desugar). */
static TokenType compound_assign_op(TokenType t) {
    switch (t) {
        case TOK_PLUS_ASSIGN: return TOK_PLUS;
        case TOK_MINUS_ASSIGN: return TOK_MINUS;
        case TOK_STAR_ASSIGN: return TOK_STAR;
        case TOK_SLASH_ASSIGN: return TOK_SLASH;
        case TOK_PERCENT_ASSIGN: return TOK_PERCENT;
        default: return TOK_EOF;
    }
}

static int is_assign_token(TokenType t) {
    return t == TOK_ASSIGN || compound_assign_op(t) != TOK_EOF;
}

/* `name = expr` or `name += expr` (etc.), optionally followed by ';' --
 * shared by plain assignment statements and a for-loop's init/step clauses.
 * `x += e` desugars to `x = x + e` right here in the parser, so sema and
 * codegen never need to know compound assignment exists. */
static Stmt *parse_assign_stmt(Parser *p, int consume_semi) {
    int line = cur(p)->line;
    Token *name = expect(p, TOK_IDENT, "a variable name");
    if (name == NULL) return NULL;
    TokenType op = compound_assign_op(cur(p)->type);
    if (!is_assign_token(cur(p)->type)) {
        diag_set(p->diag, cur(p)->line, "expected '=' after variable name but found %s",
                 token_type_name(cur(p)->type));
        return NULL;
    }
    advance_tok(p); /* '=' or the compound-assign token */
    Expr *rhs = parse_expr(p);
    if (rhs == NULL || p->diag->has_error) return NULL;
    if (consume_semi && expect(p, TOK_SEMI, "';' after assignment") == NULL) return NULL;
    Expr *value = (op == TOK_EOF) ? rhs : expr_new_binary(op, expr_new_var(name->text, line), rhs, line);
    return stmt_new_assign(name->text, value, line);
}

/* `name++` / `name--`, optionally followed by ';' -- statement-only sugar
 * (unlike C, there's no expression form, so no pre/post-increment value
 * question to answer) for `name = name + 1` / `name = name - 1`. Shared by
 * plain statements and a for-loop's step clause, same as parse_assign_stmt. */
static Stmt *parse_incdec_stmt(Parser *p, int consume_semi) {
    int line = cur(p)->line;
    Token *name = expect(p, TOK_IDENT, "a variable name");
    if (name == NULL) return NULL;
    if (!check(p, TOK_PLUS_PLUS) && !check(p, TOK_MINUS_MINUS)) {
        diag_set(p->diag, cur(p)->line, "expected '++' or '--' but found %s",
                 token_type_name(cur(p)->type));
        return NULL;
    }
    TokenType op = check(p, TOK_PLUS_PLUS) ? TOK_PLUS : TOK_MINUS;
    advance_tok(p);
    if (consume_semi && expect(p, TOK_SEMI, "';' after increment/decrement") == NULL) return NULL;
    Expr *value = expr_new_binary(op, expr_new_var(name->text, line), expr_new_int(1, line), line);
    return stmt_new_assign(name->text, value, line);
}

/* `for (let x in xs) { BODY }` is sugar, desugared here into the equivalent
 * index-based for loop so sema/codegen never need to know it exists:
 *
 *   {
 *       let __fy_feN_src = xs;
 *       for (let __fy_feN_idx = 0; __fy_feN_idx < len(__fy_feN_src); __fy_feN_idx = __fy_feN_idx + 1) {
 *           let x = __fy_feN_src[__fy_feN_idx];
 *           BODY
 *       }
 *   }
 *
 * A per-parse counter keeps the synthetic names unique across nested/sibling
 * for-in loops in the same file. Works on anything len()/[i] already work
 * on -- any list, or a string (iterating its characters). */
static int g_foreach_id = 0;

static Stmt *parse_foreach(Parser *p, int line) {
    advance_tok(p); /* 'let' */
    Token *name = expect(p, TOK_IDENT, "a loop variable name");
    if (name == NULL) return NULL;
    advance_tok(p); /* 'in' */
    Expr *source = parse_expr(p);
    if (source == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after for-in source") == NULL) return NULL;
    Stmt *body = parse_block(p);
    if (body == NULL || p->diag->has_error) return NULL;

    char src_name[32], idx_name[32];
    snprintf(src_name, sizeof(src_name), "__fy_fe%d_src", g_foreach_id);
    snprintf(idx_name, sizeof(idx_name), "__fy_fe%d_idx", g_foreach_id);
    g_foreach_id++;

    Stmt *src_let = stmt_new_let(src_name, source, line);

    Expr *cond = expr_new_binary(TOK_LT, expr_new_var(idx_name, line),
                                  expr_new_len(expr_new_var(src_name, line), line), line);
    Expr *step_rhs = expr_new_binary(TOK_PLUS, expr_new_var(idx_name, line),
                                      expr_new_int(1, line), line);
    Stmt *step = stmt_new_assign(idx_name, step_rhs, line);
    Stmt *idx_let = stmt_new_let(idx_name, expr_new_int(0, line), line);

    Expr *elem_expr = expr_new_index(expr_new_var(src_name, line), expr_new_var(idx_name, line), line);
    Stmt *elem_let = stmt_new_let(name->text, elem_expr, line);

    Stmt **stmts = malloc((size_t)(body->as.block.count + 1) * sizeof(Stmt *));
    stmts[0] = elem_let;
    for (int i = 0; i < body->as.block.count; i++) stmts[i + 1] = body->as.block.stmts[i];
    free(body->as.block.stmts);
    body->as.block.stmts = stmts;
    body->as.block.count++;

    Stmt *for_stmt = stmt_new_for(idx_let, cond, step, body, line);

    Stmt **outer = malloc(2 * sizeof(Stmt *));
    outer[0] = src_let;
    outer[1] = for_stmt;
    return stmt_new_block(outer, 2, line);
}

static Stmt *parse_for(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'for' */
    if (expect(p, TOK_LPAREN, "'(' after for") == NULL) return NULL;
    if (check(p, TOK_LET) && peek_type(p, 1) == TOK_IDENT && peek_type(p, 2) == TOK_IN) {
        return parse_foreach(p, line);
    }
    Stmt *init = check(p, TOK_LET) ? parse_let(p) : parse_assign_stmt(p, 1);
    if (init == NULL || p->diag->has_error) return NULL;
    Expr *cond = parse_expr(p);
    if (cond == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_SEMI, "';' after for condition") == NULL) return NULL;
    Stmt *step = (peek_type(p, 1) == TOK_PLUS_PLUS || peek_type(p, 1) == TOK_MINUS_MINUS)
                     ? parse_incdec_stmt(p, 0)
                     : parse_assign_stmt(p, 0);
    if (step == NULL || p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after for clauses") == NULL) return NULL;
    Stmt *body = parse_block(p);
    if (body == NULL || p->diag->has_error) return NULL;
    return stmt_new_for(init, cond, step, body, line);
}

static Stmt *parse_return(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'return' */
    Expr *value = NULL;
    if (!check(p, TOK_SEMI)) {
        value = parse_expr(p);
        if (value == NULL || p->diag->has_error) return NULL;
    }
    if (expect(p, TOK_SEMI, "';' after return statement") == NULL) return NULL;
    return stmt_new_return(value, line);
}

static Stmt *parse_statement(Parser *p) {
    switch (cur(p)->type) {
        case TOK_LET: return parse_let(p);
        case TOK_OUTPUT: return parse_output(p);
        case TOK_PUSH: return parse_push(p);
        case TOK_SORT: return parse_sort(p);
        case TOK_REVERSE: return parse_reverse(p);
        case TOK_REMOVE: return parse_remove(p);
        case TOK_IF: return parse_if(p);
        case TOK_WHILE: return parse_while(p);
        case TOK_FOR: return parse_for(p);
        case TOK_BREAK: {
            int line = cur(p)->line;
            advance_tok(p);
            if (expect(p, TOK_SEMI, "';' after break") == NULL) return NULL;
            return stmt_new_break(line);
        }
        case TOK_CONTINUE: {
            int line = cur(p)->line;
            advance_tok(p);
            if (expect(p, TOK_SEMI, "';' after continue") == NULL) return NULL;
            return stmt_new_continue(line);
        }
        case TOK_RETURN: return parse_return(p);
        case TOK_LBRACE: return parse_block(p);
        case TOK_IDENT: {
            TokenType next = p->tokens->tokens[p->pos + 1].type;
            if (is_assign_token(next)) return parse_assign_stmt(p, 1);
            if (next == TOK_PLUS_PLUS || next == TOK_MINUS_MINUS) return parse_incdec_stmt(p, 1);
            if (next == TOK_DOT) {
                /* `a.b.c = v;` -- an arbitrary chain of field reads (each
                 * wrapped as an EXPR_FIELD base) ending in one field to
                 * actually assign into. Look one field ahead at a time: as
                 * long as another '.' follows, the field just read is a
                 * read, not the assignment target. */
                int line = cur(p)->line;
                Token *name = advance_tok(p);
                Expr *base = expr_new_var(name->text, line);
                advance_tok(p); /* '.' */
                Token *field = expect(p, TOK_IDENT, "a field name");
                if (field == NULL) { expr_free(base); return NULL; }
                char *field_name = dup_str(field->text);
                while (check(p, TOK_DOT)) {
                    base = expr_new_field(base, field_name, line);
                    free(field_name);
                    advance_tok(p); /* '.' */
                    Token *next_field = expect(p, TOK_IDENT, "a field name");
                    if (next_field == NULL) { expr_free(base); return NULL; }
                    field_name = dup_str(next_field->text);
                }
                if (expect(p, TOK_ASSIGN, "'=' after field") == NULL) {
                    expr_free(base);
                    free(field_name);
                    return NULL;
                }
                Expr *value = parse_expr(p);
                if (value == NULL || p->diag->has_error) {
                    expr_free(base);
                    free(field_name);
                    return NULL;
                }
                if (expect(p, TOK_SEMI, "';' after assignment") == NULL) {
                    expr_free(base);
                    free(field_name);
                    return NULL;
                }
                Stmt *result = stmt_new_field_assign(base, field_name, value, line);
                free(field_name);
                return result;
            }
            if (next == TOK_LBRACKET) {
                int line = cur(p)->line;
                Token *name = advance_tok(p);
                advance_tok(p); /* '[' */
                Expr *idx = parse_expr(p);
                if (idx == NULL || p->diag->has_error) return NULL;
                if (expect(p, TOK_RBRACKET, "']' after index") == NULL) return NULL;
                if (expect(p, TOK_ASSIGN, "'=' after indexed assignment target") == NULL) return NULL;
                Expr *value = parse_expr(p);
                if (value == NULL || p->diag->has_error) return NULL;
                if (expect(p, TOK_SEMI, "';' after assignment") == NULL) return NULL;
                return stmt_new_index_assign(name->text, idx, value, line);
            }
            /* fall through to expression statement (e.g. a call for its side effects) */
        }
        /* fallthrough */
        default: {
            int line = cur(p)->line;
            Expr *e = parse_expr(p);
            if (e == NULL || p->diag->has_error) return NULL;
            if (expect(p, TOK_SEMI, "';' after expression") == NULL) return NULL;
            return stmt_new_expr(e, line);
        }
    }
}

static Stmt *parse_block(Parser *p) {
    int line = cur(p)->line;
    if (expect(p, TOK_LBRACE, "'{' to start a block") == NULL) return NULL;
    PtrList stmts;
    ptrlist_init(&stmts);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Stmt *s = parse_statement(p);
        if (s == NULL || p->diag->has_error) return NULL;
        ptrlist_push(&stmts, s);
    }
    if (expect(p, TOK_RBRACE, "'}' to close a block") == NULL) return NULL;
    return stmt_new_block((Stmt **)stmts.items, stmts.count, line);
}

/* ---- top level ---- */

static int parse_primitive_type(Parser *p, Type *out) {
    switch (cur(p)->type) {
        case TOK_TYPE_INT: *out = TYPE_INT; break;
        case TOK_TYPE_FLOAT: *out = TYPE_FLOAT; break;
        case TOK_TYPE_BOOL: *out = TYPE_BOOL; break;
        case TOK_TYPE_STRING: *out = TYPE_STRING; break;
        default:
            diag_set(p->diag, cur(p)->line, "expected int, float, bool, or string but found %s",
                     token_type_name(cur(p)->type));
            return 0;
    }
    advance_tok(p);
    return 1;
}

/* Like parse_primitive_type, but also accepts a struct name -- used for list
 * element types, since list<SomeStruct> is allowed (map value types are not;
 * see parse_type_token's map branch). */
static int parse_element_type(Parser *p, Type *out) {
    if (check(p, TOK_IDENT) && is_struct_name(p, cur(p)->text)) {
        *out = TYPE_STRUCT_BASE + struct_index_of(p, cur(p)->text);
        advance_tok(p);
        return 1;
    }
    return parse_primitive_type(p, out);
}

/* Enum names are only accepted as a bare type (function params/returns,
 * struct fields, `let`-inferred types come from expressions instead) --
 * unlike structs, there's no list<SomeEnum> yet, so this isn't threaded
 * into parse_element_type. */
static int parse_type_token_or_enum(Parser *p, Type *out) {
    if (check(p, TOK_IDENT) && is_enum_name(p, cur(p)->text)) {
        for (int i = 0; i < p->enum_names->count; i++) {
            if (strcmp((char *)p->enum_names->items[i], cur(p)->text) == 0) {
                *out = TYPE_ENUM_BASE + i;
                advance_tok(p);
                return 1;
            }
        }
    }
    return 0;
}

static int parse_type_token(Parser *p, Type *out) {
    if (check(p, TOK_TYPE_LIST)) {
        advance_tok(p);
        if (expect(p, TOK_LT, "'<' after 'list'") == NULL) return 0;
        Type elem;
        if (!parse_element_type(p, &elem)) return 0;
        if (expect(p, TOK_GT, "'>' after list element type") == NULL) return 0;
        *out = list_of(elem);
        return 1;
    }
    if (check(p, TOK_TYPE_MAP)) {
        advance_tok(p);
        if (expect(p, TOK_LT, "'<' after 'map'") == NULL) return 0;
        if (expect(p, TOK_TYPE_STRING, "'string' (map keys must be string)") == NULL) return 0;
        if (expect(p, TOK_COMMA, "',' after map key type") == NULL) return 0;
        Type value;
        if (!parse_primitive_type(p, &value)) return 0;
        if (expect(p, TOK_GT, "'>' after map value type") == NULL) return 0;
        *out = map_of(value);
        return 1;
    }
    if (check(p, TOK_IDENT) && is_struct_name(p, cur(p)->text)) {
        *out = TYPE_STRUCT_BASE + struct_index_of(p, cur(p)->text);
        advance_tok(p);
        return 1;
    }
    if (parse_type_token_or_enum(p, out)) return 1;
    return parse_primitive_type(p, out);
}

/* `name: type, name: type, ...` up to (not including) `terminator` -- shared
 * by function parameter lists and struct field lists. On error, returns NULL
 * with p->diag->has_error set; check that rather than the return value,
 * since a legitimately empty list also returns a non-NULL zero-length array. */
static Param *parse_typed_field_list(Parser *p, TokenType terminator, int *out_count) {
    PtrList fields;
    ptrlist_init(&fields);
    if (!check(p, terminator)) {
        do {
            Token *fname = expect(p, TOK_IDENT, "a name");
            if (fname == NULL) return NULL;
            if (expect(p, TOK_COLON, "':' after name") == NULL) return NULL;
            Type ftype;
            if (!parse_type_token(p, &ftype)) return NULL;
            Param *field = malloc(sizeof(Param));
            field->name = dup_str(fname->text);
            field->type = ftype;
            ptrlist_push(&fields, field);
        } while (match_tok(p, TOK_COMMA));
    }
    *out_count = fields.count;
    Param *flat = malloc((size_t)fields.count * sizeof(Param));
    for (int i = 0; i < fields.count; i++) {
        Param *src = (Param *)fields.items[i];
        flat[i] = *src;
        free(src);
    }
    free(fields.items);
    return flat;
}

static FunctionDecl *parse_function(Parser *p) {
    int line = cur(p)->line;
    advance_tok(p); /* 'fr' */
    Token *name = expect(p, TOK_IDENT, "a function name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_LPAREN, "'(' after function name") == NULL) return NULL;

    int param_count;
    Param *params = parse_typed_field_list(p, TOK_RPAREN, &param_count);
    if (p->diag->has_error) return NULL;
    if (expect(p, TOK_RPAREN, "')' after parameters") == NULL) return NULL;

    Type return_type = TYPE_VOID;
    if (match_tok(p, TOK_ARROW)) {
        if (!parse_type_token(p, &return_type)) return NULL;
    }

    Stmt *body = parse_block(p);
    if (body == NULL || p->diag->has_error) return NULL;

    return function_decl_new(name->text, params, param_count, return_type, body, line);
}

static StructDecl *parse_struct_decl(Parser *p) {
    advance_tok(p); /* 'struct' */
    Token *name = expect(p, TOK_IDENT, "a struct name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_LBRACE, "'{' after struct name") == NULL) return NULL;
    int field_count;
    Param *fields = parse_typed_field_list(p, TOK_RBRACE, &field_count);
    if (p->diag->has_error) return NULL;
    if (expect(p, TOK_RBRACE, "'}' after struct fields") == NULL) return NULL;
    return struct_decl_new(name->text, fields, field_count);
}

/* Struct declarations can be referenced by name before or after their own
 * textual position (a function can take a struct type declared later in the
 * file, a struct can embed one declared earlier), so struct names are
 * collected in one lightweight pass before real parsing begins. "struct" is
 * a reserved keyword, so any TOK_STRUCT is genuinely a struct declaration --
 * this pass doesn't need to fully parse it, just record the name that
 * follows and let the real parser validate the rest. */
void collect_struct_names(TokenList *tokens, PtrList *names) {
    for (int i = 0; i + 1 < tokens->count; i++) {
        if (tokens->tokens[i].type == TOK_STRUCT && tokens->tokens[i + 1].type == TOK_IDENT) {
            ptrlist_push(names, tokens->tokens[i + 1].text);
        }
    }
}

/* Same reasoning as collect_struct_names, for `enum Name { ... }`: needed
 * before real parsing so `Name.Member` can be recognized as enum-member
 * access rather than a field access on an undeclared variable. */
void collect_enum_names(TokenList *tokens, PtrList *names) {
    for (int i = 0; i + 1 < tokens->count; i++) {
        if (tokens->tokens[i].type == TOK_ENUM && tokens->tokens[i + 1].type == TOK_IDENT) {
            ptrlist_push(names, tokens->tokens[i + 1].text);
        }
    }
}

static EnumDecl *parse_enum_decl(Parser *p) {
    advance_tok(p); /* 'enum' */
    Token *name = expect(p, TOK_IDENT, "an enum name");
    if (name == NULL) return NULL;
    if (expect(p, TOK_LBRACE, "'{' after enum name") == NULL) return NULL;
    PtrList members;
    ptrlist_init(&members);
    if (!check(p, TOK_RBRACE)) {
        do {
            Token *member = expect(p, TOK_IDENT, "an enum member name");
            if (member == NULL) return NULL;
            ptrlist_push(&members, dup_str(member->text));
        } while (match_tok(p, TOK_COMMA));
    }
    if (expect(p, TOK_RBRACE, "'}' after enum members") == NULL) return NULL;
    return enum_decl_new(name->text, (char **)members.items, members.count);
}

/* Parses one file's top-level declarations into prog, using a struct-name
 * list the caller already built -- when `involve` pulls in more than one
 * file, that list covers all of them, collected up front (see involve.c),
 * so a struct can be referenced regardless of which file declares it. Any
 * `involve "...";` here is only checked for syntax: resolving and loading
 * the file it names already happened before this function runs. */
int parse_file_into_program(TokenList *tokens, PtrList *struct_names, PtrList *enum_names,
                             Program *prog, Diag *diag) {
    Parser p = {tokens, 0, diag, struct_names, enum_names};
    while (!check(&p, TOK_EOF)) {
        if (check(&p, TOK_INVOLVE)) {
            advance_tok(&p); /* 'involve' */
            if (expect(&p, TOK_STRING_LIT, "a file path") == NULL) return 0;
            if (expect(&p, TOK_SEMI, "';' after involve") == NULL) return 0;
            continue;
        }
        if (check(&p, TOK_STRUCT)) {
            StructDecl *s = parse_struct_decl(&p);
            if (s == NULL || diag->has_error) return 0;
            program_add_struct(prog, s);
            continue;
        }
        if (check(&p, TOK_ENUM)) {
            EnumDecl *e = parse_enum_decl(&p);
            if (e == NULL || diag->has_error) return 0;
            program_add_enum(prog, e);
            continue;
        }
        if (check(&p, TOK_LET)) {
            Stmt *g = parse_let(&p);
            if (g == NULL || diag->has_error) return 0;
            program_add_global(prog, g);
            continue;
        }
        if (!check(&p, TOK_FR)) {
            diag_set(diag, cur(&p)->line,
                     "expected a function, struct, enum, involve, or global declaration but found %s",
                     token_type_name(cur(&p)->type));
            return 0;
        }
        FunctionDecl *f = parse_function(&p);
        if (f == NULL || diag->has_error) return 0;
        program_add_function(prog, f);
    }
    return 1;
}
