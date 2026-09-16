#ifndef FRACTYNE_AST_H
#define FRACTYNE_AST_H

#include "lexer.h"

typedef enum {
    TYPE_VOID, TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_STRING,
    TYPE_LIST_INT, TYPE_LIST_FLOAT, TYPE_LIST_BOOL, TYPE_LIST_STRING,
    TYPE_MAP_INT, TYPE_MAP_FLOAT, TYPE_MAP_BOOL, TYPE_MAP_STRING,
    TYPE_UNKNOWN
} Type;

const char *type_name(Type t);
int type_is_list(Type t);
Type list_of(Type elem);   /* TYPE_INT -> TYPE_LIST_INT, etc. */
Type list_elem(Type list); /* TYPE_LIST_INT -> TYPE_INT, etc. */

int type_is_map(Type t);
Type map_of(Type value);    /* TYPE_INT -> TYPE_MAP_INT, etc. (keys are always string) */
Type map_value(Type map);   /* TYPE_MAP_INT -> TYPE_INT, etc. */

/* User-defined struct types don't fit a fixed enum (there can be any number of
 * them), so each one gets a Type value at TYPE_STRUCT_BASE + its index into
 * the Program's struct table, assigned once all structs are parsed. This
 * keeps Type a plain comparable int everywhere else in the compiler. A
 * list<SomeStruct> gets a second dynamic Type in the same way, one range
 * over: TYPE_LIST_STRUCT_BASE + that struct's index, so list_of/list_elem
 * work for struct elements without needing real generics. */
#define TYPE_STRUCT_BASE 1000
#define TYPE_LIST_STRUCT_BASE 100000
int type_is_struct(Type t);
int type_is_list_of_struct(Type t);

/* Enums get their own dynamic Type range, same idea as structs above but
 * kept well clear of TYPE_LIST_STRUCT_BASE's headroom. An enum member is
 * just its own C enum constant under the hood (see codegen), so unlike
 * structs, enums get ordinary equality for free -- see sema's check_binary. */
#define TYPE_ENUM_BASE 200000
int type_is_enum(Type t);

typedef struct {
    char *name;
    Type type;
} Param;

typedef struct {
    char *name;
    Param *fields; /* struct fields reuse Param (name + type) */
    int field_count;
} StructDecl;

typedef struct {
    char *name;
    char **members;
    int member_count;
} EnumDecl;


typedef enum {
    EXPR_INT, EXPR_FLOAT, EXPR_BOOL, EXPR_STRING, EXPR_VAR,
    EXPR_UNARY, EXPR_BINARY, EXPR_CALL, EXPR_LIST, EXPR_INDEX, EXPR_LEN, EXPR_MAP,
    EXPR_STRUCT_LIT, EXPR_FIELD, EXPR_TERNARY, EXPR_CONTAINS, EXPR_ENUM_MEMBER, EXPR_KEYS,
    EXPR_SLICE
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    Type type; /* filled in by sema; TYPE_UNKNOWN until then */
    int line;
    union {
        long int_val;
        double float_val;
        int bool_val;
        char *string_val; /* EXPR_STRING: raw literal text; EXPR_VAR: name */
        struct { TokenType op; struct Expr *operand; } unary;
        struct { TokenType op; struct Expr *left; struct Expr *right; } binary;
        struct { char *callee; struct Expr **args; int arg_count; } call;
        struct { struct Expr **elements; int count; } list_lit;
        struct { struct Expr *base; struct Expr *index; } index;
        struct { struct Expr *target; } len;
        struct { struct Expr **keys; struct Expr **values; int count; } map_lit;
        struct { char *struct_name; char **field_names; struct Expr **field_values; int count; } struct_lit;
        struct { struct Expr *base; char *field; } field;
        struct { struct Expr *cond; struct Expr *then_val; struct Expr *else_val; } ternary;
        struct { struct Expr *list; struct Expr *value; } contains;
        struct { char *enum_name; char *member_name; } enum_member;
        struct { struct Expr *target; } keys;
        struct { struct Expr *base; struct Expr *start; struct Expr *end; } slice;
    } as;
} Expr;

typedef enum {
    STMT_LET, STMT_ASSIGN, STMT_OUTPUT, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_RETURN, STMT_BLOCK, STMT_EXPR, STMT_PUSH, STMT_INDEX_ASSIGN,
    STMT_BREAK, STMT_CONTINUE, STMT_FIELD_ASSIGN,
    STMT_SORT, STMT_REVERSE, STMT_REMOVE
} StmtKind;

typedef struct Stmt {
    StmtKind kind;
    int line;
    union {
        struct { char *name; Expr *init; Type resolved_type; int is_fixed; } let_stmt;
        struct { char *name; Expr *value; } assign_stmt;
        struct { Expr **values; int count; } output_stmt;
        struct { Expr *cond; struct Stmt *then_branch; struct Stmt *else_branch; } if_stmt;
        struct { Expr *cond; struct Stmt *body; } while_stmt;
        struct { struct Stmt *init; Expr *cond; struct Stmt *step; struct Stmt *body; } for_stmt;
        struct { Expr *value; } return_stmt; /* value == NULL for bare `return;` */
        struct { struct Stmt **stmts; int count; } block;
        struct { Expr *expr; } expr_stmt;
        struct { char *name; Expr *value; } push_stmt;
        struct { char *name; Expr *index; Expr *value; } index_assign_stmt;
        struct { Expr *base; char *field; Expr *value; } field_assign_stmt;
        struct { char *name; Type resolved_type; } sort_stmt;
        struct { char *name; Type resolved_type; } reverse_stmt;
        struct { char *name; Expr *index; Type resolved_type; } remove_stmt;
    } as;
} Stmt;

