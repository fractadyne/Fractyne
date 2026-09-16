#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Type type;
    const char *tag;         /* used in generated names: fy_list_<tag>_push */
    const char *struct_name; /* e.g. FyListInt */
    const char *elem_ctype;  /* e.g. long */
} ListKind;

static const ListKind LIST_KINDS[] = {
    {TYPE_LIST_INT, "int", "FyListInt", "long"},
    {TYPE_LIST_FLOAT, "float", "FyListFloat", "double"},
    {TYPE_LIST_BOOL, "bool", "FyListBool", "int"},
    {TYPE_LIST_STRING, "string", "FyListString", "const char *"},
};
#define LIST_KIND_COUNT (sizeof(LIST_KINDS) / sizeof(LIST_KINDS[0]))

static const ListKind *list_kind_for(Type t) {
    for (size_t i = 0; i < LIST_KIND_COUNT; i++) {
        if (LIST_KINDS[i].type == t) return &LIST_KINDS[i];
    }
    return NULL;
}

typedef struct {
    Type type;
    const char *tag;          /* fy_map_<tag>_get / _set */
    const char *struct_name;  /* e.g. FyMapInt */
    const char *entry_name;   /* e.g. FyMapEntryInt */
    const char *value_ctype;  /* e.g. long; keys are always const char * */
    const char *zero_literal; /* value returned by get() on a missing key */
} MapKind;

static const MapKind MAP_KINDS[] = {
    {TYPE_MAP_INT, "int", "FyMapInt", "FyMapEntryInt", "long", "0L"},
    {TYPE_MAP_FLOAT, "float", "FyMapFloat", "FyMapEntryFloat", "double", "0.0"},
    {TYPE_MAP_BOOL, "bool", "FyMapBool", "FyMapEntryBool", "int", "0"},
    {TYPE_MAP_STRING, "string", "FyMapString", "FyMapEntryString", "const char *", "\"\""},
};
#define MAP_KIND_COUNT (sizeof(MAP_KINDS) / sizeof(MAP_KINDS[0]))

static const MapKind *map_kind_for(Type t) {
    for (size_t i = 0; i < MAP_KIND_COUNT; i++) {
        if (MAP_KINDS[i].type == t) return &MAP_KINDS[i];
    }
    return NULL;
}

static const char *c_type_name(Type t) {
    static char struct_buf[128];
    switch (t) {
        case TYPE_VOID: return "void";
        case TYPE_INT: return "long";
        case TYPE_FLOAT: return "double";
        case TYPE_BOOL: return "int";
        case TYPE_STRING: return "const char *";
        default: {
            const ListKind *lk = list_kind_for(t);
            if (lk != NULL) return lk->struct_name;
            const MapKind *mk = map_kind_for(t);
            if (mk != NULL) return mk->struct_name;
            if (type_is_struct(t)) {
                const StructDecl *sd = struct_decl_for(t);
                snprintf(struct_buf, sizeof(struct_buf), "FyStruct_%s", sd->name);
                return struct_buf;
            }
            if (type_is_enum(t)) {
                const EnumDecl *ed = enum_decl_for(t);
                snprintf(struct_buf, sizeof(struct_buf), "FyEnum_%s", ed->name);
                return struct_buf;
            }
            if (type_is_list_of_struct(t)) {
                const StructDecl *sd = struct_decl_for(list_elem(t));
                snprintf(struct_buf, sizeof(struct_buf), "FyListStruct_%s", sd->name);
                return struct_buf;
            }
            return "void";
        }
    }
}

static void emit_list_runtime(FILE *out) {
    for (size_t i = 0; i < LIST_KIND_COUNT; i++) {
        const ListKind *k = &LIST_KINDS[i];
        fprintf(out, "typedef struct { %s *data; long len; long cap; } %s;\n",
                k->elem_ctype, k->struct_name);
    }
    fprintf(out, "\n");

    for (size_t i = 0; i < LIST_KIND_COUNT; i++) {
        const ListKind *k = &LIST_KINDS[i];
        fprintf(out,
            "static %s fy_list_%s_new(long count, %s *values) __attribute__((unused));\n"
            "static %s fy_list_%s_new(long count, %s *values) {\n"
            "    long cap = count > 0 ? count : 1;\n"
            "    %s *data = malloc((size_t)cap * sizeof(%s));\n"
            "    memcpy(data, values, (size_t)count * sizeof(%s));\n"
            "    %s l; l.data = data; l.len = count; l.cap = cap;\n"
            "    return l;\n"
            "}\n\n",
            k->struct_name, k->tag, k->elem_ctype,
            k->struct_name, k->tag, k->elem_ctype,
            k->elem_ctype, k->elem_ctype, k->elem_ctype,
            k->struct_name);

        fprintf(out,
            "static void fy_list_%s_push(%s *l, %s v) __attribute__((unused));\n"
            "static void fy_list_%s_push(%s *l, %s v) {\n"
            "    if (l->len == l->cap) {\n"
            "        l->cap = l->cap == 0 ? 4 : l->cap * 2;\n"
            "        l->data = realloc(l->data, (size_t)l->cap * sizeof(%s));\n"
            "    }\n"
            "    l->data[l->len++] = v;\n"
            "}\n\n",
            k->tag, k->struct_name, k->elem_ctype,
            k->tag, k->struct_name, k->elem_ctype,
            k->elem_ctype);

        fprintf(out,
            "static %s fy_list_%s_get(%s l, long i) __attribute__((unused));\n"
            "static %s fy_list_%s_get(%s l, long i) {\n"
            "    if (i < 0 || i >= l.len) {\n"
            "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l.len);\n"
            "        exit(1);\n"
            "    }\n"
            "    return l.data[i];\n"
            "}\n\n",
            k->elem_ctype, k->tag, k->struct_name,
            k->elem_ctype, k->tag, k->struct_name);

        fprintf(out,
            "static void fy_list_%s_set(%s *l, long i, %s v) __attribute__((unused));\n"
            "static void fy_list_%s_set(%s *l, long i, %s v) {\n"
            "    if (i < 0 || i >= l->len) {\n"
            "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l->len);\n"
            "        exit(1);\n"
            "    }\n"
            "    l->data[i] = v;\n"
            "}\n\n",
            k->tag, k->struct_name, k->elem_ctype,
            k->tag, k->struct_name, k->elem_ctype);

        fprintf(out,
            "static void fy_list_%s_reverse(%s *l) __attribute__((unused));\n"
            "static void fy_list_%s_reverse(%s *l) {\n"
            "    for (long i = 0, j = l->len - 1; i < j; i++, j--) {\n"
            "        %s tmp = l->data[i]; l->data[i] = l->data[j]; l->data[j] = tmp;\n"
            "    }\n"
            "}\n\n",
            k->tag, k->struct_name,
            k->tag, k->struct_name,
            k->elem_ctype);

        fprintf(out,
            "static void fy_list_%s_remove(%s *l, long i) __attribute__((unused));\n"
            "static void fy_list_%s_remove(%s *l, long i) {\n"
            "    if (i < 0 || i >= l->len) {\n"
            "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l->len);\n"
            "        exit(1);\n"
            "    }\n"
            "    for (long j = i; j < l->len - 1; j++) l->data[j] = l->data[j + 1];\n"
            "    l->len--;\n"
            "}\n\n",
            k->tag, k->struct_name,
            k->tag, k->struct_name);

        if (k->type == TYPE_LIST_STRING) {
            fprintf(out,
                "static int fy_list_%s_cmp(const void *a, const void *b) __attribute__((unused));\n"
                "static int fy_list_%s_cmp(const void *a, const void *b) {\n"
                "    return strcmp(*(const char *const *)a, *(const char *const *)b);\n"
                "}\n\n",
                k->tag, k->tag);
        } else {
            fprintf(out,
                "static int fy_list_%s_cmp(const void *a, const void *b) __attribute__((unused));\n"
                "static int fy_list_%s_cmp(const void *a, const void *b) {\n"
                "    %s x = *(const %s *)a, y = *(const %s *)b;\n"
                "    return (x > y) - (x < y);\n"
                "}\n\n",
                k->tag, k->tag, k->elem_ctype, k->elem_ctype, k->elem_ctype);
        }

        fprintf(out,
            "static void fy_list_%s_sort(%s *l) __attribute__((unused));\n"
            "static void fy_list_%s_sort(%s *l) {\n"
            "    qsort(l->data, (size_t)l->len, sizeof(%s), fy_list_%s_cmp);\n"
            "}\n\n",
            k->tag, k->struct_name,
            k->tag, k->struct_name,
            k->elem_ctype, k->tag);

        if (k->type == TYPE_LIST_STRING) {
            fprintf(out,
                "static int fy_list_%s_contains(%s l, const char *v) __attribute__((unused));\n"
                "static int fy_list_%s_contains(%s l, const char *v) {\n"
                "    for (long i = 0; i < l.len; i++) if (strcmp(l.data[i], v) == 0) return 1;\n"
                "    return 0;\n"
                "}\n\n",
                k->tag, k->struct_name, k->tag, k->struct_name);
        } else {
            fprintf(out,
                "static int fy_list_%s_contains(%s l, %s v) __attribute__((unused));\n"
                "static int fy_list_%s_contains(%s l, %s v) {\n"
                "    for (long i = 0; i < l.len; i++) if (l.data[i] == v) return 1;\n"
                "    return 0;\n"
                "}\n\n",
                k->tag, k->struct_name, k->elem_ctype, k->tag, k->struct_name, k->elem_ctype);
        }
    }
}

