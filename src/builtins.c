#include "builtins.h"

#include <string.h>

static const Builtin BUILTINS[] = {
    {"int_to_string", 1, {TYPE_INT}, TYPE_STRING},
    {"float_to_string", 1, {TYPE_FLOAT}, TYPE_STRING},
    {"bool_to_string", 1, {TYPE_BOOL}, TYPE_STRING},
    {"string_to_int", 1, {TYPE_STRING}, TYPE_INT},
    {"string_to_float", 1, {TYPE_STRING}, TYPE_FLOAT},
    {"int_to_float", 1, {TYPE_INT}, TYPE_FLOAT},
    {"float_to_int", 1, {TYPE_FLOAT}, TYPE_INT},

    {"draw_square", 1, {TYPE_INT}, TYPE_VOID},
    {"draw_rect", 2, {TYPE_INT, TYPE_INT}, TYPE_VOID},
    {"draw_triangle", 1, {TYPE_INT}, TYPE_VOID},

    {"sqrt", 1, {TYPE_FLOAT}, TYPE_FLOAT},
    {"pow", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT},
    {"abs_int", 1, {TYPE_INT}, TYPE_INT},
    {"abs_float", 1, {TYPE_FLOAT}, TYPE_FLOAT},
    {"min_int", 2, {TYPE_INT, TYPE_INT}, TYPE_INT},
    {"max_int", 2, {TYPE_INT, TYPE_INT}, TYPE_INT},
    {"min_float", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT},
    {"max_float", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT},

    {"input", 0, {0}, TYPE_STRING},

    {"substring", 3, {TYPE_STRING, TYPE_INT, TYPE_INT}, TYPE_STRING},
    {"to_upper", 1, {TYPE_STRING}, TYPE_STRING},
    {"to_lower", 1, {TYPE_STRING}, TYPE_STRING},
    {"trim", 1, {TYPE_STRING}, TYPE_STRING},
    {"index_of", 2, {TYPE_STRING, TYPE_STRING}, TYPE_INT},
};
#define BUILTIN_COUNT (sizeof(BUILTINS) / sizeof(BUILTINS[0]))

const Builtin *find_builtin(const char *name) {
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return NULL;
}
