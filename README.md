# Fractyne

Fractyne is a small statically-typed language whose compiler is written entirely in C. It
compiles `.fy` source to C, then invokes `gcc` to produce a native binary — no Python, no
interpreter, no runtime dependency beyond a C compiler.

## Install

```sh
./install.sh
```

Builds Fractyne and installs it to `~/.local/bin/fractyne`, adding that directory to your
`PATH` (in `~/.bashrc` or `~/.zshrc`) if it isn't already there. Safe to re-run any time —
it just rebuilds and reinstalls. Works on Linux and macOS.

**Requirements**: a C compiler (`gcc` or `cc`) and `make`, already installed. Fractyne
compiles `.fy` source to C and invokes a real C compiler to finish the job, so this is a
permanent dependency, not just a build-time one — every machine that runs Fractyne programs
needs one, not just the machine building the compiler. The installer checks for both and
prints the right install command for your OS if either is missing (`xcode-select --install`
on macOS, or your Linux distro's `build-essential`/`gcc make`/`base-devel` package).

**Windows**: Fractyne doesn't run natively on Windows (no `fork()`, no `gcc` by default).
Install WSL first (`wsl --install` from an elevated PowerShell), then run `install.sh`
inside the WSL shell — from there it's a normal Linux environment.

**On another machine without a local checkout**: set `FRACTYNE_GIT_URL` to wherever this
repo is hosted, and `install.sh` clones it first:
```sh
FRACTYNE_GIT_URL=<repo-url> sh install.sh
```

## Build the compiler

```sh
make
```

Produces `bin/fractyne`. (`install.sh` does this and copies the result to `~/.local/bin`
for you — use this directly only if you want the binary to stay inside the repo.)

## Compile a program

```sh
bin/fractyne build examples/hello.fy
./examples/hello
```

`fractyne build path/to/file.fy` writes `path/to/file.c` (the generated C) and
`path/to/file` (the native binary) next to the source.

## Run a program in one step

```sh
bin/fractyne run examples/hello.fy
```

Compiles into a scratch directory under `$TMPDIR` (or `/tmp`), runs it, forwards its
stdin/stdout/stderr and exit code, then deletes the scratch directory — unlike `build`,
this leaves nothing behind next to your source. Put `bin/` on your `PATH` to use this from
anywhere: `fractyne run whatever.fy`.

## Run the test suite

```sh
make test
```

Builds every example that has a matching `.expected` file and diffs its output.

## Language (v1)

- Types: `int`, `float`, `bool`, `string`, `list<int|float|bool|string>`,
  `map<string, int|float|bool|string>`
- `let name = expr;` — type is inferred from the initializer and fixed thereafter
- `name = expr;` — reassignment, must match the variable's type; also `+= -= *= /= %=`,
  each desugared to `name = name <op> expr` at parse time
- Arithmetic `+ - * / %` on `int`/`float`; `+` also concatenates `string`
- Comparisons `== != < > <= >=`, logical `&& || !`. `==`/`!=` only work on `int`/`float`/
  `bool`/`string` — a list, map, or struct has no built-in equality; compare its
  fields/elements individually instead
- `cond ? a : b` — ternary; `cond` must be `bool`, and both branches must be the same type
- `output(expr);`
- `if (cond) { ... } else if (cond) { ... } else { ... }`
- `while (cond) { ... }`
- `for (let i = 0; cond; i = i + 1) { ... }` — C-style; init is `let` or a plain assignment
  (either can use a compound-assign operator), the loop variable is scoped to the loop
- `break;` / `continue;` — only valid inside a `while` or `for` body
- `fn name(param: type, ...) -> type { ... }` — recursion allowed; omit `-> type` for a
  function that returns nothing
- Every program needs exactly one `fn main() { ... }` (no parameters, no return type) as
  the entry point
- `let NAME = literal;` at the top level — a global variable, visible to every function;
  the initializer must be a literal (or `-literal`), not an expression, so codegen can emit
  it as a plain C global with no static-initializer complications. Despite the name, it's
  an ordinary mutable variable (`NAME = expr;` works from any function), not a true constant
- `//` line comments, `/* block comments */` (no nesting)

Splitting a program across files:

- `involve "relative/path.fy";` at the top level — merges that file's structs, functions,
  and globals into this program. The path is resolved relative to the file containing the
  `involve`, not the current directory
- There are no namespaces or qualified access (`file.thing()`) — it's closer to C's
  `#include` than a module system: every involved file's declarations land in one flat,
  shared namespace, so involving two files that both declare the same name is a compile
  error, same as declaring it twice in one file
- The same file is only ever merged once, even if several files involve it, or it's
  involved transitively through a chain, or involving is cyclic (`a` involves `b` which
  involves `a`) — no include guards needed
- Exactly one `fn main()` must exist across every involved file combined, not per file

Lists:

- `[e1, e2, ...]` — a list literal; element type is inferred from the first element and
  every other element must match it (empty literals aren't allowed — there's nothing to infer)
- `xs[i]` — read an element; `xs[i] = v;` — write one (both need an `int` index); both are
  bounds-checked at runtime and exit with a clear error on an out-of-range index, rather
  than reading or corrupting memory silently
- `len(xs)` — element count; also works on `string` (byte length)
- `push(xs, v);` — appends `v` (must match the list's element type) to variable `xs`
- Lists are passed by value like everything else: `push`/`xs[i] = v` inside a function only
  mutate that function's own copy, not the caller's list
- `list<T>` allows a struct as `T` too (`list<Point>`), including a struct field whose type
  is a list of another struct — declaration order between the two doesn't matter. A list can't
  hold another list or a map, though (no `list<list<int>>`)

Maps (keys are always `string`, values must be `int`/`float`/`bool`/`string` — not a struct):

- `{k1: v1, k2: v2, ...}` — a map literal; value type is inferred from the first value and
  every other value must match it (empty literals aren't allowed — there's nothing to infer)
- `m[key]` — read a value, or a zero value (`0`, `0.0`, `false`, `""`) if the key isn't
  present; `m[key] = v;` — insert or overwrite (unlike lists, this doesn't require the key
  to already exist)
- `len(m)` — number of entries
- Backed by a simple linear-scan association array, not a hash table — fine at small sizes,
  not chosen for lookup performance

Structs:

- `struct Name { field: type, ... }` at the top level, alongside `fn` declarations — can be
  declared in any order relative to where they're used, including fields of other structs
- `Name { field: value, ... }` — a literal; must set every declared field exactly once, in
  any order (not just declaration order)
- `value.field` — read; `name.field = value;` — write (the target must be a plain variable,
  not a nested expression — `a.b.c = 1` isn't supported, only reading nested fields is)
- Structs are ordinary value types like everything else: passed by value, fields can be any
  type including another struct or a list/map, but not a list/map of structs

Type conversion builtins (ordinary function-call syntax, resolved before user functions):
`int_to_string`, `float_to_string`, `bool_to_string`, `string_to_int`, `string_to_float`,
`int_to_float`, `float_to_int`.

Shape builtins — print ASCII art to stdout; there's no windowing dependency in this
project, so these draw with `*` rather than opening a window:
`draw_square(n)`, `draw_rect(w, h)`, `draw_triangle(n)`.

Math builtins: `sqrt`, `pow` (both `float`), `abs_int`, `abs_float`, `min_int`, `max_int`,
`min_float`, `max_float`.

String builtins: `s[i]` reads the character at `i` as a length-1 `string` (there's no
dedicated char type), bounds-checked at runtime like list indexing; strings are otherwise
immutable — `s[i] = ...` is rejected at compile time. Also `substring(s, start, length)`
(clamps out-of-range `start`/`length` instead of erroring — unlike `s[i]`), `to_upper`,
`to_lower`, `trim`, and `index_of(s, needle)` (returns `-1` if not found).

I/O: `input()` reads one line from stdin (no trailing newline) and returns it as a
`string`; at EOF it returns `""`. Not covered by `make test`, since the harness doesn't
feed stdin to each example — try it directly, e.g. `echo Ada | ./examples/some_program`.

See `examples/` for sample programs covering each feature.
