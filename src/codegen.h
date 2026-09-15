#ifndef FRACTYNE_CODEGEN_H
#define FRACTYNE_CODEGEN_H

#include "ast.h"
#include "diag.h"

/* Emits C source for the (already sema-checked) program to out_path.
 * Returns 1 on success, 0 on failure (diag is set). */
int codegen_emit(Program *prog, const char *out_path, Diag *diag);

#endif
