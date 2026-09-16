#include "sema.h"

#include <stdlib.h>
#include <string.h>

#include "builtins.h"

typedef struct {
    const char *name;
    Type type;
    int is_fixed;
} VarEntry;

typedef struct Scope {
    struct Scope *parent;
    VarEntry *vars;
    int count;
    int capacity;
} Scope;

typedef struct {
    Program *prog;
    Diag *diag;
    FunctionDecl **funcs; /* alias of prog->functions, for lookup by name */
    FunctionDecl *current_function;
    int loop_depth; /* > 0 inside a while/for body; guards break/continue */
} Sema;

static void scope_push_var(Scope *sc, const char *name, Type type, int is_fixed) {
    if (sc->count == sc->capacity) {
        sc->capacity = sc->capacity == 0 ? 8 : sc->capacity * 2;
        sc->vars = realloc(sc->vars, (size_t)sc->capacity * sizeof(VarEntry));
    }
    sc->vars[sc->count].name = name;
    sc->vars[sc->count].type = type;
    sc->vars[sc->count].is_fixed = is_fixed;
    sc->count++;
}

static int scope_declared_here(Scope *sc, const char *name) {
    for (int i = 0; i < sc->count; i++) {
        if (strcmp(sc->vars[i].name, name) == 0) return 1;
    }
    return 0;
}

static VarEntry *scope_find(Scope *sc, const char *name) {
    for (Scope *s = sc; s != NULL; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            if (strcmp(s->vars[i].name, name) == 0) return &s->vars[i];
        }
    }
    return NULL;
}

static Type scope_lookup(Scope *sc, const char *name, int *found) {
    VarEntry *e = scope_find(sc, name);
    if (e == NULL) { *found = 0; return TYPE_UNKNOWN; }
    *found = 1;
    return e->type;
}

/* Reports and returns 0 if `name` is declared and fixed; else returns 1
 * (including when `name` isn't declared at all -- the caller's own
 * "undeclared" check runs separately and takes priority). `verb` fills in
 * "cannot <verb> fixed variable 'name'", e.g. "assign to", "push onto". */
static int check_not_fixed(Sema *sm, Scope *sc, const char *name, int line, const char *verb) {
    VarEntry *e = scope_find(sc, name);
    if (e != NULL && e->is_fixed) {
        diag_set(sm->diag, line, "cannot %s fixed variable '%s'", verb, name);
        return 0;
    }
    return 1;
}

/* Walks a field-access chain (a.b.c) down to the plain variable it's rooted
 * in, e.g. for `a.b.c = v;`'s base expression `a.b` -- fixed-ness is a
 * property of that root binding, not of any intermediate field. */
static const char *root_var_name(Expr *e) {
    while (e->kind == EXPR_FIELD) e = e->as.field.base;
    return e->kind == EXPR_VAR ? e->as.string_val : NULL;
}

static FunctionDecl *find_function(Sema *sm, const char *name) {
    for (int i = 0; i < sm->prog->count; i++) {
        if (strcmp(sm->funcs[i]->name, name) == 0) return sm->funcs[i];
    }
    return NULL;
}

static int find_struct_index(Sema *sm, const char *name) {
    for (int i = 0; i < sm->prog->struct_count; i++) {
        if (strcmp(sm->prog->structs[i]->name, name) == 0) return i;
    }
    return -1;
}

static int find_enum_index(Sema *sm, const char *name) {
    for (int i = 0; i < sm->prog->enum_count; i++) {
        if (strcmp(sm->prog->enums[i]->name, name) == 0) return i;
    }
    return -1;
}

static int find_enum_member_index(const EnumDecl *ed, const char *name) {
    for (int i = 0; i < ed->member_count; i++) {
        if (strcmp(ed->members[i], name) == 0) return i;
    }
    return -1;
}

static const Param *find_struct_field(const StructDecl *sd, const char *name) {
    for (int i = 0; i < sd->field_count; i++) {
        if (strcmp(sd->fields[i].name, name) == 0) return &sd->fields[i];
    }
    return NULL;
}

static int is_numeric(Type t) { return t == TYPE_INT || t == TYPE_FLOAT; }

/* Global initializers stay literal-only so codegen can emit them as plain C
 * global initializers without worrying about C's rules on what counts as a
 * constant expression at file scope. */
static int is_constant_literal(Expr *e) {
    switch (e->kind) {
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_BOOL:
        case EXPR_STRING:
            return 1;
        case EXPR_UNARY:
            return e->as.unary.op == TOK_MINUS && is_constant_literal(e->as.unary.operand);
        default:
            return 0;
    }
}

static Type check_expr(Sema *sm, Scope *sc, Expr *e);

