#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ast.h"
#include "codegen.h"
#include "diag.h"
#include "involve.h"
#include "sema.h"

/* With out_dir NULL: derives "examples/hello.c" and "examples/hello" from
 * "examples/hello.fy", next to the source (used by `build`).
 * With out_dir given: derives "<out_dir>/hello.c" and "<out_dir>/hello"
 * instead, using just the source's basename (used by `run`, so it can
 * compile into a scratch directory and clean up afterward). */
static void derive_output_paths(const char *src_path, const char *out_dir,
                                 char *c_path, size_t c_path_size,
                                 char *bin_path, size_t bin_path_size) {
    const char *name = src_path;
    if (out_dir != NULL) {
        const char *slash = strrchr(src_path, '/');
        if (slash != NULL) name = slash + 1;
    }
    size_t len = strlen(name);
    size_t base_len = len;
    if (len > 3 && strcmp(name + len - 3, ".fy") == 0) base_len = len - 3;

    if (out_dir != NULL) {
        snprintf(bin_path, bin_path_size, "%s/%.*s", out_dir, (int)base_len, name);
        snprintf(c_path, c_path_size, "%s/%.*s.c", out_dir, (int)base_len, name);
    } else {
        snprintf(bin_path, bin_path_size, "%.*s", (int)base_len, name);
        snprintf(c_path, c_path_size, "%.*s.c", (int)base_len, name);
    }
}

static int run_gcc(const char *c_path, const char *bin_path) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fractyne: fork");
        return 0;
    }
    if (pid == 0) {
        execlp("gcc", "gcc", "-std=c11", "-Wall", "-Wextra", "-o", bin_path, c_path, "-lm", (char *)NULL);
        perror("fractyne: exec gcc");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("fractyne: waitpid");
        return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Prints a diagnostic, preferring the file it actually occurred in
 * (tracked by the multi-file loader for lex/parse-stage errors) and falling
 * back to the entry path for sema/codegen-stage errors, which can span more
 * than one file (e.g. the same function declared in two involved files). */
static void print_diag(const char *fallback_path, const Diag *diag) {
    const char *path = (diag->file[0] != '\0') ? diag->file : fallback_path;
    fprintf(stderr, "fractyne: %s:%d: error: %s\n", path, diag->line, diag->message);
}

/* Runs the full pipeline (load -> sema -> codegen -> gcc) for src_path,
 * writing the generated C to c_path and the binary to bin_path (both
 * already decided by the caller). Returns 1 on success; on failure prints a
 * diagnostic itself and returns 0. */
static int compile_program(const char *src_path, const char *c_path, const char *bin_path) {
    Diag diag;
    diag_init(&diag);

    Program *prog = load_program(src_path, &diag);
    if (prog == NULL) {
        print_diag(src_path, &diag);
        return 0;
    }

    if (!sema_check(prog, &diag)) {
        print_diag(src_path, &diag);
        program_free(prog);
        return 0;
    }

    if (!codegen_emit(prog, c_path, &diag)) {
        print_diag(src_path, &diag);
        program_free(prog);
        return 0;
    }

    program_free(prog);

    if (!run_gcc(c_path, bin_path)) {
        fprintf(stderr, "fractyne: gcc failed to compile the generated C for '%s'\n", src_path);
        return 0;
    }

    return 1;
}

static int cmd_build(const char *src_path) {
    char c_path[4096], bin_path[4096];
    derive_output_paths(src_path, NULL, c_path, sizeof(c_path), bin_path, sizeof(bin_path));
    if (!compile_program(src_path, c_path, bin_path)) return 1;
    printf("built %s\n", bin_path);
    return 0;
}

/* Executes an already-built binary, forwarding stdin/stdout/stderr, and
 * propagates its exit status (128+signal if it was killed by one). */
static int run_binary(const char *bin_path) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fractyne: fork");
        return 1;
    }
    if (pid == 0) {
        char *argv[] = {(char *)bin_path, NULL};
        execv(bin_path, argv);
        perror("fractyne: exec");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("fractyne: waitpid");
        return 1;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "fractyne: program terminated by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}

/* `run` compiles into a scratch directory under TMPDIR (or /tmp) and removes
 * it afterward, regardless of outcome -- unlike `build`, it's meant to feel
 * like just executing the source, leaving nothing behind next to it. */
static int cmd_run(const char *src_path) {
    const char *tmp_base = getenv("TMPDIR");
    if (tmp_base == NULL || tmp_base[0] == '\0') tmp_base = "/tmp";

    char tmpl[4096];
    snprintf(tmpl, sizeof(tmpl), "%s/fractyne-XXXXXX", tmp_base);
    if (mkdtemp(tmpl) == NULL) {
        perror("fractyne: mkdtemp");
        return 1;
    }

    char c_path[4096], bin_path[4096];
    derive_output_paths(src_path, tmpl, c_path, sizeof(c_path), bin_path, sizeof(bin_path));

    int result = compile_program(src_path, c_path, bin_path) ? run_binary(bin_path) : 1;

    remove(c_path);
    remove(bin_path);
    rmdir(tmpl);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "build") != 0 && strcmp(argv[1], "run") != 0)) {
        fprintf(stderr, "usage: fractyne build <file.fy>\n       fractyne run <file.fy>\n");
        return 1;
    }
    if (strcmp(argv[1], "build") == 0) return cmd_build(argv[2]);
    return cmd_run(argv[2]);
}