static void emit_map_runtime(FILE *out) {
    for (size_t i = 0; i < MAP_KIND_COUNT; i++) {
        const MapKind *k = &MAP_KINDS[i];
        fprintf(out, "typedef struct { const char *key; %s value; } %s;\n",
                k->value_ctype, k->entry_name);
        fprintf(out, "typedef struct { %s *entries; long len; long cap; } %s;\n",
                k->entry_name, k->struct_name);
    }
    fprintf(out, "\n");

    for (size_t i = 0; i < MAP_KIND_COUNT; i++) {
        const MapKind *k = &MAP_KINDS[i];
        fprintf(out,
            "static %s fy_map_%s_from(long count, const char **keys, %s *values) __attribute__((unused));\n"
            "static %s fy_map_%s_from(long count, const char **keys, %s *values) {\n"
            "    long cap = count > 0 ? count : 1;\n"
            "    %s *entries = malloc((size_t)cap * sizeof(%s));\n"
            "    for (long i = 0; i < count; i++) { entries[i].key = keys[i]; entries[i].value = values[i]; }\n"
            "    %s m; m.entries = entries; m.len = count; m.cap = cap;\n"
            "    return m;\n"
            "}\n\n",
            k->struct_name, k->tag, k->value_ctype,
            k->struct_name, k->tag, k->value_ctype,
            k->entry_name, k->entry_name,
            k->struct_name);

        fprintf(out,
            "static %s fy_map_%s_get(%s m, const char *key) __attribute__((unused));\n"
            "static %s fy_map_%s_get(%s m, const char *key) {\n"
            "    for (long i = 0; i < m.len; i++) {\n"
            "        if (strcmp(m.entries[i].key, key) == 0) return m.entries[i].value;\n"
            "    }\n"
            "    return %s;\n"
            "}\n\n",
            k->value_ctype, k->tag, k->struct_name,
            k->value_ctype, k->tag, k->struct_name,
            k->zero_literal);

        fprintf(out,
            "static void fy_map_%s_set(%s *m, const char *key, %s value) __attribute__((unused));\n"
            "static void fy_map_%s_set(%s *m, const char *key, %s value) {\n"
            "    for (long i = 0; i < m->len; i++) {\n"
            "        if (strcmp(m->entries[i].key, key) == 0) { m->entries[i].value = value; return; }\n"
            "    }\n"
            "    if (m->len == m->cap) {\n"
            "        m->cap = m->cap == 0 ? 4 : m->cap * 2;\n"
            "        m->entries = realloc(m->entries, (size_t)m->cap * sizeof(%s));\n"
            "    }\n"
            "    m->entries[m->len].key = key;\n"
            "    m->entries[m->len].value = value;\n"
            "    m->len++;\n"
            "}\n\n",
            k->tag, k->struct_name, k->value_ctype,
            k->tag, k->struct_name, k->value_ctype,
            k->entry_name);

        fprintf(out,
            "static int fy_map_%s_has(%s m, const char *key) __attribute__((unused));\n"
            "static int fy_map_%s_has(%s m, const char *key) {\n"
            "    for (long i = 0; i < m.len; i++) {\n"
            "        if (strcmp(m.entries[i].key, key) == 0) return 1;\n"
            "    }\n"
            "    return 0;\n"
            "}\n\n",
            k->tag, k->struct_name,
            k->tag, k->struct_name);

        fprintf(out,
            "static FyListString fy_map_%s_keys(%s m) __attribute__((unused));\n"
            "static FyListString fy_map_%s_keys(%s m) {\n"
            "    long cap = m.len > 0 ? m.len : 1;\n"
            "    const char **data = malloc((size_t)cap * sizeof(const char *));\n"
            "    for (long i = 0; i < m.len; i++) data[i] = m.entries[i].key;\n"
            "    FyListString l; l.data = data; l.len = m.len; l.cap = cap;\n"
            "    return l;\n"
            "}\n\n",
            k->tag, k->struct_name,
            k->tag, k->struct_name);
    }
}

/* A list<SomeStruct> needs a per-struct list wrapper the same way primitive
 * lists use LIST_KINDS -- but since it's keyed off a dynamically-numbered
 * struct type rather than a fixed enum, it's generated here instead of a
 * static table. Emitted right alongside the struct's own typedef (see
 * emit_struct_typedefs) so a later struct with a `list<Earlier>` field
 * always finds both already defined. */
static void emit_struct_list_wrapper(FILE *out, const StructDecl *sd) {
    char struct_ctype[160];
    snprintf(struct_ctype, sizeof(struct_ctype), "FyStruct_%s", sd->name);

    fprintf(out, "typedef struct { %s *data; long len; long cap; } FyListStruct_%s;\n\n",
            struct_ctype, sd->name);

    fprintf(out,
        "static FyListStruct_%s fy_list_struct_%s_new(long count, %s *values) __attribute__((unused));\n"
        "static FyListStruct_%s fy_list_struct_%s_new(long count, %s *values) {\n"
        "    long cap = count > 0 ? count : 1;\n"
        "    %s *data = malloc((size_t)cap * sizeof(%s));\n"
        "    memcpy(data, values, (size_t)count * sizeof(%s));\n"
        "    FyListStruct_%s l; l.data = data; l.len = count; l.cap = cap;\n"
        "    return l;\n"
        "}\n\n",
        sd->name, sd->name, struct_ctype,
        sd->name, sd->name, struct_ctype,
        struct_ctype, struct_ctype, struct_ctype,
        sd->name);

    fprintf(out,
        "static void fy_list_struct_%s_push(FyListStruct_%s *l, %s v) __attribute__((unused));\n"
        "static void fy_list_struct_%s_push(FyListStruct_%s *l, %s v) {\n"
        "    if (l->len == l->cap) {\n"
        "        l->cap = l->cap == 0 ? 4 : l->cap * 2;\n"
        "        l->data = realloc(l->data, (size_t)l->cap * sizeof(%s));\n"
        "    }\n"
        "    l->data[l->len++] = v;\n"
        "}\n\n",
        sd->name, sd->name, struct_ctype,
        sd->name, sd->name, struct_ctype,
        struct_ctype);

    fprintf(out,
        "static %s fy_list_struct_%s_get(FyListStruct_%s l, long i) __attribute__((unused));\n"
        "static %s fy_list_struct_%s_get(FyListStruct_%s l, long i) {\n"
        "    if (i < 0 || i >= l.len) {\n"
        "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l.len);\n"
        "        exit(1);\n"
        "    }\n"
        "    return l.data[i];\n"
        "}\n\n",
        struct_ctype, sd->name, sd->name,
        struct_ctype, sd->name, sd->name);

    fprintf(out,
        "static void fy_list_struct_%s_set(FyListStruct_%s *l, long i, %s v) __attribute__((unused));\n"
        "static void fy_list_struct_%s_set(FyListStruct_%s *l, long i, %s v) {\n"
        "    if (i < 0 || i >= l->len) {\n"
        "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l->len);\n"
        "        exit(1);\n"
        "    }\n"
        "    l->data[i] = v;\n"
        "}\n\n",
        sd->name, sd->name, struct_ctype,
        sd->name, sd->name, struct_ctype);

    fprintf(out,
        "static void fy_list_struct_%s_reverse(FyListStruct_%s *l) __attribute__((unused));\n"
        "static void fy_list_struct_%s_reverse(FyListStruct_%s *l) {\n"
        "    for (long i = 0, j = l->len - 1; i < j; i++, j--) {\n"
        "        %s tmp = l->data[i]; l->data[i] = l->data[j]; l->data[j] = tmp;\n"
        "    }\n"
        "}\n\n",
        sd->name, sd->name, sd->name, sd->name, struct_ctype);

    fprintf(out,
        "static void fy_list_struct_%s_remove(FyListStruct_%s *l, long i) __attribute__((unused));\n"
        "static void fy_list_struct_%s_remove(FyListStruct_%s *l, long i) {\n"
        "    if (i < 0 || i >= l->len) {\n"
        "        fprintf(stderr, \"fractyne: runtime error: list index %%ld out of bounds (len %%ld)\\n\", i, l->len);\n"
        "        exit(1);\n"
        "    }\n"
        "    for (long j = i; j < l->len - 1; j++) l->data[j] = l->data[j + 1];\n"
        "    l->len--;\n"
        "}\n\n",
        sd->name, sd->name, sd->name, sd->name);
}

