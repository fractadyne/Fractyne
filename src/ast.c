#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Program *g_program = NULL;

void type_system_set_program(const Program *prog) { g_program = prog; }

int type_is_struct(Type t) { return t >= TYPE_STRUCT_BASE && t < TYPE_LIST_STRUCT_BASE; }
int type_is_list_of_struct(Type t) { return t >= TYPE_LIST_STRUCT_BASE && t < TYPE_ENUM_BASE; }
int type_is_enum(Type t) { return t >= TYPE_ENUM_BASE; }

const StructDecl *struct_decl_for(Type t) {
    if (!type_is_struct(t) || g_program == NULL) return NULL;
    int index = t - TYPE_STRUCT_BASE;
    if (index < 0 || index >= g_program->struct_count) return NULL;
    return g_program->structs[index];
}

const EnumDecl *enum_decl_for(Type t) {
    if (!type_is_enum(t) || g_program == NULL) return NULL;
    int index = t - TYPE_ENUM_BASE;
    if (index < 0 || index >= g_program->enum_count) return NULL;
    return g_program->enums[index];
}

/* type_name() must stay safe to call twice in one diagnostic's format string
 * (many call sites do, e.g. "cannot assign %s to %s"). The fixed cases below
 * return literals or a stable pointer into a struct's own name, which is
 * always safe; the formatted "list<Name>" case below is the only one that
 * needs a scratch buffer, so it rotates through a small pool instead of one
 * shared buffer, to survive being called more than once per format string. */
const char *type_name(Type t) {
    if (type_is_struct(t)) {
        const StructDecl *sd = struct_decl_for(t);
        return sd != NULL ? sd->name : "<unknown struct>";
    }
    if (type_is_enum(t)) {
        const EnumDecl *ed = enum_decl_for(t);
        return ed != NULL ? ed->name : "<unknown enum>";
    }
    if (type_is_list_of_struct(t)) {
        static char bufs[4][160];
        static int slot = 0;
        const StructDecl *sd = struct_decl_for(t - TYPE_LIST_STRUCT_BASE + TYPE_STRUCT_BASE);
        char *buf = bufs[slot];
        slot = (slot + 1) % 4;
        snprintf(buf, sizeof(bufs[0]), "list<%s>", sd != NULL ? sd->name : "<unknown struct>");
        return buf;
    }
    switch (t) {
        case TYPE_VOID: return "void";
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_LIST_INT: return "list<int>";
        case TYPE_LIST_FLOAT: return "list<float>";
        case TYPE_LIST_BOOL: return "list<bool>";
        case TYPE_LIST_STRING: return "list<string>";
        case TYPE_MAP_INT: return "map<string, int>";
        case TYPE_MAP_FLOAT: return "map<string, float>";
        case TYPE_MAP_BOOL: return "map<string, bool>";
        case TYPE_MAP_STRING: return "map<string, string>";
        case TYPE_UNKNOWN: return "<unknown>";
    }
    return "<unknown>";
}

int type_is_list(Type t) {
    return t == TYPE_LIST_INT || t == TYPE_LIST_FLOAT || t == TYPE_LIST_BOOL || t == TYPE_LIST_STRING
           || type_is_list_of_struct(t);
}

Type list_of(Type elem) {
    switch (elem) {
        case TYPE_INT: return TYPE_LIST_INT;
        case TYPE_FLOAT: return TYPE_LIST_FLOAT;
        case TYPE_BOOL: return TYPE_LIST_BOOL;
        case TYPE_STRING: return TYPE_LIST_STRING;
        default:
            if (type_is_struct(elem)) return TYPE_LIST_STRUCT_BASE + (elem - TYPE_STRUCT_BASE);
            return TYPE_UNKNOWN;
    }
}

Type list_elem(Type list) {
    switch (list) {
        case TYPE_LIST_INT: return TYPE_INT;
        case TYPE_LIST_FLOAT: return TYPE_FLOAT;
        case TYPE_LIST_BOOL: return TYPE_BOOL;
        case TYPE_LIST_STRING: return TYPE_STRING;
        default:
            if (type_is_list_of_struct(list)) return list - TYPE_LIST_STRUCT_BASE + TYPE_STRUCT_BASE;
            return TYPE_UNKNOWN;
    }
}

int type_is_map(Type t) {
    return t == TYPE_MAP_INT || t == TYPE_MAP_FLOAT || t == TYPE_MAP_BOOL || t == TYPE_MAP_STRING;
}

