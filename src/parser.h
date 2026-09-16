#ifndef FRACTYNE_PARSER_H
#define FRACTYNE_PARSER_H

#include "ast.h"
#include "diag.h"
#include "lexer.h"

/* Collects every struct name declared anywhere in tokens (a lightweight
 * pre-scan so struct types can be referenced before their own declaration).
 * Appends to names; the caller inits and frees the list. */
void collect_struct_names(TokenList *tokens, PtrList *names);

/* Same idea as collect_struct_names, for `enum Name { ... }` declarations --
 * needed up front so `Name.Member` can be told apart from a plain variable
 * followed by a field access during parsing. */
void collect_enum_names(TokenList *tokens, PtrList *names);

/* Parses one file's top-level declarations into prog, using struct/enum-name
 * lists the caller already built. Returns 1 on success, 0 on failure (diag
 * is set). See involve.c for the multi-file driver that calls this once per
 * involved file. */
int parse_file_into_program(TokenList *tokens, PtrList *struct_names, PtrList *enum_names,
                             Program *prog, Diag *diag);

#endif