/* Enums have no field dependencies, so they can all be emitted up front, in
 * declaration order, with no ordering dance like emit_struct_typedefs below
 * needs. Each member becomes a C enum constant named <EnumName>_<Member>
 * (kept apart from the AST's ExprKind naming) so two different Fractyne
 * enums can each have a same-named member without colliding in the
 * generated C. A parallel string table lets output() print a member's own
 * name (see emit_output) since a plain C enum has no such lookup built in. */
static void emit_enum_typedefs(FILE *out, Program *prog) {
    for (int i = 0; i < prog->enum_count; i++) {
        EnumDecl *ed = prog->enums[i];
        fprintf(out, "typedef enum { ");
        for (int j = 0; j < ed->member_count; j++) {
            if (j > 0) fprintf(out, ", ");
            fprintf(out, "%s_%s", ed->name, ed->members[j]);
        }
        fprintf(out, " } FyEnum_%s;\n", ed->name);

        fprintf(out, "static const char *fy_enum_names_%s[] __attribute__((unused)) = {", ed->name);
        for (int j = 0; j < ed->member_count; j++) {
            if (j > 0) fprintf(out, ", ");
            fprintf(out, "\"%s\"", ed->members[j]);
        }
        fprintf(out, "};\n");
    }
    if (prog->enum_count > 0) fprintf(out, "\n");
}

/* Struct typedefs must appear after any struct-typed (or list-of-that-struct
 * typed) field's own typedef, so this emits in dependency order rather than
 * declaration order: repeatedly emit whichever remaining structs have all
 * their struct/list-of-struct-typed fields already emitted. A genuine cycle
 * (impossible to lay out in C without a pointer indirection Fractyne doesn't
 * have) stops making progress and is left for gcc to report -- not worth a
 * bespoke diagnostic for. */
static void emit_struct_typedefs(FILE *out, Program *prog) {
    int *emitted = calloc((size_t)prog->struct_count, sizeof(int));
    int remaining = prog->struct_count;
    while (remaining > 0) {
        int progressed = 0;
        for (int i = 0; i < prog->struct_count; i++) {
            if (emitted[i]) continue;
            StructDecl *sd = prog->structs[i];
            int ready = 1;
            for (int j = 0; j < sd->field_count; j++) {
                Type ft = sd->fields[j].type;
                int dep = -1;
                if (type_is_struct(ft)) dep = ft - TYPE_STRUCT_BASE;
                else if (type_is_list_of_struct(ft)) dep = list_elem(ft) - TYPE_STRUCT_BASE;
                if (dep >= 0 && !emitted[dep]) { ready = 0; break; }
            }
            if (!ready) continue;
            fprintf(out, "typedef struct {\n");
            for (int j = 0; j < sd->field_count; j++) {
                fprintf(out, "    %s %s;\n", c_type_name(sd->fields[j].type), sd->fields[j].name);
            }
            fprintf(out, "} FyStruct_%s;\n\n", sd->name);
            emit_struct_list_wrapper(out, sd);
            emitted[i] = 1;
            remaining--;
            progressed = 1;
        }
        if (!progressed) break;
    }
    free(emitted);
}

static void emit_conversion_builtins(FILE *out) {
    fprintf(out,
        "static const char *fy_int_to_string(long x) __attribute__((unused));\n"
        "static const char *fy_int_to_string(long x) {\n"
        "    char buf[32];\n"
        "    int n = snprintf(buf, sizeof(buf), \"%%ld\", x);\n"
        "    char *r = malloc((size_t)n + 1);\n"
        "    memcpy(r, buf, (size_t)n + 1);\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static const char *fy_float_to_string(double x) __attribute__((unused));\n"
        "static const char *fy_float_to_string(double x) {\n"
        "    char buf[64];\n"
        "    int n = snprintf(buf, sizeof(buf), \"%%g\", x);\n"
        "    char *r = malloc((size_t)n + 1);\n"
        "    memcpy(r, buf, (size_t)n + 1);\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static const char *fy_bool_to_string(int b) __attribute__((unused));\n"
        "static const char *fy_bool_to_string(int b) {\n"
        "    return b ? \"true\" : \"false\";\n"
        "}\n\n");

    fprintf(out,
        "static long fy_string_to_int(const char *s) __attribute__((unused));\n"
        "static long fy_string_to_int(const char *s) {\n"
        "    return atol(s);\n"
        "}\n\n");

    fprintf(out,
        "static double fy_string_to_float(const char *s) __attribute__((unused));\n"
        "static double fy_string_to_float(const char *s) {\n"
        "    return atof(s);\n"
        "}\n\n");

    fprintf(out,
        "static double fy_int_to_float(long x) __attribute__((unused));\n"
        "static double fy_int_to_float(long x) {\n"
        "    return (double)x;\n"
        "}\n\n");

    fprintf(out,
        "static long fy_float_to_int(double x) __attribute__((unused));\n"
        "static long fy_float_to_int(double x) {\n"
        "    return (long)x;\n"
        "}\n\n");
}

static void emit_shape_builtins(FILE *out) {
    fprintf(out,
        "static void fy_draw_square(long n) __attribute__((unused));\n"
        "static void fy_draw_square(long n) {\n"
        "    for (long row = 0; row < n; row++) {\n"
        "        for (long col = 0; col < n; col++) putchar('*');\n"
        "        putchar('\\n');\n"
        "    }\n"
        "}\n\n");

    fprintf(out,
        "static void fy_draw_rect(long w, long h) __attribute__((unused));\n"
        "static void fy_draw_rect(long w, long h) {\n"
        "    for (long row = 0; row < h; row++) {\n"
        "        for (long col = 0; col < w; col++) putchar('*');\n"
        "        putchar('\\n');\n"
        "    }\n"
        "}\n\n");

    fprintf(out,
        "static void fy_draw_triangle(long n) __attribute__((unused));\n"
        "static void fy_draw_triangle(long n) {\n"
        "    for (long row = 1; row <= n; row++) {\n"
        "        for (long col = 0; col < row; col++) putchar('*');\n"
        "        putchar('\\n');\n"
        "    }\n"
        "}\n\n");
}