Type map_of(Type value) {
    switch (value) {
        case TYPE_INT: return TYPE_MAP_INT;
        case TYPE_FLOAT: return TYPE_MAP_FLOAT;
        case TYPE_BOOL: return TYPE_MAP_BOOL;
        case TYPE_STRING: return TYPE_MAP_STRING;
        default: return TYPE_UNKNOWN;
    }
}

Type map_value(Type map) {
    switch (map) {
        case TYPE_MAP_INT: return TYPE_INT;
        case TYPE_MAP_FLOAT: return TYPE_FLOAT;
        case TYPE_MAP_BOOL: return TYPE_BOOL;
        case TYPE_MAP_STRING: return TYPE_STRING;
        default: return TYPE_UNKNOWN;
    }
}

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

void ptrlist_init(PtrList *l) {
    l->items = NULL;
    l->count = 0;
    l->capacity = 0;
}

void ptrlist_push(PtrList *l, void *item) {
    if (l->count == l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = realloc(l->items, (size_t)l->capacity * sizeof(void *));
    }
    l->items[l->count++] = item;
}

/* ---- Expr ---- */

static Expr *expr_new(ExprKind kind, int line) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = kind;
    e->type = TYPE_UNKNOWN;
    e->line = line;
    return e;
}

Expr *expr_new_int(long v, int line) {
    Expr *e = expr_new(EXPR_INT, line);
    e->as.int_val = v;
    return e;
}

Expr *expr_new_float(double v, int line) {
    Expr *e = expr_new(EXPR_FLOAT, line);
    e->as.float_val = v;
    return e;
}

Expr *expr_new_bool(int v, int line) {
    Expr *e = expr_new(EXPR_BOOL, line);
    e->as.bool_val = v;
    return e;
}

Expr *expr_new_string(const char *raw, int line) {
    Expr *e = expr_new(EXPR_STRING, line);
    e->as.string_val = dup_str(raw);
    return e;
}

Expr *expr_new_var(const char *name, int line) {
    Expr *e = expr_new(EXPR_VAR, line);
    e->as.string_val = dup_str(name);
    return e;
}

