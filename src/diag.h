#ifndef FRACTYNE_DIAG_H
#define FRACTYNE_DIAG_H

/* Shared error-reporting mechanism used by every compiler stage.
 * First error wins: once has_error is set, later diag_set() calls are
 * ignored so callers don't have to guard every check to avoid cascades.
 *
 * current_file is a borrowed pointer the multi-file loader updates to
 * "whichever file is being lexed/parsed right now" before touching each
 * file; diag_set() copies it into `file` at the moment the first error
 * fires, so lex/parse errors are attributed to the actual file they're in
 * even when involve pulls in several. It's cleared back to NULL once
 * loading finishes, so a later sema/codegen error (which can span more than
 * one file, e.g. a duplicate declaration) leaves `file` empty and the
 * caller falls back to naming the entry file instead of guessing. */
typedef struct {
    int has_error;
    int line;
    char message[256];
    const char *current_file;
    char file[4096];
} Diag;

void diag_init(Diag *d);
void diag_set(Diag *d, int line, const char *fmt, ...);

#endif