static void emit_math_builtins(FILE *out) {
    fprintf(out,
        "static double fy_sqrt(double x) __attribute__((unused));\n"
        "static double fy_sqrt(double x) { return sqrt(x); }\n\n"
        "static double fy_pow(double base, double exp) __attribute__((unused));\n"
        "static double fy_pow(double base, double exp) { return pow(base, exp); }\n\n"
        "static long fy_abs_int(long x) __attribute__((unused));\n"
        "static long fy_abs_int(long x) { return x < 0 ? -x : x; }\n\n"
        "static double fy_abs_float(double x) __attribute__((unused));\n"
        "static double fy_abs_float(double x) { return x < 0 ? -x : x; }\n\n"
        "static long fy_min_int(long a, long b) __attribute__((unused));\n"
        "static long fy_min_int(long a, long b) { return a < b ? a : b; }\n\n"
        "static long fy_max_int(long a, long b) __attribute__((unused));\n"
        "static long fy_max_int(long a, long b) { return a > b ? a : b; }\n\n"
        "static double fy_min_float(double a, double b) __attribute__((unused));\n"
        "static double fy_min_float(double a, double b) { return a < b ? a : b; }\n\n"
        "static double fy_max_float(double a, double b) __attribute__((unused));\n"
        "static double fy_max_float(double a, double b) { return a > b ? a : b; }\n\n");

    fprintf(out,
        "static const char *fy_input(void) __attribute__((unused));\n"
        "static const char *fy_input(void) {\n"
        "    size_t cap = 64, len = 0;\n"
        "    char *buf = malloc(cap);\n"
        "    int c;\n"
        "    while ((c = fgetc(stdin)) != EOF && c != '\\n') {\n"
        "        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }\n"
        "        buf[len++] = (char)c;\n"
        "    }\n"
        "    buf[len] = '\\0';\n"
        "    return buf;\n"
        "}\n\n");
}

static void emit_string_builtins(FILE *out) {
    fprintf(out,
        "static const char *fy_substring(const char *s, long start, long length) __attribute__((unused));\n"
        "static const char *fy_substring(const char *s, long start, long length) {\n"
        "    long slen = (long)strlen(s);\n"
        "    if (start < 0) start = 0;\n"
        "    if (start > slen) start = slen;\n"
        "    if (length < 0) length = 0;\n"
        "    if (start + length > slen) length = slen - start;\n"
        "    char *r = malloc((size_t)length + 1);\n"
        "    memcpy(r, s + start, (size_t)length);\n"
        "    r[length] = '\\0';\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static const char *fy_to_upper(const char *s) __attribute__((unused));\n"
        "static const char *fy_to_upper(const char *s) {\n"
        "    size_t len = strlen(s);\n"
        "    char *r = malloc(len + 1);\n"
        "    for (size_t i = 0; i < len; i++) r[i] = (char)toupper((unsigned char)s[i]);\n"
        "    r[len] = '\\0';\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static const char *fy_to_lower(const char *s) __attribute__((unused));\n"
        "static const char *fy_to_lower(const char *s) {\n"
        "    size_t len = strlen(s);\n"
        "    char *r = malloc(len + 1);\n"
        "    for (size_t i = 0; i < len; i++) r[i] = (char)tolower((unsigned char)s[i]);\n"
        "    r[len] = '\\0';\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static const char *fy_trim(const char *s) __attribute__((unused));\n"
        "static const char *fy_trim(const char *s) {\n"
        "    size_t start = 0, end = strlen(s);\n"
        "    while (start < end && isspace((unsigned char)s[start])) start++;\n"
        "    while (end > start && isspace((unsigned char)s[end - 1])) end--;\n"
        "    size_t len = end - start;\n"
        "    char *r = malloc(len + 1);\n"
        "    memcpy(r, s + start, len);\n"
        "    r[len] = '\\0';\n"
        "    return r;\n"
        "}\n\n");

    fprintf(out,
        "static long fy_index_of(const char *s, const char *needle) __attribute__((unused));\n"
        "static long fy_index_of(const char *s, const char *needle) {\n"
        "    const char *found = strstr(s, needle);\n"
        "    return found ? (long)(found - s) : -1L;\n"
        "}\n\n");
}

static void emit_random_builtins(FILE *out) {
    fprintf(out,
        "static int fy_rand_seeded __attribute__((unused)) = 0;\n"
        "static void fy_rand_ensure_seeded(void) __attribute__((unused));\n"
        "static void fy_rand_ensure_seeded(void) {\n"
        "    if (!fy_rand_seeded) { srand((unsigned)time(NULL)); fy_rand_seeded = 1; }\n"
        "}\n\n");

    fprintf(out,
        "static double fy_random(void) __attribute__((unused));\n"
        "static double fy_random(void) {\n"
        "    fy_rand_ensure_seeded();\n"
        "    return (double)rand() / ((double)RAND_MAX + 1.0);\n"
        "}\n\n");

    fprintf(out,
        "static long fy_random_int(long lo, long hi) __attribute__((unused));\n"
        "static long fy_random_int(long lo, long hi) {\n"
        "    fy_rand_ensure_seeded();\n"
        "    if (hi <= lo) return lo;\n"
        "    return lo + (long)(rand() %% (hi - lo + 1));\n"
        "}\n\n");

    fprintf(out,
        "static void fy_random_seed(long seed) __attribute__((unused));\n"
        "static void fy_random_seed(long seed) {\n"
        "    srand((unsigned)seed);\n"
        "    fy_rand_seeded = 1;\n"
        "}\n\n");
}

/* read_file/file_exists return "read nothing"/"false" rather than an error
 * on failure -- no exceptions or Option type in Fractyne, so a missing or
 * unreadable file behaves the same as input() at EOF: a defined, checkable
 * empty result instead of a crash. write_file/append_file report success
 * via their bool return instead. */
static void emit_file_builtins(FILE *out) {
    fprintf(out,
        "static const char *fy_read_file(const char *path) __attribute__((unused));\n"
        "static const char *fy_read_file(const char *path) {\n"
        "    FILE *f = fopen(path, \"rb\");\n"
        "    if (f == NULL) { char *r = malloc(1); r[0] = '\\0'; return r; }\n"
        "    fseek(f, 0, SEEK_END);\n"
        "    long size = ftell(f);\n"
        "    if (size < 0) { fclose(f); char *r = malloc(1); r[0] = '\\0'; return r; }\n"
        "    rewind(f);\n"
        "    char *buf = malloc((size_t)size + 1);\n"
        "    size_t n = fread(buf, 1, (size_t)size, f);\n"
        "    buf[n] = '\\0';\n"
        "    fclose(f);\n"
        "    return buf;\n"
        "}\n\n");

    fprintf(out,
        "static int fy_write_file(const char *path, const char *content) __attribute__((unused));\n"
        "static int fy_write_file(const char *path, const char *content) {\n"
        "    FILE *f = fopen(path, \"wb\");\n"
        "    if (f == NULL) return 0;\n"
        "    size_t len = strlen(content);\n"
        "    size_t written = fwrite(content, 1, len, f);\n"
        "    fclose(f);\n"
        "    return written == len;\n"
        "}\n\n");

    fprintf(out,
        "static int fy_append_file(const char *path, const char *content) __attribute__((unused));\n"
        "static int fy_append_file(const char *path, const char *content) {\n"
        "    FILE *f = fopen(path, \"ab\");\n"
        "    if (f == NULL) return 0;\n"
        "    size_t len = strlen(content);\n"
        "    size_t written = fwrite(content, 1, len, f);\n"
        "    fclose(f);\n"
        "    return written == len;\n"
        "}\n\n");

    fprintf(out,
        "static int fy_file_exists(const char *path) __attribute__((unused));\n"
        "static int fy_file_exists(const char *path) {\n"
        "    FILE *f = fopen(path, \"rb\");\n"
        "    if (f == NULL) return 0;\n"
        "    fclose(f);\n"
        "    return 1;\n"
        "}\n\n");
}

/* fy_cli_args is only populated when the program calls launch_args() (see
 * Program.uses_args): codegen only gives generated main() an (argc, argv) it
 * can read from otherwise, so leaving this always-declared-but-unpopulated
 * for programs that don't use it stays harmless and warning-free. */