static Type check_builtin_call(Sema *sm, Scope *sc, Expr *e, const Builtin *b) {
    if (e->as.call.arg_count != b->param_count) {
        diag_set(sm->diag, e->line, "'%s' expects %d argument%s, got %d",
                 b->name, b->param_count, b->param_count == 1 ? "" : "s", e->as.call.arg_count);
        return TYPE_UNKNOWN;
    }
    for (int i = 0; i < b->param_count; i++) {
        Type arg_type = check_expr(sm, sc, e->as.call.args[i]);
        if (sm->diag->has_error) return TYPE_UNKNOWN;
        if (arg_type != b->params[i]) {
            diag_set(sm->diag, e->as.call.args[i]->line,
                     "argument %d of '%s' has type %s, expected %s",
                     i + 1, b->name, type_name(arg_type), type_name(b->params[i]));
            return TYPE_UNKNOWN;
        }
    }
    if (b->needs_sdl) sm->prog->uses_sdl = 1;
    if (strcmp(b->name, "launch_args") == 0) sm->prog->uses_args = 1;
    return b->return_type;
}

static Type check_call(Sema *sm, Scope *sc, Expr *e) {
    FunctionDecl *fn = find_function(sm, e->as.call.callee);
    if (fn == NULL) {
        const Builtin *b = find_builtin(e->as.call.callee);
        if (b != NULL) return check_builtin_call(sm, sc, e, b);
        diag_set(sm->diag, e->line, "call to undeclared function '%s'", e->as.call.callee);
        return TYPE_UNKNOWN;
    }
    if (fn->param_count != e->as.call.arg_count) {
        diag_set(sm->diag, e->line, "function '%s' expects %d argument%s, got %d",
                 fn->name, fn->param_count, fn->param_count == 1 ? "" : "s",
                 e->as.call.arg_count);
        return TYPE_UNKNOWN;
    }
    for (int i = 0; i < e->as.call.arg_count; i++) {
        Type arg_type = check_expr(sm, sc, e->as.call.args[i]);
        if (sm->diag->has_error) return TYPE_UNKNOWN;
        if (arg_type != fn->params[i].type) {
            diag_set(sm->diag, e->as.call.args[i]->line,
                     "argument %d of call to '%s' has type %s, expected %s",
                     i + 1, fn->name, type_name(arg_type), type_name(fn->params[i].type));
            return TYPE_UNKNOWN;
        }
    }
    return fn->return_type;
}

