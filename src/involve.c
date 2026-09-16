#define _DEFAULT_SOURCE /* realpath */

#include "involve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

typedef struct {
    char *path; /* canonical absolute path, owned */
    TokenList tokens;
} FileUnit;

typedef struct {
    char *path; /* borrowed: Token.text of the string literal */
    int line;
} InvolveRef;

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static int already_visited(PtrList *visited, const char *canonical) {
    for (int i = 0; i < visited->count; i++) {
        if (strcmp((char *)visited->items[i], canonical) == 0) return 1;
    }
    return 0;
}

/* Resolves the path written in `involve "raw_path";` relative to the
 * directory of from_file, which is always already a canonical absolute
 * path, so it always contains at least one '/'. */
static void resolve_relative(const char *from_file, const char *raw_path, char *out, size_t out_size) {
    const char *slash = strrchr(from_file, '/');
    size_t dir_len = (slash != NULL) ? (size_t)(slash - from_file) + 1 : 0;
    snprintf(out, out_size, "%.*s%s", (int)dir_len, from_file, raw_path);
}

static void collect_involve_refs(TokenList *tokens, PtrList *refs) {
    for (int i = 0; i + 1 < tokens->count; i++) {
        if (tokens->tokens[i].type == TOK_INVOLVE && tokens->tokens[i + 1].type == TOK_STRING_LIT) {
            InvolveRef *ref = malloc(sizeof(InvolveRef));
            ref->path = tokens->tokens[i + 1].text;
            ref->line = tokens->tokens[i].line;
            ptrlist_push(refs, ref);
        }
    }
}

/* Depth-first file discovery: reads and lexes `path` (skipping it if it's
 * already been visited, directly or via a cycle), appends it to `files`,
 * then recurses into whatever it involves. `from`/`from_line` name the
 * involve statement that led here, for error messages -- both are NULL/0
 * for the entry file itself. */
static int discover(const char *path, const char *from, int from_line,
                     PtrList *visited, PtrList *files, Diag *diag) {
    char canonical[4096];
    if (realpath(path, canonical) == NULL) {
        diag->current_file = from;
        if (from != NULL) {
            diag_set(diag, from_line, "could not find involved file '%s'", path);
        } else {
            diag_set(diag, 0, "could not find '%s'", path);
        }
        return 0;
    }
    if (already_visited(visited, canonical)) return 1;
    ptrlist_push(visited, dup_str(canonical));

    char *source = read_file(canonical);
    if (source == NULL) {
        diag->current_file = from;
        diag_set(diag, from_line, "could not read '%s'", canonical);
        return 0;
    }

    FileUnit *fu = malloc(sizeof(FileUnit));
    fu->path = dup_str(canonical);

    diag->current_file = fu->path;
    int ok = lex(source, &fu->tokens, diag);
    free(source);
    if (!ok) {
        free(fu->path);
        free(fu);
        return 0;
    }

    ptrlist_push(files, fu);

    PtrList refs;
    ptrlist_init(&refs);
    collect_involve_refs(&fu->tokens, &refs);
    int result = 1;
    for (int i = 0; i < refs.count; i++) {
        InvolveRef *ref = (InvolveRef *)refs.items[i];
        char resolved[4096];
        resolve_relative(fu->path, ref->path, resolved, sizeof(resolved));
        if (!discover(resolved, fu->path, ref->line, visited, files, diag)) {
            result = 0;
            break;
        }
    }
    for (int i = 0; i < refs.count; i++) free(refs.items[i]);
    free(refs.items);
    return result;
}

static void free_files(PtrList *files) {
    for (int i = 0; i < files->count; i++) {
        FileUnit *fu = (FileUnit *)files->items[i];
        token_list_free(&fu->tokens);
        free(fu->path);
        free(fu);
    }
    free(files->items);
}

Program *load_program(const char *entry_path, Diag *diag) {
    PtrList visited, files;
    ptrlist_init(&visited);
    ptrlist_init(&files);

    if (!discover(entry_path, NULL, 0, &visited, &files, diag)) {
        free_files(&files);
        for (int i = 0; i < visited.count; i++) free(visited.items[i]);
        free(visited.items);
        return NULL;
    }

    PtrList struct_names, enum_names;
    ptrlist_init(&struct_names);
    ptrlist_init(&enum_names);
    for (int i = 0; i < files.count; i++) {
        collect_struct_names(&((FileUnit *)files.items[i])->tokens, &struct_names);
        collect_enum_names(&((FileUnit *)files.items[i])->tokens, &enum_names);
    }

    Program *prog = program_new();
    int ok = 1;
    for (int i = 0; i < files.count && ok; i++) {
        FileUnit *fu = (FileUnit *)files.items[i];
        diag->current_file = fu->path;
        ok = parse_file_into_program(&fu->tokens, &struct_names, &enum_names, prog, diag);
    }
    diag->current_file = NULL; /* sema/codegen errors can span files; fall back to the entry path */

    free_files(&files);
    free(struct_names.items);
    free(enum_names.items);
    for (int i = 0; i < visited.count; i++) free(visited.items[i]);
    free(visited.items);

    if (!ok) {
        program_free(prog);
        return NULL;
    }
    return prog;
}