static void emit_args_builtins(FILE *out) {
    fprintf(out, "static FyListString fy_cli_args;\n");
    fprintf(out,
        "static void fy_init_launch_args(int argc, char **argv) __attribute__((unused));\n"
        "static void fy_init_launch_args(int argc, char **argv) {\n"
        "    long n = argc > 1 ? (long)argc - 1 : 0;\n"
        "    const char **data = malloc((size_t)(n > 0 ? n : 1) * sizeof(const char *));\n"
        "    for (long i = 0; i < n; i++) data[i] = argv[i + 1];\n"
        "    fy_cli_args.data = data;\n"
        "    fy_cli_args.len = n;\n"
        "    fy_cli_args.cap = n;\n"
        "}\n\n");

    fprintf(out,
        "static FyListString fy_launch_args(void) __attribute__((unused));\n"
        "static FyListString fy_launch_args(void) {\n"
        "    return fy_cli_args;\n"
        "}\n\n");
}

/* Only emitted for programs that actually call a window/gfx/input/timing
 * builtin (see Program.uses_sdl) -- everything here is a thin, direct
 * wrapper around one SDL2 call, backed by a handful of file-scope globals
 * standing in for "the" window, since Fractyne has no handle/pointer type
 * to thread a real window object through user code. Only one window at a
 * time; that's the deliberate v1 scope. */
static void emit_sdl_builtins(FILE *out) {
    fprintf(out,
        "static SDL_Window *fy_sdl_window = NULL;\n"
        "static SDL_Renderer *fy_sdl_renderer = NULL;\n"
        "static int fy_sdl_should_close = 0;\n"
        "static Uint8 fy_sdl_color_r = 255, fy_sdl_color_g = 255, fy_sdl_color_b = 255;\n"
        "static int fy_sdl_mouse_x = 0, fy_sdl_mouse_y = 0;\n"
        "static Uint32 fy_sdl_mouse_buttons = 0;\n"
        "static const Uint8 *fy_sdl_keys = NULL;\n\n");

    fprintf(out,
        "static int fy_window_open(long width, long height, const char *title) __attribute__((unused));\n"
        "static int fy_window_open(long width, long height, const char *title) {\n"
        "    if (SDL_Init(SDL_INIT_VIDEO) != 0) {\n"
        "        fprintf(stderr, \"fractyne: SDL_Init failed: %%s\\n\", SDL_GetError());\n"
        "        return 0;\n"
        "    }\n"
        "    fy_sdl_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n"
        "        (int)width, (int)height, SDL_WINDOW_SHOWN);\n"
        "    if (fy_sdl_window == NULL) {\n"
        "        fprintf(stderr, \"fractyne: SDL_CreateWindow failed: %%s\\n\", SDL_GetError());\n"
        "        return 0;\n"
        "    }\n"
        "    fy_sdl_renderer = SDL_CreateRenderer(fy_sdl_window, -1, SDL_RENDERER_ACCELERATED);\n"
        "    if (fy_sdl_renderer == NULL) {\n"
        "        fprintf(stderr, \"fractyne: SDL_CreateRenderer failed: %%s\\n\", SDL_GetError());\n"
        "        return 0;\n"
        "    }\n"
        "    fy_sdl_keys = SDL_GetKeyboardState(NULL);\n"
        "    return 1;\n"
        "}\n\n");

    fprintf(out,
        "static void fy_window_close(void) __attribute__((unused));\n"
        "static void fy_window_close(void) {\n"
        "    if (fy_sdl_renderer != NULL) { SDL_DestroyRenderer(fy_sdl_renderer); fy_sdl_renderer = NULL; }\n"
        "    if (fy_sdl_window != NULL) { SDL_DestroyWindow(fy_sdl_window); fy_sdl_window = NULL; }\n"
        "    SDL_Quit();\n"
        "}\n\n");

    fprintf(out,
        "static int fy_window_should_close(void) __attribute__((unused));\n"
        "static int fy_window_should_close(void) { return fy_sdl_should_close; }\n\n");

    fprintf(out,
        "static void fy_window_poll_events(void) __attribute__((unused));\n"
        "static void fy_window_poll_events(void) {\n"
        "    SDL_Event ev;\n"
        "    while (SDL_PollEvent(&ev)) {\n"
        "        if (ev.type == SDL_QUIT) fy_sdl_should_close = 1;\n"
        "    }\n"
        "    fy_sdl_mouse_buttons = SDL_GetMouseState(&fy_sdl_mouse_x, &fy_sdl_mouse_y);\n"
        "}\n\n");

    fprintf(out,
        "static void fy_window_clear(long r, long g, long b) __attribute__((unused));\n"
        "static void fy_window_clear(long r, long g, long b) {\n"
        "    SDL_SetRenderDrawColor(fy_sdl_renderer, (Uint8)r, (Uint8)g, (Uint8)b, 255);\n"
        "    SDL_RenderClear(fy_sdl_renderer);\n"
        "}\n\n");

    fprintf(out,
        "static void fy_window_present(void) __attribute__((unused));\n"
        "static void fy_window_present(void) { SDL_RenderPresent(fy_sdl_renderer); }\n\n");

    fprintf(out,
        "static void fy_gfx_set_color(long r, long g, long b) __attribute__((unused));\n"
        "static void fy_gfx_set_color(long r, long g, long b) {\n"
        "    fy_sdl_color_r = (Uint8)r; fy_sdl_color_g = (Uint8)g; fy_sdl_color_b = (Uint8)b;\n"
        "}\n\n");

    fprintf(out,
        "static void fy_gfx_pixel(long x, long y) __attribute__((unused));\n"
        "static void fy_gfx_pixel(long x, long y) {\n"
        "    SDL_SetRenderDrawColor(fy_sdl_renderer, fy_sdl_color_r, fy_sdl_color_g, fy_sdl_color_b, 255);\n"
        "    SDL_RenderDrawPoint(fy_sdl_renderer, (int)x, (int)y);\n"
        "}\n\n");

    fprintf(out,
        "static void fy_gfx_line(long x1, long y1, long x2, long y2) __attribute__((unused));\n"
        "static void fy_gfx_line(long x1, long y1, long x2, long y2) {\n"
        "    SDL_SetRenderDrawColor(fy_sdl_renderer, fy_sdl_color_r, fy_sdl_color_g, fy_sdl_color_b, 255);\n"
        "    SDL_RenderDrawLine(fy_sdl_renderer, (int)x1, (int)y1, (int)x2, (int)y2);\n"
        "}\n\n");

    fprintf(out,
        "static void fy_gfx_rect(long x, long y, long w, long h) __attribute__((unused));\n"
        "static void fy_gfx_rect(long x, long y, long w, long h) {\n"
        "    SDL_Rect rect; rect.x = (int)x; rect.y = (int)y; rect.w = (int)w; rect.h = (int)h;\n"
        "    SDL_SetRenderDrawColor(fy_sdl_renderer, fy_sdl_color_r, fy_sdl_color_g, fy_sdl_color_b, 255);\n"
        "    SDL_RenderFillRect(fy_sdl_renderer, &rect);\n"
        "}\n\n");

    fprintf(out,
        "static void fy_gfx_circle(long cx, long cy, long radius) __attribute__((unused));\n"
        "static void fy_gfx_circle(long cx, long cy, long radius) {\n"
        "    SDL_SetRenderDrawColor(fy_sdl_renderer, fy_sdl_color_r, fy_sdl_color_g, fy_sdl_color_b, 255);\n"
        "    for (long dy = -radius; dy <= radius; dy++) {\n"
        "        long dx = (long)(sqrt((double)(radius * radius - dy * dy)) + 0.5);\n"
        "        SDL_RenderDrawLine(fy_sdl_renderer, (int)(cx - dx), (int)(cy + dy), (int)(cx + dx), (int)(cy + dy));\n"
        "    }\n"
        "}\n\n");

    fprintf(out,
        "static int fy_key_down(const char *name) __attribute__((unused));\n"
        "static int fy_key_down(const char *name) {\n"
        "    SDL_Scancode sc = SDL_GetScancodeFromName(name);\n"
        "    if (sc == SDL_SCANCODE_UNKNOWN || fy_sdl_keys == NULL) return 0;\n"
        "    return fy_sdl_keys[sc] ? 1 : 0;\n"
        "}\n\n");

    fprintf(out,
        "static long fy_mouse_x(void) __attribute__((unused));\n"
        "static long fy_mouse_x(void) { return (long)fy_sdl_mouse_x; }\n\n"
        "static long fy_mouse_y(void) __attribute__((unused));\n"
        "static long fy_mouse_y(void) { return (long)fy_sdl_mouse_y; }\n\n");

    fprintf(out,
        "static int fy_mouse_down(const char *button) __attribute__((unused));\n"
        "static int fy_mouse_down(const char *button) {\n"
        "    Uint32 mask;\n"
        "    if (strcmp(button, \"left\") == 0) mask = SDL_BUTTON(SDL_BUTTON_LEFT);\n"
        "    else if (strcmp(button, \"right\") == 0) mask = SDL_BUTTON(SDL_BUTTON_RIGHT);\n"
        "    else if (strcmp(button, \"middle\") == 0) mask = SDL_BUTTON(SDL_BUTTON_MIDDLE);\n"
        "    else return 0;\n"
        "    return (fy_sdl_mouse_buttons & mask) ? 1 : 0;\n"
        "}\n\n");

    fprintf(out,
        "static void fy_delay_ms(long ms) __attribute__((unused));\n"
        "static void fy_delay_ms(long ms) { SDL_Delay((Uint32)ms); }\n\n"
        "static long fy_ticks_ms(void) __attribute__((unused));\n"
        "static long fy_ticks_ms(void) { return (long)SDL_GetTicks(); }\n\n");
}

