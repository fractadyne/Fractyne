#ifndef FRACTYNE_SEMA_H
#define FRACTYNE_SEMA_H

#include "ast.h"
#include "diag.h"

/* Type-checks and annotates the AST in place (Expr.type, let_stmt.resolved_type).
 * Returns 1 on success, 0 on failure (diag is set). */
int sema_check(Program *prog, Diag *diag);

#endif