Expr *expr_new_unary(TokenType op, Expr *operand, int line) {
    Expr *e = expr_new(EXPR_UNARY, line);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *expr_new_binary(TokenType op, Expr *left, Expr *right, int line) {
    Expr *e = expr_new(EXPR_BINARY, line);
    e->as.binary.op = op;
    e->as.binary.left = left;
    e->as.binary.right = right;
    return e;
}

Expr *expr_new_call(const char *callee, Expr **args, int arg_count, int line) {
    Expr *e = expr_new(EXPR_CALL, line);
    e->as.call.callee = dup_str(callee);
    e->as.call.args = args;
    e->as.call.arg_count = arg_count;
    return e;
}

Expr *expr_new_list(Expr **elements, int count, int line) {
    Expr *e = expr_new(EXPR_LIST, line);
    e->as.list_lit.elements = elements;
    e->as.list_lit.count = count;
    return e;
}

Expr *expr_new_index(Expr *base, Expr *index, int line) {
    Expr *e = expr_new(EXPR_INDEX, line);
    e->as.index.base = base;
    e->as.index.index = index;
    return e;
}

Expr *expr_new_len(Expr *target, int line) {
    Expr *e = expr_new(EXPR_LEN, line);
    e->as.len.target = target;
    return e;
}

Expr *expr_new_map(Expr **keys, Expr **values, int count, int line) {
    Expr *e = expr_new(EXPR_MAP, line);
    e->as.map_lit.keys = keys;
    e->as.map_lit.values = values;
    e->as.map_lit.count = count;
    return e;
}

Expr *expr_new_struct_lit(const char *struct_name, char **field_names, Expr **field_values,
                           int count, int line) {
    Expr *e = expr_new(EXPR_STRUCT_LIT, line);
    e->as.struct_lit.struct_name = dup_str(struct_name);
    e->as.struct_lit.field_names = field_names;
    e->as.struct_lit.field_values = field_values;
    e->as.struct_lit.count = count;
    return e;
}

Expr *expr_new_field(Expr *base, const char *field, int line) {
    Expr *e = expr_new(EXPR_FIELD, line);
    e->as.field.base = base;
    e->as.field.field = dup_str(field);
    return e;
}

Expr *expr_new_ternary(Expr *cond, Expr *then_val, Expr *else_val, int line) {
    Expr *e = expr_new(EXPR_TERNARY, line);
    e->as.ternary.cond = cond;
    e->as.ternary.then_val = then_val;
    e->as.ternary.else_val = else_val;
    return e;
}

Expr *expr_new_contains(Expr *list, Expr *value, int line) {
    Expr *e = expr_new(EXPR_CONTAINS, line);
    e->as.contains.list = list;
    e->as.contains.value = value;
    return e;
}

Expr *expr_new_enum_member(const char *enum_name, const char *member_name, int line) {
    Expr *e = expr_new(EXPR_ENUM_MEMBER, line);
    e->as.enum_member.enum_name = dup_str(enum_name);
    e->as.enum_member.member_name = dup_str(member_name);
    return e;
}

Expr *expr_new_keys(Expr *target, int line) {
    Expr *e = expr_new(EXPR_KEYS, line);
    e->as.keys.target = target;
    return e;
}

void expr_free(Expr *e) {
    if (e == NULL) return;
    switch (e->kind) {
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_BOOL:
            break;
        case EXPR_STRING:
        case EXPR_VAR:
            free(e->as.string_val);
            break;
        case EXPR_UNARY:
            expr_free(e->as.unary.operand);
            break;
        case EXPR_BINARY:
            expr_free(e->as.binary.left);
            expr_free(e->as.binary.right);
            break;
        case EXPR_CALL:
            free(e->as.call.callee);
            for (int i = 0; i < e->as.call.arg_count; i++) expr_free(e->as.call.args[i]);
            free(e->as.call.args);
            break;
        case EXPR_LIST:
            for (int i = 0; i < e->as.list_lit.count; i++) expr_free(e->as.list_lit.elements[i]);
            free(e->as.list_lit.elements);
            break;
        case EXPR_INDEX:
            expr_free(e->as.index.base);
            expr_free(e->as.index.index);
            break;
        case EXPR_LEN:
            expr_free(e->as.len.target);
            break;
        case EXPR_MAP:
            for (int i = 0; i < e->as.map_lit.count; i++) {
                expr_free(e->as.map_lit.keys[i]);
                expr_free(e->as.map_lit.values[i]);
            }
            free(e->as.map_lit.keys);
            free(e->as.map_lit.values);
            break;
        case EXPR_STRUCT_LIT:
            free(e->as.struct_lit.struct_name);
            for (int i = 0; i < e->as.struct_lit.count; i++) {
                free(e->as.struct_lit.field_names[i]);
                expr_free(e->as.struct_lit.field_values[i]);
            }
            free(e->as.struct_lit.field_names);
            free(e->as.struct_lit.field_values);
            break;
        case EXPR_FIELD:
            expr_free(e->as.field.base);
            free(e->as.field.field);
            break;
        case EXPR_TERNARY:
            expr_free(e->as.ternary.cond);
            expr_free(e->as.ternary.then_val);
            expr_free(e->as.ternary.else_val);
            break;
        case EXPR_CONTAINS:
            expr_free(e->as.contains.list);
            expr_free(e->as.contains.value);
            break;
        case EXPR_ENUM_MEMBER:
            free(e->as.enum_member.enum_name);
            free(e->as.enum_member.member_name);
            break;
        case EXPR_KEYS:
            expr_free(e->as.keys.target);
            break;
    }
    free(e);
}

/* ---- Stmt ---- */

static Stmt *stmt_new(StmtKind kind, int line) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

Stmt *stmt_new_let(const char *name, Expr *init, int line) {
    Stmt *s = stmt_new(STMT_LET, line);
    s->as.let_stmt.name = dup_str(name);
    s->as.let_stmt.init = init;
    s->as.let_stmt.resolved_type = TYPE_UNKNOWN;
    return s;
}

Stmt *stmt_new_assign(const char *name, Expr *value, int line) {
    Stmt *s = stmt_new(STMT_ASSIGN, line);
    s->as.assign_stmt.name = dup_str(name);
    s->as.assign_stmt.value = value;
    return s;
}

Stmt *stmt_new_output(Expr **values, int count, int line) {
    Stmt *s = stmt_new(STMT_OUTPUT, line);
    s->as.output_stmt.values = values;
    s->as.output_stmt.count = count;
    return s;
}

Stmt *stmt_new_if(Expr *cond, Stmt *then_branch, Stmt *else_branch, int line) {
    Stmt *s = stmt_new(STMT_IF, line);
    s->as.if_stmt.cond = cond;
    s->as.if_stmt.then_branch = then_branch;
    s->as.if_stmt.else_branch = else_branch;
    return s;
}

Stmt *stmt_new_while(Expr *cond, Stmt *body, int line) {
    Stmt *s = stmt_new(STMT_WHILE, line);
    s->as.while_stmt.cond = cond;
    s->as.while_stmt.body = body;
    return s;
}

Stmt *stmt_new_for(Stmt *init, Expr *cond, Stmt *step, Stmt *body, int line) {
    Stmt *s = stmt_new(STMT_FOR, line);
    s->as.for_stmt.init = init;
    s->as.for_stmt.cond = cond;
    s->as.for_stmt.step = step;
    s->as.for_stmt.body = body;
    return s;
}

Stmt *stmt_new_break(int line) { return stmt_new(STMT_BREAK, line); }
Stmt *stmt_new_continue(int line) { return stmt_new(STMT_CONTINUE, line); }

Stmt *stmt_new_return(Expr *value, int line) {
    Stmt *s = stmt_new(STMT_RETURN, line);
    s->as.return_stmt.value = value;
    return s;
}

Stmt *stmt_new_block(Stmt **stmts, int count, int line) {
    Stmt *s = stmt_new(STMT_BLOCK, line);
    s->as.block.stmts = stmts;
    s->as.block.count = count;
    return s;
}

Stmt *stmt_new_expr(Expr *expr, int line) {
    Stmt *s = stmt_new(STMT_EXPR, line);
    s->as.expr_stmt.expr = expr;
    return s;
}

Stmt *stmt_new_push(const char *name, Expr *value, int line) {
    Stmt *s = stmt_new(STMT_PUSH, line);
    s->as.push_stmt.name = dup_str(name);
    s->as.push_stmt.value = value;
    return s;
}

Stmt *stmt_new_index_assign(const char *name, Expr *index, Expr *value, int line) {
    Stmt *s = stmt_new(STMT_INDEX_ASSIGN, line);
    s->as.index_assign_stmt.name = dup_str(name);
    s->as.index_assign_stmt.index = index;
    s->as.index_assign_stmt.value = value;
    return s;
}

Stmt *stmt_new_field_assign(Expr *base, const char *field, Expr *value, int line) {
    Stmt *s = stmt_new(STMT_FIELD_ASSIGN, line);
    s->as.field_assign_stmt.base = base;
    s->as.field_assign_stmt.field = dup_str(field);
    s->as.field_assign_stmt.value = value;
    return s;
}

Stmt *stmt_new_sort(const char *name, int line) {
    Stmt *s = stmt_new(STMT_SORT, line);
    s->as.sort_stmt.name = dup_str(name);
    s->as.sort_stmt.resolved_type = TYPE_UNKNOWN;
    return s;
}

Stmt *stmt_new_reverse(const char *name, int line) {
    Stmt *s = stmt_new(STMT_REVERSE, line);
    s->as.reverse_stmt.name = dup_str(name);
    s->as.reverse_stmt.resolved_type = TYPE_UNKNOWN;
    return s;
}

Stmt *stmt_new_remove(const char *name, Expr *index, int line) {
    Stmt *s = stmt_new(STMT_REMOVE, line);
    s->as.remove_stmt.name = dup_str(name);
    s->as.remove_stmt.index = index;
    s->as.remove_stmt.resolved_type = TYPE_UNKNOWN;
    return s;
}

void stmt_free(Stmt *s) {
    if (s == NULL) return;
    switch (s->kind) {
        case STMT_LET:
            free(s->as.let_stmt.name);
            expr_free(s->as.let_stmt.init);
            break;
        case STMT_ASSIGN:
            free(s->as.assign_stmt.name);
            expr_free(s->as.assign_stmt.value);
            break;
        case STMT_OUTPUT:
            for (int i = 0; i < s->as.output_stmt.count; i++) expr_free(s->as.output_stmt.values[i]);
            free(s->as.output_stmt.values);
            break;
        case STMT_IF:
            expr_free(s->as.if_stmt.cond);
            stmt_free(s->as.if_stmt.then_branch);
            stmt_free(s->as.if_stmt.else_branch);
            break;
        case STMT_WHILE:
            expr_free(s->as.while_stmt.cond);
            stmt_free(s->as.while_stmt.body);
            break;
        case STMT_FOR:
            stmt_free(s->as.for_stmt.init);
            expr_free(s->as.for_stmt.cond);
            stmt_free(s->as.for_stmt.step);
            stmt_free(s->as.for_stmt.body);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
        case STMT_RETURN:
            expr_free(s->as.return_stmt.value);
            break;
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++) stmt_free(s->as.block.stmts[i]);
            free(s->as.block.stmts);
            break;
        case STMT_EXPR:
            expr_free(s->as.expr_stmt.expr);
            break;
        case STMT_PUSH:
            free(s->as.push_stmt.name);
            expr_free(s->as.push_stmt.value);
            break;
        case STMT_INDEX_ASSIGN:
            free(s->as.index_assign_stmt.name);
            expr_free(s->as.index_assign_stmt.index);
            expr_free(s->as.index_assign_stmt.value);
            break;
        case STMT_FIELD_ASSIGN:
            expr_free(s->as.field_assign_stmt.base);
            free(s->as.field_assign_stmt.field);
            expr_free(s->as.field_assign_stmt.value);
            break;
        case STMT_SORT:
            free(s->as.sort_stmt.name);
            break;
        case STMT_REVERSE:
            free(s->as.reverse_stmt.name);
            break;
        case STMT_REMOVE:
            free(s->as.remove_stmt.name);
            expr_free(s->as.remove_stmt.index);
            break;
    }
    free(s);
}

