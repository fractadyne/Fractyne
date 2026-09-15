#ifndef FRACTYNE_INVOLVE_H
#define FRACTYNE_INVOLVE_H

#include "ast.h"
#include "diag.h"

/* Reads entry_path and follows every `involve "...";` statement, each path
 * resolved relative to the file that contains it. The same file is only
 * ever loaded once even if several files involve it (or it involves itself,
 * directly or through a cycle). Every involved file's structs, functions,
 * and globals are merged into one flat Program -- involve has no namespaces,
 * it's closer to C's #include than a module system.
 *
 * Returns NULL on failure; diag is set, with diag->file naming the actual
 * file a lex/parse error is in when that differs from entry_path. */
Program *load_program(const char *entry_path, Diag *diag);

#endif