static const char *c_func_name(const char *fractyne_name, char *buf, size_t buf_size) {
    if (strcmp(fractyne_name, "main") == 0) return "main";
    snprintf(buf, buf_size, "fy_%s", fractyne_name);
    return buf;
}

static const char *binop_text(TokenType op) {
    switch (op) {
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%";
        case TOK_EQ: return "==";
        case TOK_NE: return "!=";
        case TOK_LT: return "<";
        case TOK_LE: return "<=";
        case TOK_GT: return ">";
        case TOK_GE: return ">=";
        case TOK_AND: return "&&";
        case TOK_OR: return "||";
        case TOK_AMP: return "&";
        case TOK_PIPE: return "|";
        case TOK_CARET: return "^";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        default: return "?";
    }
}

static void emit_expr(FILE *out, Expr *e) {
    switch (e->kind) {
        case EXPR_INT:
            fprintf(out, "%ldL", e->as.int_val);
            return;
        case EXPR_FLOAT:
            fprintf(out, "%g", e->as.float_val);
            return;
        case EXPR_BOOL:
            fprintf(out, "%d", e->as.bool_val ? 1 : 0);
            return;
        case EXPR_STRING:
            fprintf(out, "\"%s\"", e->as.string_val);
            return;
        case EXPR_VAR:
            fprintf(out, "%s", e->as.string_val);
            return;
        case EXPR_UNARY:
            fprintf(out, "(%s", e->as.unary.op == TOK_BANG ? "!" :
                                 e->as.unary.op == TOK_TILDE ? "~" : "-");
            emit_expr(out, e->as.unary.operand);
            fprintf(out, ")");
            return;
        case EXPR_BINARY: {
            Type operand_type = e->as.binary.left->type;
            if (operand_type == TYPE_STRING && (e->as.binary.op == TOK_EQ || e->as.binary.op == TOK_NE)) {
                fprintf(out, "(strcmp(");
                emit_expr(out, e->as.binary.left);
                fprintf(out, ", ");
                emit_expr(out, e->as.binary.right);
                fprintf(out, ") %s 0)", e->as.binary.op == TOK_EQ ? "==" : "!=");
                return;
            }
            if (operand_type == TYPE_STRING && e->as.binary.op == TOK_PLUS) {
                fprintf(out, "fy_concat(");
                emit_expr(out, e->as.binary.left);
                fprintf(out, ", ");
                emit_expr(out, e->as.binary.right);
                fprintf(out, ")");
                return;
            }
            fprintf(out, "(");
            emit_expr(out, e->as.binary.left);
            fprintf(out, " %s ", binop_text(e->as.binary.op));
            emit_expr(out, e->as.binary.right);
            fprintf(out, ")");
            return;
        }
        case EXPR_CALL: {
            char buf[256];
            fprintf(out, "%s(", c_func_name(e->as.call.callee, buf, sizeof(buf)));
            for (int i = 0; i < e->as.call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, e->as.call.args[i]);
            }
            fprintf(out, ")");
            return;
        }
        case EXPR_LIST: {
            if (type_is_list_of_struct(e->type)) {
                const StructDecl *sd = struct_decl_for(list_elem(e->type));
                fprintf(out, "fy_list_struct_%s_new(%dL, (FyStruct_%s[]){", sd->name,
                        e->as.list_lit.count, sd->name);
            } else {
                const ListKind *k = list_kind_for(e->type);
                fprintf(out, "fy_list_%s_new(%dL, (%s[]){", k->tag, e->as.list_lit.count, k->elem_ctype);
            }
            for (int i = 0; i < e->as.list_lit.count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, e->as.list_lit.elements[i]);
            }
            fprintf(out, "})");
            return;
        }
        case EXPR_INDEX:
            if (type_is_map(e->as.index.base->type)) {
                const MapKind *k = map_kind_for(e->as.index.base->type);
                fprintf(out, "fy_map_%s_get(", k->tag);
                emit_expr(out, e->as.index.base);
                fprintf(out, ", ");
                emit_expr(out, e->as.index.index);
                fprintf(out, ")");
            } else if (e->as.index.base->type == TYPE_STRING) {
                fprintf(out, "fy_string_char_at(");
                emit_expr(out, e->as.index.base);
                fprintf(out, ", ");
                emit_expr(out, e->as.index.index);
                fprintf(out, ")");
            } else if (type_is_list_of_struct(e->as.index.base->type)) {
                const StructDecl *sd = struct_decl_for(list_elem(e->as.index.base->type));
                fprintf(out, "fy_list_struct_%s_get(", sd->name);
                emit_expr(out, e->as.index.base);
                fprintf(out, ", ");
                emit_expr(out, e->as.index.index);
                fprintf(out, ")");
            } else {
                const ListKind *k = list_kind_for(e->as.index.base->type);
                fprintf(out, "fy_list_%s_get(", k->tag);
                emit_expr(out, e->as.index.base);
                fprintf(out, ", ");
                emit_expr(out, e->as.index.index);
                fprintf(out, ")");
            }
            return;
        case EXPR_LEN:
            if (e->as.len.target->type == TYPE_STRING) {
                fprintf(out, "(long)strlen(");
                emit_expr(out, e->as.len.target);
                fprintf(out, ")");
            } else {
                emit_expr(out, e->as.len.target);
                fprintf(out, ".len");
            }
            return;
        case EXPR_MAP: {
            const MapKind *k = map_kind_for(e->type);
            fprintf(out, "fy_map_%s_from(%dL, (const char *[]){", k->tag, e->as.map_lit.count);
            for (int i = 0; i < e->as.map_lit.count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, e->as.map_lit.keys[i]);
            }
            fprintf(out, "}, (%s[]){", k->value_ctype);
            for (int i = 0; i < e->as.map_lit.count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, e->as.map_lit.values[i]);
            }
            fprintf(out, "})");
            return;
        }
        case EXPR_STRUCT_LIT: {
            fprintf(out, "(%s){", c_type_name(e->type));
            for (int i = 0; i < e->as.struct_lit.count; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, ".%s = ", e->as.struct_lit.field_names[i]);
                emit_expr(out, e->as.struct_lit.field_values[i]);
            }
            fprintf(out, "}");
            return;
        }
        case EXPR_FIELD:
            emit_expr(out, e->as.field.base);
            fprintf(out, ".%s", e->as.field.field);
            return;
        case EXPR_TERNARY:
            fprintf(out, "(");
            emit_expr(out, e->as.ternary.cond);
            fprintf(out, " ? ");
            emit_expr(out, e->as.ternary.then_val);
            fprintf(out, " : ");
            emit_expr(out, e->as.ternary.else_val);
            fprintf(out, ")");
            return;
        case EXPR_CONTAINS: {
            Type container_type = e->as.contains.list->type;
            if (type_is_map(container_type)) {
                const MapKind *k = map_kind_for(container_type);
                fprintf(out, "fy_map_%s_has(", k->tag);
            } else {
                const ListKind *k = list_kind_for(container_type);
                fprintf(out, "fy_list_%s_contains(", k->tag);
            }
            emit_expr(out, e->as.contains.list);
            fprintf(out, ", ");
            emit_expr(out, e->as.contains.value);
            fprintf(out, ")");
            return;
        }
        case EXPR_ENUM_MEMBER:
            fprintf(out, "%s_%s", e->as.enum_member.enum_name, e->as.enum_member.member_name);
            return;
        case EXPR_KEYS: {
            const MapKind *k = map_kind_for(e->as.keys.target->type);
            fprintf(out, "fy_map_%s_keys(", k->tag);
            emit_expr(out, e->as.keys.target);
            fprintf(out, ")");
            return;
        }
    }
}