/* ---- FunctionDecl / Program ---- */

FunctionDecl *function_decl_new(const char *name, Param *params, int param_count,
                                 Type return_type, Stmt *body, int line) {
    FunctionDecl *f = calloc(1, sizeof(FunctionDecl));
    f->name = dup_str(name);
    f->params = params;
    f->param_count = param_count;
    f->return_type = return_type;
    f->body = body;
    f->line = line;
    return f;
}

void function_decl_free(FunctionDecl *f) {
    if (f == NULL) return;
    free(f->name);
    for (int i = 0; i < f->param_count; i++) free(f->params[i].name);
    free(f->params);
    stmt_free(f->body);
    free(f);
}

StructDecl *struct_decl_new(const char *name, Param *fields, int field_count) {
    StructDecl *s = calloc(1, sizeof(StructDecl));
    s->name = dup_str(name);
    s->fields = fields;
    s->field_count = field_count;
    return s;
}

void struct_decl_free(StructDecl *s) {
    if (s == NULL) return;
    free(s->name);
    for (int i = 0; i < s->field_count; i++) free(s->fields[i].name);
    free(s->fields);
    free(s);
}

EnumDecl *enum_decl_new(const char *name, char **members, int member_count) {
    EnumDecl *e = calloc(1, sizeof(EnumDecl));
    e->name = dup_str(name);
    e->members = members;
    e->member_count = member_count;
    return e;
}