static Type check_binary(Sema *sm, Expr *e, Type lt, Type rt) {
    TokenType op = e->as.binary.op;
    switch (op) {
        case TOK_AND:
        case TOK_OR:
            if (lt != TYPE_BOOL || rt != TYPE_BOOL) {
                diag_set(sm->diag, e->line, "'%s' requires bool operands, got %s and %s",
                         op == TOK_AND ? "&&" : "||", type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            return TYPE_BOOL;
        case TOK_EQ:
        case TOK_NE:
            if (lt != rt) {
                diag_set(sm->diag, e->line, "cannot compare %s and %s", type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            if (type_is_list(lt) || type_is_map(lt) || type_is_struct(lt)) {
                diag_set(sm->diag, e->line,
                         "cannot compare %s with '%s'; compare fields/elements individually instead",
                         type_name(lt), op == TOK_EQ ? "==" : "!=");
                return TYPE_UNKNOWN;
            }
            return TYPE_BOOL;
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
            if (!is_numeric(lt) || lt != rt) {
                diag_set(sm->diag, e->line, "cannot compare %s and %s", type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            return TYPE_BOOL;
        case TOK_PLUS:
            if (lt == TYPE_STRING && rt == TYPE_STRING) return TYPE_STRING;
            if (!is_numeric(lt) || lt != rt) {
                diag_set(sm->diag, e->line, "cannot apply '+' to %s and %s",
                         type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            return lt;
        case TOK_MINUS:
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
            if (!is_numeric(lt) || lt != rt) {
                diag_set(sm->diag, e->line, "cannot apply arithmetic to %s and %s",
                         type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            return lt;
        case TOK_AMP:
        case TOK_PIPE:
        case TOK_CARET:
        case TOK_SHL:
        case TOK_SHR:
            if (lt != TYPE_INT || rt != TYPE_INT) {
                diag_set(sm->diag, e->line, "'%s' requires int operands, got %s and %s",
                         op == TOK_AMP ? "&" : op == TOK_PIPE ? "|" : op == TOK_CARET ? "^" :
                         op == TOK_SHL ? "<<" : ">>",
                         type_name(lt), type_name(rt));
                return TYPE_UNKNOWN;
            }
            return TYPE_INT;
        default:
            diag_set(sm->diag, e->line, "internal error: unhandled binary operator");
            return TYPE_UNKNOWN;
    }
}

static Type check_expr(Sema *sm, Scope *sc, Expr *e) {
    if (sm->diag->has_error) return TYPE_UNKNOWN;
    switch (e->kind) {
        case EXPR_INT: e->type = TYPE_INT; return TYPE_INT;
        case EXPR_FLOAT: e->type = TYPE_FLOAT; return TYPE_FLOAT;
        case EXPR_BOOL: e->type = TYPE_BOOL; return TYPE_BOOL;
        case EXPR_STRING: e->type = TYPE_STRING; return TYPE_STRING;
        case EXPR_VAR: {
            int found;
            Type t = scope_lookup(sc, e->as.string_val, &found);
            if (!found) {
                diag_set(sm->diag, e->line, "use of undeclared variable '%s'", e->as.string_val);
                return TYPE_UNKNOWN;
            }
            e->type = t;
            return t;
        }
        case EXPR_UNARY: {
            Type operand = check_expr(sm, sc, e->as.unary.operand);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (e->as.unary.op == TOK_BANG) {
                if (operand != TYPE_BOOL) {
                    diag_set(sm->diag, e->line, "'!' requires a bool operand, got %s", type_name(operand));
                    return TYPE_UNKNOWN;
                }
                e->type = TYPE_BOOL;
            } else if (e->as.unary.op == TOK_TILDE) {
                if (operand != TYPE_INT) {
                    diag_set(sm->diag, e->line, "'~' requires an int operand, got %s", type_name(operand));
                    return TYPE_UNKNOWN;
                }
                e->type = TYPE_INT;
            } else { /* TOK_MINUS */
                if (!is_numeric(operand)) {
                    diag_set(sm->diag, e->line, "unary '-' requires a numeric operand, got %s",
                             type_name(operand));
                    return TYPE_UNKNOWN;
                }
                e->type = operand;
            }
            return e->type;
        }
        case EXPR_BINARY: {
            Type lt = check_expr(sm, sc, e->as.binary.left);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            Type rt = check_expr(sm, sc, e->as.binary.right);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            e->type = check_binary(sm, e, lt, rt);
            return e->type;
        }
        case EXPR_CALL:
            e->type = check_call(sm, sc, e);
            return e->type;
        case EXPR_LIST: {
            if (e->as.list_lit.count == 0) {
                diag_set(sm->diag, e->line, "cannot infer the element type of an empty list literal");
                return TYPE_UNKNOWN;
            }
            Type elem = check_expr(sm, sc, e->as.list_lit.elements[0]);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (list_of(elem) == TYPE_UNKNOWN) {
                diag_set(sm->diag, e->as.list_lit.elements[0]->line,
                         "list elements must be int, float, bool, string, or a struct; got %s",
                         type_name(elem));
                return TYPE_UNKNOWN;
            }
            for (int i = 1; i < e->as.list_lit.count; i++) {
                Type t = check_expr(sm, sc, e->as.list_lit.elements[i]);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (t != elem) {
                    diag_set(sm->diag, e->as.list_lit.elements[i]->line,
                             "list element %d has type %s, expected %s", i + 1,
                             type_name(t), type_name(elem));
                    return TYPE_UNKNOWN;
                }
            }
            e->type = list_of(elem);
            return e->type;
        }
        case EXPR_INDEX: {
            Type base = check_expr(sm, sc, e->as.index.base);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (type_is_list(base)) {
                Type idx = check_expr(sm, sc, e->as.index.index);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (idx != TYPE_INT) {
                    diag_set(sm->diag, e->line, "list index must be int, got %s", type_name(idx));
                    return TYPE_UNKNOWN;
                }
                e->type = list_elem(base);
                return e->type;
            }
            if (type_is_map(base)) {
                Type key = check_expr(sm, sc, e->as.index.index);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (key != TYPE_STRING) {
                    diag_set(sm->diag, e->line, "map key must be string, got %s", type_name(key));
                    return TYPE_UNKNOWN;
                }
                e->type = map_value(base);
                return e->type;
            }
            if (base == TYPE_STRING) {
                Type idx = check_expr(sm, sc, e->as.index.index);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (idx != TYPE_INT) {
                    diag_set(sm->diag, e->line, "string index must be int, got %s", type_name(idx));
                    return TYPE_UNKNOWN;
                }
                e->type = TYPE_STRING; /* no dedicated char type; a character is a length-1 string */
                return e->type;
            }
            diag_set(sm->diag, e->line, "cannot index into %s", type_name(base));
            return TYPE_UNKNOWN;
        }
        case EXPR_LEN: {
            Type target = check_expr(sm, sc, e->as.len.target);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (!type_is_list(target) && !type_is_map(target) && target != TYPE_STRING) {
                diag_set(sm->diag, e->line, "len() requires a list, map, or string, got %s",
                         type_name(target));
                return TYPE_UNKNOWN;
            }
            e->type = TYPE_INT;
            return TYPE_INT;
        }
        case EXPR_MAP: {
            if (e->as.map_lit.count == 0) {
                diag_set(sm->diag, e->line, "cannot infer the value type of an empty map literal");
                return TYPE_UNKNOWN;
            }
            Type value_type = TYPE_UNKNOWN;
            for (int i = 0; i < e->as.map_lit.count; i++) {
                Type key = check_expr(sm, sc, e->as.map_lit.keys[i]);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (key != TYPE_STRING) {
                    diag_set(sm->diag, e->as.map_lit.keys[i]->line,
                             "map key %d has type %s, expected string", i + 1, type_name(key));
                    return TYPE_UNKNOWN;
                }
                Type value = check_expr(sm, sc, e->as.map_lit.values[i]);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (i == 0) {
                    if (map_of(value) == TYPE_UNKNOWN) {
                        diag_set(sm->diag, e->as.map_lit.values[i]->line,
                                 "map values must be int, float, bool, or string; got %s",
                                 type_name(value));
                        return TYPE_UNKNOWN;
                    }
                    value_type = value;
                } else if (value != value_type) {
                    diag_set(sm->diag, e->as.map_lit.values[i]->line,
                             "map value %d has type %s, expected %s", i + 1,
                             type_name(value), type_name(value_type));
                    return TYPE_UNKNOWN;
                }
            }
            e->type = map_of(value_type);
            return e->type;
        }
        case EXPR_STRUCT_LIT: {
            int struct_idx = find_struct_index(sm, e->as.struct_lit.struct_name);
            if (struct_idx < 0) {
                diag_set(sm->diag, e->line, "unknown struct '%s'", e->as.struct_lit.struct_name);
                return TYPE_UNKNOWN;
            }
            const StructDecl *sd = sm->prog->structs[struct_idx];
            if (e->as.struct_lit.count != sd->field_count) {
                diag_set(sm->diag, e->line, "struct '%s' has %d field%s, got %d",
                         sd->name, sd->field_count, sd->field_count == 1 ? "" : "s",
                         e->as.struct_lit.count);
                return TYPE_UNKNOWN;
            }
            /* every declared field must appear exactly once; order in the
             * literal doesn't have to match declaration order */
            int *matched = calloc((size_t)sd->field_count, sizeof(int));
            for (int i = 0; i < e->as.struct_lit.count; i++) {
                const char *fname = e->as.struct_lit.field_names[i];
                const Param *field = find_struct_field(sd, fname);
                if (field == NULL) {
                    diag_set(sm->diag, e->line, "struct '%s' has no field '%s'", sd->name, fname);
                    free(matched);
                    return TYPE_UNKNOWN;
                }
                int field_index = (int)(field - sd->fields);
                if (matched[field_index]) {
                    diag_set(sm->diag, e->line, "field '%s' set twice in '%s' literal", fname, sd->name);
                    free(matched);
                    return TYPE_UNKNOWN;
                }
                matched[field_index] = 1;
                Type value_type = check_expr(sm, sc, e->as.struct_lit.field_values[i]);
                if (sm->diag->has_error) { free(matched); return TYPE_UNKNOWN; }
                if (value_type != field->type) {
                    diag_set(sm->diag, e->as.struct_lit.field_values[i]->line,
                             "field '%s' of '%s' expects %s, got %s", fname, sd->name,
                             type_name(field->type), type_name(value_type));
                    free(matched);
                    return TYPE_UNKNOWN;
                }
            }
            free(matched);
            e->type = TYPE_STRUCT_BASE + struct_idx;
            return e->type;
        }
        case EXPR_FIELD: {
            Type base = check_expr(sm, sc, e->as.field.base);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (!type_is_struct(base)) {
                diag_set(sm->diag, e->line, "cannot access field '%s' on %s",
                         e->as.field.field, type_name(base));
                return TYPE_UNKNOWN;
            }
            const StructDecl *sd = struct_decl_for(base);
            const Param *field = find_struct_field(sd, e->as.field.field);
            if (field == NULL) {
                diag_set(sm->diag, e->line, "struct '%s' has no field '%s'", sd->name, e->as.field.field);
                return TYPE_UNKNOWN;
            }
            e->type = field->type;
            return e->type;
        }
        case EXPR_TERNARY: {
            Type cond = check_expr(sm, sc, e->as.ternary.cond);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (cond != TYPE_BOOL) {
                diag_set(sm->diag, e->line, "ternary condition must be bool, got %s", type_name(cond));
                return TYPE_UNKNOWN;
            }
            Type then_type = check_expr(sm, sc, e->as.ternary.then_val);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            Type else_type = check_expr(sm, sc, e->as.ternary.else_val);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (then_type != else_type) {
                diag_set(sm->diag, e->line, "ternary branches have different types: %s and %s",
                         type_name(then_type), type_name(else_type));
                return TYPE_UNKNOWN;
            }
            e->type = then_type;
            return e->type;
        }
        case EXPR_ENUM_MEMBER: {
            int enum_idx = find_enum_index(sm, e->as.enum_member.enum_name);
            if (enum_idx < 0) {
                diag_set(sm->diag, e->line, "unknown enum '%s'", e->as.enum_member.enum_name);
                return TYPE_UNKNOWN;
            }
            const EnumDecl *ed = sm->prog->enums[enum_idx];
            if (find_enum_member_index(ed, e->as.enum_member.member_name) < 0) {
                diag_set(sm->diag, e->line, "enum '%s' has no member '%s'",
                         ed->name, e->as.enum_member.member_name);
                return TYPE_UNKNOWN;
            }
            e->type = TYPE_ENUM_BASE + enum_idx;
            return e->type;
        }
        case EXPR_CONTAINS: {
            Type container_type = check_expr(sm, sc, e->as.contains.list);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (type_is_map(container_type)) {
                Type key_type = check_expr(sm, sc, e->as.contains.value);
                if (sm->diag->has_error) return TYPE_UNKNOWN;
                if (key_type != TYPE_STRING) {
                    diag_set(sm->diag, e->line, "contains() key has type %s, expected string",
                             type_name(key_type));
                    return TYPE_UNKNOWN;
                }
                e->type = TYPE_BOOL;
                return TYPE_BOOL;
            }
            if (!type_is_list(container_type)) {
                diag_set(sm->diag, e->line, "contains() requires a list or map, got %s",
                         type_name(container_type));
                return TYPE_UNKNOWN;
            }
            if (type_is_list_of_struct(container_type)) {
                diag_set(sm->diag, e->line,
                         "cannot check containment in a list of structs; structs have no "
                         "equality -- compare fields individually");
                return TYPE_UNKNOWN;
            }
            Type value_type = check_expr(sm, sc, e->as.contains.value);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            Type elem = list_elem(container_type);
            if (value_type != elem) {
                diag_set(sm->diag, e->line, "contains() value has type %s, expected %s",
                         type_name(value_type), type_name(elem));
                return TYPE_UNKNOWN;
            }
            e->type = TYPE_BOOL;
            return TYPE_BOOL;
        }
        case EXPR_KEYS: {
            Type map_type = check_expr(sm, sc, e->as.keys.target);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (!type_is_map(map_type)) {
                diag_set(sm->diag, e->line, "keys() requires a map, got %s", type_name(map_type));
                return TYPE_UNKNOWN;
            }
            e->type = TYPE_LIST_STRING;
            return TYPE_LIST_STRING;
        }
        case EXPR_SLICE: {
            Type base_type = check_expr(sm, sc, e->as.slice.base);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (!type_is_list(base_type)) {
                diag_set(sm->diag, e->line, "cannot slice %s; only lists support [a:b]",
                         type_name(base_type));
                return TYPE_UNKNOWN;
            }
            Type start_type = check_expr(sm, sc, e->as.slice.start);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (start_type != TYPE_INT) {
                diag_set(sm->diag, e->line, "slice start must be int, got %s", type_name(start_type));
                return TYPE_UNKNOWN;
            }
            Type end_type = check_expr(sm, sc, e->as.slice.end);
            if (sm->diag->has_error) return TYPE_UNKNOWN;
            if (end_type != TYPE_INT) {
                diag_set(sm->diag, e->line, "slice end must be int, got %s", type_name(end_type));
                return TYPE_UNKNOWN;
            }
            e->type = base_type;
            return base_type;
        }
    }
    return TYPE_UNKNOWN;
}

static void check_stmt(Sema *sm, Scope *parent_scope, Stmt *s);

static void check_block(Sema *sm, Scope *parent_scope, Stmt *block) {
    Scope sc = {0};
    sc.parent = parent_scope;
    for (int i = 0; i < block->as.block.count && !sm->diag->has_error; i++) {
        check_stmt(sm, &sc, block->as.block.stmts[i]);
    }
    free(sc.vars);
}

static void check_stmt(Sema *sm, Scope *sc, Stmt *s) {
    if (sm->diag->has_error) return;
    switch (s->kind) {
        case STMT_LET: {
            Type t = check_expr(sm, sc, s->as.let_stmt.init);
            if (sm->diag->has_error) return;
            if (t == TYPE_VOID) {
                diag_set(sm->diag, s->line, "cannot assign a void value to variable '%s'",
                         s->as.let_stmt.name);
                return;
            }
            if (scope_declared_here(sc, s->as.let_stmt.name)) {
                diag_set(sm->diag, s->line, "variable '%s' is already declared in this scope",
                         s->as.let_stmt.name);
                return;
            }
            s->as.let_stmt.resolved_type = t;
            scope_push_var(sc, s->as.let_stmt.name, t, s->as.let_stmt.is_fixed);
            return;
        }
        case STMT_ASSIGN: {
            int found;
            Type declared = scope_lookup(sc, s->as.assign_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "assignment to undeclared variable '%s'",
                         s->as.assign_stmt.name);
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.assign_stmt.name, s->line, "assign to")) return;
            Type value_type = check_expr(sm, sc, s->as.assign_stmt.value);
            if (sm->diag->has_error) return;
            if (value_type != declared) {
                diag_set(sm->diag, s->line, "cannot assign %s to variable '%s' of type %s",
                         type_name(value_type), s->as.assign_stmt.name, type_name(declared));
                return;
            }
            return;
        }
        case STMT_OUTPUT: {
            for (int i = 0; i < s->as.output_stmt.count; i++) {
                Type t = check_expr(sm, sc, s->as.output_stmt.values[i]);
                if (sm->diag->has_error) return;
                if (type_is_list(t) || type_is_map(t)) {
                    diag_set(sm->diag, s->line,
                             "cannot output a %s directly; index into it or use len()", type_name(t));
                    return;
                }
                if (type_is_struct(t)) {
                    diag_set(sm->diag, s->line,
                             "cannot output a struct directly; output its fields instead");
                    return;
                }
                if (t == TYPE_VOID) {
                    diag_set(sm->diag, s->line, "cannot output a void value");
                    return;
                }
            }
            return;
        }
        case STMT_IF: {
            Type cond = check_expr(sm, sc, s->as.if_stmt.cond);
            if (sm->diag->has_error) return;
            if (cond != TYPE_BOOL) {
                diag_set(sm->diag, s->line, "if condition must be bool, got %s", type_name(cond));
                return;
            }
            check_block(sm, sc, s->as.if_stmt.then_branch);
            if (sm->diag->has_error) return;
            if (s->as.if_stmt.else_branch != NULL) {
                if (s->as.if_stmt.else_branch->kind == STMT_IF) {
                    check_stmt(sm, sc, s->as.if_stmt.else_branch);
                } else {
                    check_block(sm, sc, s->as.if_stmt.else_branch);
                }
            }
            return;
        }
        case STMT_WHILE: {
            Type cond = check_expr(sm, sc, s->as.while_stmt.cond);
            if (sm->diag->has_error) return;
            if (cond != TYPE_BOOL) {
                diag_set(sm->diag, s->line, "while condition must be bool, got %s", type_name(cond));
                return;
            }
            sm->loop_depth++;
            check_block(sm, sc, s->as.while_stmt.body);
            sm->loop_depth--;
            return;
        }
        case STMT_FOR: {
            Scope for_scope = {0};
            for_scope.parent = sc;
            check_stmt(sm, &for_scope, s->as.for_stmt.init);
            if (sm->diag->has_error) { free(for_scope.vars); return; }
            Type cond = check_expr(sm, &for_scope, s->as.for_stmt.cond);
            if (sm->diag->has_error) { free(for_scope.vars); return; }
            if (cond != TYPE_BOOL) {
                diag_set(sm->diag, s->line, "for condition must be bool, got %s", type_name(cond));
                free(for_scope.vars);
                return;
            }
            sm->loop_depth++;
            check_block(sm, &for_scope, s->as.for_stmt.body);
            if (!sm->diag->has_error) check_stmt(sm, &for_scope, s->as.for_stmt.step);
            sm->loop_depth--;
            free(for_scope.vars);
            return;
        }
        case STMT_BREAK:
            if (sm->loop_depth == 0) diag_set(sm->diag, s->line, "'break' outside of a loop");
            return;
        case STMT_CONTINUE:
            if (sm->loop_depth == 0) diag_set(sm->diag, s->line, "'continue' outside of a loop");
            return;
        case STMT_RETURN: {
            Type expected = sm->current_function->return_type;
            if (s->as.return_stmt.value == NULL) {
                if (expected != TYPE_VOID) {
                    diag_set(sm->diag, s->line, "function '%s' must return a value of type %s",
                             sm->current_function->name, type_name(expected));
                }
                return;
            }
            Type actual = check_expr(sm, sc, s->as.return_stmt.value);
            if (sm->diag->has_error) return;
            if (expected == TYPE_VOID) {
                diag_set(sm->diag, s->line, "function '%s' does not return a value",
                         sm->current_function->name);
                return;
            }
            if (actual != expected) {
                diag_set(sm->diag, s->line, "function '%s' returns %s, got %s",
                         sm->current_function->name, type_name(expected), type_name(actual));
            }
            return;
        }
        case STMT_BLOCK:
            check_block(sm, sc, s);
            return;
        case STMT_EXPR:
            check_expr(sm, sc, s->as.expr_stmt.expr);
            return;
        case STMT_PUSH: {
            int found;
            Type declared = scope_lookup(sc, s->as.push_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "push() on undeclared variable '%s'", s->as.push_stmt.name);
                return;
            }
            if (!type_is_list(declared)) {
                diag_set(sm->diag, s->line, "push() requires a list variable, got %s", type_name(declared));
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.push_stmt.name, s->line, "push onto")) return;
            Type value_type = check_expr(sm, sc, s->as.push_stmt.value);
            if (sm->diag->has_error) return;
            Type elem = list_elem(declared);
            if (value_type != elem) {
                diag_set(sm->diag, s->line, "cannot push %s onto %s",
                         type_name(value_type), type_name(declared));
            }
            return;
        }
        case STMT_INDEX_ASSIGN: {
            int found;
            Type declared = scope_lookup(sc, s->as.index_assign_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "assignment to undeclared variable '%s'",
                         s->as.index_assign_stmt.name);
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.index_assign_stmt.name, s->line, "assign into")) return;
            if (type_is_list(declared)) {
                Type idx = check_expr(sm, sc, s->as.index_assign_stmt.index);
                if (sm->diag->has_error) return;
                if (idx != TYPE_INT) {
                    diag_set(sm->diag, s->line, "list index must be int, got %s", type_name(idx));
                    return;
                }
                Type value_type = check_expr(sm, sc, s->as.index_assign_stmt.value);
                if (sm->diag->has_error) return;
                Type elem = list_elem(declared);
                if (value_type != elem) {
                    diag_set(sm->diag, s->line, "cannot assign %s into %s",
                             type_name(value_type), type_name(declared));
                }
                return;
            }
            if (type_is_map(declared)) {
                Type key = check_expr(sm, sc, s->as.index_assign_stmt.index);
                if (sm->diag->has_error) return;
                if (key != TYPE_STRING) {
                    diag_set(sm->diag, s->line, "map key must be string, got %s", type_name(key));
                    return;
                }
                Type value_type = check_expr(sm, sc, s->as.index_assign_stmt.value);
                if (sm->diag->has_error) return;
                Type expected = map_value(declared);
                if (value_type != expected) {
                    diag_set(sm->diag, s->line, "cannot assign %s into %s",
                             type_name(value_type), type_name(declared));
                }
                return;
            }
            if (declared == TYPE_STRING) {
                diag_set(sm->diag, s->line, "strings are immutable; cannot assign to a character");
                return;
            }
            diag_set(sm->diag, s->line, "cannot index into %s", type_name(declared));
            return;
        }
        case STMT_FIELD_ASSIGN: {
            Type declared = check_expr(sm, sc, s->as.field_assign_stmt.base);
            if (sm->diag->has_error) return;
            if (!type_is_struct(declared)) {
                diag_set(sm->diag, s->line, "cannot access field '%s' on %s",
                         s->as.field_assign_stmt.field, type_name(declared));
                return;
            }
            const char *root = root_var_name(s->as.field_assign_stmt.base);
            if (root != NULL && !check_not_fixed(sm, sc, root, s->line, "assign into")) return;
            const StructDecl *sd = struct_decl_for(declared);
            const Param *field = find_struct_field(sd, s->as.field_assign_stmt.field);
            if (field == NULL) {
                diag_set(sm->diag, s->line, "struct '%s' has no field '%s'",
                         sd->name, s->as.field_assign_stmt.field);
                return;
            }
            Type value_type = check_expr(sm, sc, s->as.field_assign_stmt.value);
            if (sm->diag->has_error) return;
            if (value_type != field->type) {
                diag_set(sm->diag, s->line, "cannot assign %s to field '%s' of type %s",
                         type_name(value_type), field->name, type_name(field->type));
            }
            return;
        }
        case STMT_SORT: {
            int found;
            Type declared = scope_lookup(sc, s->as.sort_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "sort() on undeclared variable '%s'", s->as.sort_stmt.name);
                return;
            }
            if (!type_is_list(declared)) {
                diag_set(sm->diag, s->line, "sort() requires a list, got %s", type_name(declared));
                return;
            }
            if (type_is_list_of_struct(declared)) {
                diag_set(sm->diag, s->line,
                         "cannot sort a list of structs; structs have no ordering -- "
                         "sort by a field manually");
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.sort_stmt.name, s->line, "sort")) return;
            s->as.sort_stmt.resolved_type = declared;
            return;
        }
        case STMT_REVERSE: {
            int found;
            Type declared = scope_lookup(sc, s->as.reverse_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "reverse() on undeclared variable '%s'",
                         s->as.reverse_stmt.name);
                return;
            }
            if (!type_is_list(declared)) {
                diag_set(sm->diag, s->line, "reverse() requires a list, got %s", type_name(declared));
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.reverse_stmt.name, s->line, "reverse")) return;
            s->as.reverse_stmt.resolved_type = declared;
            return;
        }
        case STMT_REMOVE: {
            int found;
            Type declared = scope_lookup(sc, s->as.remove_stmt.name, &found);
            if (!found) {
                diag_set(sm->diag, s->line, "remove() on undeclared variable '%s'",
                         s->as.remove_stmt.name);
                return;
            }
            if (!type_is_list(declared)) {
                diag_set(sm->diag, s->line, "remove() requires a list, got %s", type_name(declared));
                return;
            }
            if (!check_not_fixed(sm, sc, s->as.remove_stmt.name, s->line, "remove from")) return;
            Type idx = check_expr(sm, sc, s->as.remove_stmt.index);
            if (sm->diag->has_error) return;
            if (idx != TYPE_INT) {
                diag_set(sm->diag, s->line, "remove() index must be int, got %s", type_name(idx));
                return;
            }
            s->as.remove_stmt.resolved_type = declared;
            return;
        }
    }
}