static void indent(FILE *out, int level) {
    for (int i = 0; i < level; i++) fprintf(out, "    ");
}

/* Prints exactly one value's own representation, no separator and no
 * trailing newline -- output(a, b, c) (see emit_output_stmt) calls this once
 * per argument, printing a space between calls and one newline at the end,
 * rather than each argument getting its own line. */
static void emit_output_value(FILE *out, Expr *value) {
    if (type_is_enum(value->type)) {
        const EnumDecl *ed = enum_decl_for(value->type);
        fprintf(out, "printf(\"%%s\", fy_enum_names_%s[(int)(", ed->name);
        emit_expr(out, value);
        fprintf(out, ")])");
        return;
    }
    switch (value->type) {
        case TYPE_INT:
            fprintf(out, "printf(\"%%ld\", ");
            emit_expr(out, value);
            fprintf(out, ")");
            return;
        case TYPE_FLOAT:
            fprintf(out, "printf(\"%%g\", ");
            emit_expr(out, value);
            fprintf(out, ")");
            return;
        case TYPE_BOOL:
            fprintf(out, "printf(\"%%s\", (");
            emit_expr(out, value);
            fprintf(out, ") ? \"true\" : \"false\")");
            return;
        case TYPE_STRING:
            fprintf(out, "printf(\"%%s\", ");
            emit_expr(out, value);
            fprintf(out, ")");
            return;
        default:
            fprintf(out, "/* unreachable: output of void/unknown */ (void)0");
            return;
    }
}

static void emit_output_stmt(FILE *out, int level, Expr **values, int count) {
    indent(out, level);
    for (int i = 0; i < count; i++) {
        if (i > 0) fprintf(out, "printf(\" \"); ");
        emit_output_value(out, values[i]);
        fprintf(out, "; ");
    }
    fprintf(out, "printf(\"\\n\");\n");
}

static void emit_stmt(FILE *out, int level, Stmt *s, int in_main);

static void emit_block(FILE *out, int level, Stmt *block, int in_main) {
    indent(out, level);
    fprintf(out, "{\n");
    for (int i = 0; i < block->as.block.count; i++) {
        emit_stmt(out, level + 1, block->as.block.stmts[i], in_main);
    }
    indent(out, level);
    fprintf(out, "}\n");
}

/* in_main: Fractyne's main() is conceptually void (see sema_check), but C
 * requires `int main(void)`, so a bare `return;` written inside it has to
 * become `return 0;` here -- everywhere else a bare return is emitted as-is
 * since every other Fractyne void function is genuinely declared void in C. */
static void emit_stmt(FILE *out, int level, Stmt *s, int in_main) {
    switch (s->kind) {
        case STMT_LET:
            indent(out, level);
            /* c_type_name(TYPE_STRING) is already "const char *" -- skip the
             * extra qualifier there to avoid a duplicate-const warning. */
            fprintf(out, "%s%s %s = ",
                    (s->as.let_stmt.is_fixed && s->as.let_stmt.resolved_type != TYPE_STRING) ? "const " : "",
                    c_type_name(s->as.let_stmt.resolved_type), s->as.let_stmt.name);
            emit_expr(out, s->as.let_stmt.init);
            fprintf(out, ";\n");
            return;
        case STMT_ASSIGN:
            indent(out, level);
            fprintf(out, "%s = ", s->as.assign_stmt.name);
            emit_expr(out, s->as.assign_stmt.value);
            fprintf(out, ";\n");
            return;
        case STMT_OUTPUT:
            emit_output_stmt(out, level, s->as.output_stmt.values, s->as.output_stmt.count);
            return;
        case STMT_IF:
            indent(out, level);
            fprintf(out, "if (");
            emit_expr(out, s->as.if_stmt.cond);
            fprintf(out, ")\n");
            emit_block(out, level, s->as.if_stmt.then_branch, in_main);
            if (s->as.if_stmt.else_branch != NULL) {
                indent(out, level);
                fprintf(out, "else\n");
                if (s->as.if_stmt.else_branch->kind == STMT_IF) {
                    emit_stmt(out, level, s->as.if_stmt.else_branch, in_main);
                } else {
                    emit_block(out, level, s->as.if_stmt.else_branch, in_main);
                }
            }
            return;
        case STMT_WHILE:
            indent(out, level);
            fprintf(out, "while (");
            emit_expr(out, s->as.while_stmt.cond);
            fprintf(out, ")\n");
            emit_block(out, level, s->as.while_stmt.body, in_main);
            return;
        case STMT_FOR: {
            Stmt *init = s->as.for_stmt.init;
            Stmt *step = s->as.for_stmt.step;
            indent(out, level);
            fprintf(out, "for (");
            if (init->kind == STMT_LET) {
                fprintf(out, "%s %s = ", c_type_name(init->as.let_stmt.resolved_type), init->as.let_stmt.name);
                emit_expr(out, init->as.let_stmt.init);
            } else {
                fprintf(out, "%s = ", init->as.assign_stmt.name);
                emit_expr(out, init->as.assign_stmt.value);
            }
            fprintf(out, "; ");
            emit_expr(out, s->as.for_stmt.cond);
            fprintf(out, "; %s = ", step->as.assign_stmt.name);
            emit_expr(out, step->as.assign_stmt.value);
            fprintf(out, ")\n");
            emit_block(out, level, s->as.for_stmt.body, in_main);
            return;
        }
        case STMT_BREAK:
            indent(out, level);
            fprintf(out, "break;\n");
            return;
        case STMT_CONTINUE:
            indent(out, level);
            fprintf(out, "continue;\n");
            return;
        case STMT_RETURN:
            indent(out, level);
            if (s->as.return_stmt.value == NULL) {
                fprintf(out, in_main ? "return 0;\n" : "return;\n");
            } else {
                fprintf(out, "return ");
                emit_expr(out, s->as.return_stmt.value);
                fprintf(out, ";\n");
            }
            return;
        case STMT_BLOCK:
            emit_block(out, level, s, in_main);
            return;
        case STMT_EXPR:
            indent(out, level);
            emit_expr(out, s->as.expr_stmt.expr);
            fprintf(out, ";\n");
            return;
        case STMT_PUSH: {
            indent(out, level);
            if (type_is_struct(s->as.push_stmt.value->type)) {
                const StructDecl *sd = struct_decl_for(s->as.push_stmt.value->type);
                fprintf(out, "fy_list_struct_%s_push(&%s, ", sd->name, s->as.push_stmt.name);
            } else {
                const ListKind *k = list_kind_for(list_of(s->as.push_stmt.value->type));
                fprintf(out, "fy_list_%s_push(&%s, ", k->tag, s->as.push_stmt.name);
            }
            emit_expr(out, s->as.push_stmt.value);
            fprintf(out, ");\n");
            return;
        }
        case STMT_INDEX_ASSIGN:
            indent(out, level);
            if (s->as.index_assign_stmt.index->type == TYPE_STRING) {
                const MapKind *k = map_kind_for(map_of(s->as.index_assign_stmt.value->type));
                fprintf(out, "fy_map_%s_set(&%s, ", k->tag, s->as.index_assign_stmt.name);
                emit_expr(out, s->as.index_assign_stmt.index);
                fprintf(out, ", ");
                emit_expr(out, s->as.index_assign_stmt.value);
                fprintf(out, ");\n");
            } else if (type_is_struct(s->as.index_assign_stmt.value->type)) {
                const StructDecl *sd = struct_decl_for(s->as.index_assign_stmt.value->type);
                fprintf(out, "fy_list_struct_%s_set(&%s, ", sd->name, s->as.index_assign_stmt.name);
                emit_expr(out, s->as.index_assign_stmt.index);
                fprintf(out, ", ");
                emit_expr(out, s->as.index_assign_stmt.value);
                fprintf(out, ");\n");
            } else {
                const ListKind *k = list_kind_for(list_of(s->as.index_assign_stmt.value->type));
                fprintf(out, "fy_list_%s_set(&%s, ", k->tag, s->as.index_assign_stmt.name);
                emit_expr(out, s->as.index_assign_stmt.index);
                fprintf(out, ", ");
                emit_expr(out, s->as.index_assign_stmt.value);
                fprintf(out, ");\n");
            }
            return;
        case STMT_FIELD_ASSIGN:
            indent(out, level);
            emit_expr(out, s->as.field_assign_stmt.base);
            fprintf(out, ".%s = ", s->as.field_assign_stmt.field);
            emit_expr(out, s->as.field_assign_stmt.value);
            fprintf(out, ";\n");
            return;
        case STMT_SORT: {
            const ListKind *k = list_kind_for(s->as.sort_stmt.resolved_type);
            indent(out, level);
            fprintf(out, "fy_list_%s_sort(&%s);\n", k->tag, s->as.sort_stmt.name);
            return;
        }
        case STMT_REVERSE: {
            Type t = s->as.reverse_stmt.resolved_type;
            indent(out, level);
            if (type_is_list_of_struct(t)) {
                const StructDecl *sd = struct_decl_for(list_elem(t));
                fprintf(out, "fy_list_struct_%s_reverse(&%s);\n", sd->name, s->as.reverse_stmt.name);
            } else {
                const ListKind *k = list_kind_for(t);
                fprintf(out, "fy_list_%s_reverse(&%s);\n", k->tag, s->as.reverse_stmt.name);
            }
            return;
        }
        case STMT_REMOVE: {
            Type t = s->as.remove_stmt.resolved_type;
            indent(out, level);
            if (type_is_list_of_struct(t)) {
                const StructDecl *sd = struct_decl_for(list_elem(t));
                fprintf(out, "fy_list_struct_%s_remove(&%s, ", sd->name, s->as.remove_stmt.name);
            } else {
                const ListKind *k = list_kind_for(t);
                fprintf(out, "fy_list_%s_remove(&%s, ", k->tag, s->as.remove_stmt.name);
            }
            emit_expr(out, s->as.remove_stmt.index);
            fprintf(out, ");\n");
            return;
        }
    }
}