void enum_decl_free(EnumDecl *e) {
    if (e == NULL) return;
    free(e->name);
    for (int i = 0; i < e->member_count; i++) free(e->members[i]);
    free(e->members);
    free(e);
}

Program *program_new(void) {
    Program *p = calloc(1, sizeof(Program));
    return p;
}

void program_add_function(Program *p, FunctionDecl *f) {
    p->functions = realloc(p->functions, (size_t)(p->count + 1) * sizeof(FunctionDecl *));
    p->functions[p->count++] = f;
}

void program_add_struct(Program *p, StructDecl *s) {
    p->structs = realloc(p->structs, (size_t)(p->struct_count + 1) * sizeof(StructDecl *));
    p->structs[p->struct_count++] = s;
}

void program_add_enum(Program *p, EnumDecl *e) {
    p->enums = realloc(p->enums, (size_t)(p->enum_count + 1) * sizeof(EnumDecl *));
    p->enums[p->enum_count++] = e;
}

void program_add_global(Program *p, Stmt *g) {
    p->globals = realloc(p->globals, (size_t)(p->global_count + 1) * sizeof(Stmt *));
    p->globals[p->global_count++] = g;
}

void program_free(Program *p) {
    if (p == NULL) return;
    for (int i = 0; i < p->count; i++) function_decl_free(p->functions[i]);
    free(p->functions);
    for (int i = 0; i < p->struct_count; i++) struct_decl_free(p->structs[i]);
    free(p->structs);
    for (int i = 0; i < p->enum_count; i++) enum_decl_free(p->enums[i]);
    free(p->enums);
    for (int i = 0; i < p->global_count; i++) stmt_free(p->globals[i]);
    free(p->globals);
    free(p);
}