int sema_check(Program *prog, Diag *diag) {
    type_system_set_program(prog);

    Sema sm = {0};
    sm.prog = prog;
    sm.diag = diag;
    sm.funcs = prog->functions;

    for (int i = 0; i < prog->struct_count; i++) {
        StructDecl *sd = prog->structs[i];
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->structs[j]->name, sd->name) == 0) {
                diag_set(diag, 0, "struct '%s' is already declared", sd->name);
                return 0;
            }
        }
        for (int j = 0; j < sd->field_count; j++) {
            for (int k = 0; k < j; k++) {
                if (strcmp(sd->fields[j].name, sd->fields[k].name) == 0) {
                    diag_set(diag, 0, "struct '%s' has field '%s' twice", sd->name, sd->fields[j].name);
                    return 0;
                }
            }
        }
    }

    for (int i = 0; i < prog->enum_count; i++) {
        EnumDecl *ed = prog->enums[i];
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->enums[j]->name, ed->name) == 0) {
                diag_set(diag, 0, "enum '%s' is already declared", ed->name);
                return 0;
            }
        }
        if (ed->member_count == 0) {
            diag_set(diag, 0, "enum '%s' has no members", ed->name);
            return 0;
        }
        for (int j = 0; j < ed->member_count; j++) {
            for (int k = 0; k < j; k++) {
                if (strcmp(ed->members[j], ed->members[k]) == 0) {
                    diag_set(diag, 0, "enum '%s' has member '%s' twice", ed->name, ed->members[j]);
                    return 0;
                }
            }
        }
    }

    FunctionDecl *main_fn = NULL;
    for (int i = 0; i < prog->count; i++) {
        FunctionDecl *f = prog->functions[i];
        for (int j = 0; j < i; j++) {
            if (strcmp(prog->functions[j]->name, f->name) == 0) {
                diag_set(diag, f->line, "function '%s' is already declared", f->name);
                return 0;
            }
        }
        if (find_builtin(f->name) != NULL) {
            diag_set(diag, f->line, "'%s' is a reserved builtin name", f->name);
            return 0;
        }
        if (strcmp(f->name, "main") == 0) {
            if (f->param_count != 0) {
                diag_set(diag, f->line, "'main' must not take any parameters");
                return 0;
            }
            if (f->return_type != TYPE_VOID) {
                diag_set(diag, f->line, "'main' must not declare a return type");
                return 0;
            }
            main_fn = f;
        }
    }
    if (main_fn == NULL) {
        diag_set(diag, 0, "program must define 'fr main() { ... }'");
        return 0;
    }

    Scope global_scope = {0};
    for (int i = 0; i < prog->global_count; i++) {
        Stmt *g = prog->globals[i];
        if (!is_constant_literal(g->as.let_stmt.init)) {
            diag_set(diag, g->line, "global variable '%s' must be initialized with a constant literal",
                     g->as.let_stmt.name);
            free(global_scope.vars);
            return 0;
        }
        if (scope_declared_here(&global_scope, g->as.let_stmt.name)) {
            diag_set(diag, g->line, "global variable '%s' is already declared", g->as.let_stmt.name);
            free(global_scope.vars);
            return 0;
        }
        Type t = check_expr(&sm, &global_scope, g->as.let_stmt.init);
        if (diag->has_error) { free(global_scope.vars); return 0; }
        g->as.let_stmt.resolved_type = t;
        scope_push_var(&global_scope, g->as.let_stmt.name, t, g->as.let_stmt.is_fixed);
    }

    for (int i = 0; i < prog->count && !diag->has_error; i++) {
        FunctionDecl *f = prog->functions[i];
        sm.current_function = f;
        Scope fn_scope = {0};
        fn_scope.parent = &global_scope;
        for (int j = 0; j < f->param_count; j++) {
            scope_push_var(&fn_scope, f->params[j].name, f->params[j].type, 0);
        }
        check_block(&sm, &fn_scope, f->body);
        free(fn_scope.vars);
    }

    free(global_scope.vars);
    return !diag->has_error;
}