static void emit_signature(FILE *out, FunctionDecl *f, int uses_args) {
    char buf[256];
    if (strcmp(f->name, "main") == 0) {
        fprintf(out, uses_args ? "int main(int argc, char *argv[])" : "int main(void)");
        return;
    }
    fprintf(out, "%s %s(", c_type_name(f->return_type), c_func_name(f->name, buf, sizeof(buf)));
    if (f->param_count == 0) {
        fprintf(out, "void");
    } else {
        for (int i = 0; i < f->param_count; i++) {
            if (i > 0) fprintf(out, ", ");
            fprintf(out, "%s %s", c_type_name(f->params[i].type), f->params[i].name);
        }
    }
    fprintf(out, ")");
}

static void emit_function(FILE *out, FunctionDecl *f, int uses_args) {
    int in_main = strcmp(f->name, "main") == 0;
    emit_signature(out, f, uses_args);
    fprintf(out, "\n{\n");
    if (in_main && uses_args) {
        fprintf(out, "    fy_init_launch_args(argc, argv);\n");
    }
    for (int i = 0; i < f->body->as.block.count; i++) {
        emit_stmt(out, 1, f->body->as.block.stmts[i], in_main);
    }
    if (in_main) {
        fprintf(out, "    return 0;\n");
    }
    fprintf(out, "}\n\n");
}

int codegen_emit(Program *prog, const char *out_path, Diag *diag) {
    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        diag_set(diag, 0, "could not open '%s' for writing", out_path);
        return 0;
    }

    fprintf(out, "/* Generated by the Fractyne compiler. Do not edit by hand. */\n");
    fprintf(out, "#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#include <math.h>\n#include <ctype.h>\n#include <time.h>\n");
    if (prog->uses_sdl) fprintf(out, "#include <SDL2/SDL.h>\n");
    fprintf(out, "\n");
    fprintf(out, "static const char *fy_concat(const char *a, const char *b) __attribute__((unused));\n");
    fprintf(out, "static const char *fy_concat(const char *a, const char *b) {\n");
    fprintf(out, "    size_t la = strlen(a), lb = strlen(b);\n");
    fprintf(out, "    char *r = malloc(la + lb + 1);\n");
    fprintf(out, "    memcpy(r, a, la);\n");
    fprintf(out, "    memcpy(r + la, b, lb);\n");
    fprintf(out, "    r[la + lb] = '\\0';\n");
    fprintf(out, "    return r;\n");
    fprintf(out, "}\n\n");

    fprintf(out, "static const char *fy_string_char_at(const char *s, long i) __attribute__((unused));\n");
    fprintf(out, "static const char *fy_string_char_at(const char *s, long i) {\n");
    fprintf(out, "    long len = (long)strlen(s);\n");
    fprintf(out, "    if (i < 0 || i >= len) {\n");
    fprintf(out, "        fprintf(stderr, \"fractyne: runtime error: string index %%ld out of bounds (len %%ld)\\n\", i, len);\n");
    fprintf(out, "        exit(1);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    char *r = malloc(2);\n");
    fprintf(out, "    r[0] = s[i];\n");
    fprintf(out, "    r[1] = '\\0';\n");
    fprintf(out, "    return r;\n");
    fprintf(out, "}\n\n");

    emit_list_runtime(out);
    emit_map_runtime(out);
    emit_enum_typedefs(out, prog);
    emit_struct_typedefs(out, prog);
    emit_conversion_builtins(out);
    emit_shape_builtins(out);
    emit_math_builtins(out);
    emit_string_builtins(out);
    emit_random_builtins(out);
    emit_file_builtins(out);
    emit_args_builtins(out);
    if (prog->uses_sdl) emit_sdl_builtins(out);

    for (int i = 0; i < prog->global_count; i++) {
        Stmt *g = prog->globals[i];
        fprintf(out, "static %s%s %s = ",
                (g->as.let_stmt.is_fixed && g->as.let_stmt.resolved_type != TYPE_STRING) ? "const " : "",
                c_type_name(g->as.let_stmt.resolved_type), g->as.let_stmt.name);
        emit_expr(out, g->as.let_stmt.init);
        fprintf(out, ";\n");
    }
    if (prog->global_count > 0) fprintf(out, "\n");

    for (int i = 0; i < prog->count; i++) {
        FunctionDecl *f = prog->functions[i];
        if (strcmp(f->name, "main") == 0) continue;
        emit_signature(out, f, 0);
        fprintf(out, ";\n");
    }
    fprintf(out, "\n");

    for (int i = 0; i < prog->count; i++) {
        if (strcmp(prog->functions[i]->name, "main") == 0) continue;
        emit_function(out, prog->functions[i], 0);
    }
    for (int i = 0; i < prog->count; i++) {
        if (strcmp(prog->functions[i]->name, "main") == 0) {
            emit_function(out, prog->functions[i], prog->uses_args);
        }
    }

    fclose(out);
    return 1;
}
