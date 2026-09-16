#include "builtins.h"

#include <string.h>

static const Builtin BUILTINS[] = {
    {"int_to_string", 1, {TYPE_INT}, TYPE_STRING, 0},
    {"float_to_string", 1, {TYPE_FLOAT}, TYPE_STRING, 0},
    {"bool_to_string", 1, {TYPE_BOOL}, TYPE_STRING, 0},
    {"string_to_int", 1, {TYPE_STRING}, TYPE_INT, 0},
    {"string_to_float", 1, {TYPE_STRING}, TYPE_FLOAT, 0},
    {"int_to_float", 1, {TYPE_INT}, TYPE_FLOAT, 0},
    {"float_to_int", 1, {TYPE_FLOAT}, TYPE_INT, 0},

    {"draw_square", 1, {TYPE_INT}, TYPE_VOID, 0},
    {"draw_rect", 2, {TYPE_INT, TYPE_INT}, TYPE_VOID, 0},
    {"draw_triangle", 1, {TYPE_INT}, TYPE_VOID, 0},

    {"sqrt", 1, {TYPE_FLOAT}, TYPE_FLOAT, 0},
    {"pow", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT, 0},
    {"abs_int", 1, {TYPE_INT}, TYPE_INT, 0},
    {"abs_float", 1, {TYPE_FLOAT}, TYPE_FLOAT, 0},
    {"min_int", 2, {TYPE_INT, TYPE_INT}, TYPE_INT, 0},
    {"max_int", 2, {TYPE_INT, TYPE_INT}, TYPE_INT, 0},
    {"min_float", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT, 0},
    {"max_float", 2, {TYPE_FLOAT, TYPE_FLOAT}, TYPE_FLOAT, 0},

    {"input", 0, {0}, TYPE_STRING, 0},

    {"substring", 3, {TYPE_STRING, TYPE_INT, TYPE_INT}, TYPE_STRING, 0},
    {"to_upper", 1, {TYPE_STRING}, TYPE_STRING, 0},
    {"to_lower", 1, {TYPE_STRING}, TYPE_STRING, 0},
    {"trim", 1, {TYPE_STRING}, TYPE_STRING, 0},
    {"index_of", 2, {TYPE_STRING, TYPE_STRING}, TYPE_INT, 0},

    {"window_open", 3, {TYPE_INT, TYPE_INT, TYPE_STRING}, TYPE_BOOL, 1},
    {"window_close", 0, {0}, TYPE_VOID, 1},
    {"window_should_close", 0, {0}, TYPE_BOOL, 1},
    {"window_poll_events", 0, {0}, TYPE_VOID, 1},
    {"window_clear", 3, {TYPE_INT, TYPE_INT, TYPE_INT}, TYPE_VOID, 1},
    {"window_present", 0, {0}, TYPE_VOID, 1},

    {"gfx_set_color", 3, {TYPE_INT, TYPE_INT, TYPE_INT}, TYPE_VOID, 1},
    {"gfx_pixel", 2, {TYPE_INT, TYPE_INT}, TYPE_VOID, 1},
    {"gfx_line", 4, {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT}, TYPE_VOID, 1},
    {"gfx_rect", 4, {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT}, TYPE_VOID, 1},
    {"gfx_circle", 3, {TYPE_INT, TYPE_INT, TYPE_INT}, TYPE_VOID, 1},

    {"key_down", 1, {TYPE_STRING}, TYPE_BOOL, 1},
    {"mouse_x", 0, {0}, TYPE_INT, 1},
    {"mouse_y", 0, {0}, TYPE_INT, 1},
    {"mouse_down", 1, {TYPE_STRING}, TYPE_BOOL, 1},

    {"delay_ms", 1, {TYPE_INT}, TYPE_VOID, 1},
    {"ticks_ms", 0, {0}, TYPE_INT, 1},

    {"random", 0, {0}, TYPE_FLOAT, 0},
    {"random_int", 2, {TYPE_INT, TYPE_INT}, TYPE_INT, 0},
    {"random_seed", 1, {TYPE_INT}, TYPE_VOID, 0},

    {"read_file", 1, {TYPE_STRING}, TYPE_STRING, 0},
    {"write_file", 2, {TYPE_STRING, TYPE_STRING}, TYPE_BOOL, 0},
    {"append_file", 2, {TYPE_STRING, TYPE_STRING}, TYPE_BOOL, 0},
    {"file_exists", 1, {TYPE_STRING}, TYPE_BOOL, 0},

    {"launch_args", 0, {0}, TYPE_LIST_STRING, 0},
};
#define BUILTIN_COUNT (sizeof(BUILTINS) / sizeof(BUILTINS[0]))

const Builtin *find_builtin(const char *name) {
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return NULL;
}
