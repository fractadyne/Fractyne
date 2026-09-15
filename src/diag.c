#include "diag.h"

#include <stdarg.h>
#include <stdio.h>

void diag_init(Diag *d) {
    d->has_error = 0;
    d->line = 0;
    d->message[0] = '\0';
    d->current_file = NULL;
    d->file[0] = '\0';
}

void diag_set(Diag *d, int line, const char *fmt, ...) {
    if (d->has_error) return;
    d->has_error = 1;
    d->line = line;
    if (d->current_file != NULL) {
        snprintf(d->file, sizeof(d->file), "%s", d->current_file);
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, args);
    va_end(args);
}