typedef struct {
    char *name;
    Param *params;
    int param_count;
    Type return_type;
    Stmt *body;
    int line;
} FunctionDecl;

typedef struct Program {
    FunctionDecl **functions;
    int count;
    StructDecl **structs;
    int struct_count;
    EnumDecl **enums;
    int enum_count;
    Stmt **globals; /* each a STMT_LET; visible to every function */
    int global_count;
    int uses_sdl; /* set by sema when a window/gfx/input/timing builtin is called
                     anywhere -- tells codegen/main.c whether SDL2 is needed at all */
    int uses_args; /* set by sema when launch_args() is called anywhere -- tells
                      codegen whether generated main() needs to receive argc/argv */
} Program;

/* Struct name lookups need the Program in scope; set once, right after
 * parsing, before sema or codegen touch any type. Single-threaded, one
 * compilation per process -- there's nothing to make this reentrant for. */
void type_system_set_program(const Program *prog);
const StructDecl *struct_decl_for(Type t);
const EnumDecl *enum_decl_for(Type t);

/* Expr constructors */
Expr *expr_new_int(long v, int line);
Expr *expr_new_float(double v, int line);
Expr *expr_new_bool(int v, int line);
Expr *expr_new_string(const char *raw, int line);
Expr *expr_new_var(const char *name, int line);
Expr *expr_new_unary(TokenType op, Expr *operand, int line);
Expr *expr_new_binary(TokenType op, Expr *left, Expr *right, int line);
Expr *expr_new_call(const char *callee, Expr **args, int arg_count, int line);
Expr *expr_new_list(Expr **elements, int count, int line);
Expr *expr_new_index(Expr *base, Expr *index, int line);
Expr *expr_new_len(Expr *target, int line);
Expr *expr_new_map(Expr **keys, Expr **values, int count, int line);
Expr *expr_new_struct_lit(const char *struct_name, char **field_names, Expr **field_values,
                           int count, int line);
Expr *expr_new_field(Expr *base, const char *field, int line);
Expr *expr_new_ternary(Expr *cond, Expr *then_val, Expr *else_val, int line);
Expr *expr_new_contains(Expr *list, Expr *value, int line);
Expr *expr_new_enum_member(const char *enum_name, const char *member_name, int line);
Expr *expr_new_keys(Expr *target, int line);
Expr *expr_new_slice(Expr *base, Expr *start, Expr *end, int line);
void expr_free(Expr *e);

/* Stmt constructors */
Stmt *stmt_new_let(const char *name, Expr *init, int line);
Stmt *stmt_new_assign(const char *name, Expr *value, int line);
Stmt *stmt_new_output(Expr **values, int count, int line);
Stmt *stmt_new_if(Expr *cond, Stmt *then_branch, Stmt *else_branch, int line);
Stmt *stmt_new_while(Expr *cond, Stmt *body, int line);
Stmt *stmt_new_for(Stmt *init, Expr *cond, Stmt *step, Stmt *body, int line);
Stmt *stmt_new_break(int line);
Stmt *stmt_new_continue(int line);
Stmt *stmt_new_return(Expr *value, int line);
Stmt *stmt_new_block(Stmt **stmts, int count, int line);
Stmt *stmt_new_expr(Expr *expr, int line);
Stmt *stmt_new_push(const char *name, Expr *value, int line);
Stmt *stmt_new_index_assign(const char *name, Expr *index, Expr *value, int line);
Stmt *stmt_new_field_assign(Expr *base, const char *field, Expr *value, int line);
Stmt *stmt_new_sort(const char *name, int line);
Stmt *stmt_new_reverse(const char *name, int line);
Stmt *stmt_new_remove(const char *name, Expr *index, int line);
void stmt_free(Stmt *s);

FunctionDecl *function_decl_new(const char *name, Param *params, int param_count,
                                 Type return_type, Stmt *body, int line);
void function_decl_free(FunctionDecl *f);

StructDecl *struct_decl_new(const char *name, Param *fields, int field_count);
void struct_decl_free(StructDecl *s);

EnumDecl *enum_decl_new(const char *name, char **members, int member_count);
void enum_decl_free(EnumDecl *e);

Program *program_new(void);
void program_add_function(Program *p, FunctionDecl *f);
void program_add_struct(Program *p, StructDecl *s);
void program_add_enum(Program *p, EnumDecl *e);
void program_add_global(Program *p, Stmt *g);
void program_free(Program *p);

/* Small growable-array helper reused by the parser for stmt/expr/param lists. */
typedef struct {
    void **items;
    int count;
    int capacity;
} PtrList;

void ptrlist_init(PtrList *l);
void ptrlist_push(PtrList *l, void *item);

#endif
