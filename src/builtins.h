#ifndef FRACTYNE_BUILTINS_H
#define FRACTYNE_BUILTINS_H

#include "ast.h"

/* Builtins are called with ordinary function-call syntax and resolved here
 * instead of against user FunctionDecls. Codegen emits calls to them the
 * same way as user functions (fy_<name>(...)), so each entry needs a
 * matching fy_<name> implementation in codegen's preamble. */
#define BUILTIN_MAX_PARAMS 3

typedef struct {
    const char *name;
    int param_count;
    Type params[BUILTIN_MAX_PARAMS];
    Type return_type;
} Builtin;

const Builtin *find_builtin(const char *name);

#endif
