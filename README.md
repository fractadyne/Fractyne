# Fractyne

Fractyne is a small statically-typed language whose compiler is written entirely in C. It
compiles `.fy` source to C, then invokes `gcc` to produce a native binary — no Python, no
interpreter, no runtime dependency beyond a C compiler (SDL2 is needed too, but only for
programs that use the window/graphics builtins — see below).

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

Anything after the source file is forwarded to the compiled program itself, readable from
inside the program with `launch_args()`: `fractyne run whatever.fy foo bar` runs the program
with `launch_args()` returning `["foo", "bar"]`. A binary built with `build` takes its own
arguments directly (`./whatever foo bar`) — no forwarding needed since there's no wrapper
in the way.

## Run the test suite

```sh
make test
```

Builds every example that has a matching `.expected` file and diffs its output.

## Language (v1)

- Types: `int`, `float`, `bool`, `string`, `list<int|float|bool|string>`,
  `map<string, int|float|bool|string>`
- `let name = expr;` — type is inferred from the initializer and fixed thereafter
- `fixed name = expr;` — same as `let`, except the binding itself can never change again:
  no `name = ...`, `+=`-style compound assign, `++`/`--`, `push`/`sort`/`reverse`/`remove`
  on it if it's a list, or writing into one of its fields if it's a struct. Reading it and
  passing it around works exactly like `let`
- `name = expr;` — reassignment, must match the variable's type; also `+= -= *= /= %=`,
  each desugared to `name = name <op> expr` at parse time
- `name++;` / `name--;` — `int`-only shorthand for `name = name + 1;` / `name = name - 1;`.
  Statement-only sugar, not an expression (no pre/post-increment value to argue about) —
  usable anywhere a statement is, including a for-loop's step clause
- Arithmetic `+ - * / %` on `int`/`float`; `+` also concatenates `string`
- Comparisons `== != < > <= >=`, logical `&& || !`. `==`/`!=` only work on `int`/`float`/
  `bool`/`string`/an enum — a list, map, or struct has no built-in equality; compare its
  fields/elements individually instead
- Bitwise `& | ^ ~ << >>` on `int` only, same precedence as C (`&`/`^`/`|` sit between `&&`
  and `==`; `<<`/`>>` sit between relational comparisons and `+`/`-`); also `&= |= ^= <<= >>=`
- `cond ? a : b` — ternary; `cond` must be `bool`, and both branches must be the same type
- `output(expr, ...);` — one or more comma-separated values, printed space-separated with a
  single trailing newline (not one newline per value)
- `if (cond) { ... } else if (cond) { ... } else { ... }`
- `branch (expr) { case v1, v2 { ... } case v3 { ... } else { ... } }` — compares `expr`
  against each case's value(s) with `==` in order, running the first block that matches (a
  comma-separated case matches on any of its values); `else` is optional and runs if nothing
  matched. Pure sugar for an if/else-if/else chain (desugared at parse time into one), so the
  same `==` rules apply — case values must match `expr`'s type, and branching on a list, map,
  or struct is rejected the same way comparing them with `==` already is. No fallthrough
  between cases. Not a loop: `break`/`continue` inside a case body only affect an actual
  enclosing loop, not the branch itself
- `while (cond) { ... }`
- `for (let i = 0; cond; i = i + 1) { ... }` — C-style; init is `let` or a plain assignment
  (either can use a compound-assign operator), the loop variable is scoped to the loop
- `for (let x in xs) { ... }` — iterates a list's elements, or a string's characters, one
  per iteration; pure sugar for the C-style form above (desugared at parse time into it, so
  `break`/`continue` work the same way)
- `break;` / `continue;` — only valid inside a `while` or `for` body
- `fr name(param: type, ...) -> type { ... }` — recursion allowed; omit `-> type` for a
  function that returns nothing
- Every program needs exactly one `fr main() { ... }` (no parameters, no return type) as
  the entry point
- `let NAME = literal;` (or `fixed NAME = literal;`) at the top level — a global variable,
  visible to every function; the initializer must be a literal (or `-literal`), not an
  expression, so codegen can emit it as a plain C global with no static-initializer
  complications. A `let` global is an ordinary mutable variable (`NAME = expr;` works from
  any function) despite the name — use `fixed` for an actual global constant
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
- Exactly one `fr main()` must exist across every involved file combined, not per file

Lists:

- `[e1, e2, ...]` — a list literal; element type is inferred from the first element and
  every other element must match it (empty literals aren't allowed — there's nothing to infer)
- `xs[i]` — read an element; `xs[i] = v;` — write one (both need an `int` index); both are
  bounds-checked at runtime and exit with a clear error on an out-of-range index, rather
  than reading or corrupting memory silently
- `xs[start:end]` — a new list copied from index `start` (inclusive) up to `end` (exclusive);
  out-of-range or reversed bounds clamp instead of erroring (`xs[2:1000]` just goes to the
  end, `xs[-5:3]` clamps `start` to `0`, `end < start` gives an empty list) — same spirit as
  `substring`. Works on any list, including `list<SomeStruct>`. Both bounds are required —
  there's no `xs[:3]`/`xs[2:]` shorthand for an implied `0`/`len(xs)`; write `len(xs)`
  explicitly for an open-ended slice
- `len(xs)` — element count; also works on `string` (byte length)
- `push(xs, v);` — appends `v` (must match the list's element type) to variable `xs`
- `reverse(xs);`, `remove(xs, i);` (bounds-checked, shifts later elements down) — work on any
  list, including `list<SomeStruct>`
- `sort(xs);`, `contains(xs, v) -> bool` — only for `int`/`float`/`bool`/`string` element
  lists; rejected at compile time for a list of structs, since structs have no defined
  ordering or equality (same reasoning as `==` below)
- Lists are passed by value like everything else: `push`/`sort`/`reverse`/`remove`/
  `xs[i] = v` inside a function only mutate that function's own copy, not the caller's list
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
- `contains(m, key) -> bool` — true if `key` is actually present (distinguishes a real entry
  from `m[key]`'s zero-value fallback on a missing key)
- `keys(m) -> list<string>` — every key, in no particular order
- Backed by a simple linear-scan association array, not a hash table — fine at small sizes,
  not chosen for lookup performance

Structs:

- `struct Name { field: type, ... }` at the top level, alongside `fr` declarations — can be
  declared in any order relative to where they're used, including fields of other structs
- `Name { field: value, ... }` — a literal; must set every declared field exactly once, in
  any order (not just declaration order)
- `value.field` — read; `target.field = value;` — write, where `target` can itself be a
  field chain (`a.b.c = 1` writes to `c` on `a.b`, working through any depth of nested structs)
- Structs are ordinary value types like everything else: passed by value, fields can be any
  type including another struct, enum, or a list/map, but not a list/map of structs

Method-call syntax — there's no separate method/`impl` declaration; `recv.name(args)` is
sugar for `name(recv, args)`, so any ordinary `fr` function can be called this way as long as
its first parameter's type matches `recv`'s:

```
fr move(p: Point, dx: int, dy: int) -> Point {
    return Point { x: p.x + dx, y: p.y + dy };
}

let moved = p.move(3, 4);       // same as move(p, 3, 4)
let chained = p.move(1, 0).move(0, 1); // chains, since move returns a Point too
```

Not limited to structs — `n.double()` works too, if `double(n: int) -> int` exists. Never
ambiguous with a plain field read: Fractyne has no function-valued fields to call through, so
`recv.name` followed by `(` always means a call, and followed by anything else always means a
field read.

Enums:

- `enum Name { Member1, Member2, ... }` at the top level — can be declared in any order
  relative to where it's used, same as structs
- `Name.Member` — refers to one member; usable anywhere a value is expected (`let`,
  function params/returns, struct fields, `output()`)
- `==`/`!=` work between two values of the *same* enum (comparing values of two different
  enums is a compile error, same as any other type mismatch); no ordering (`< > <= >=`) and
  no `list<SomeEnum>` yet
- `output(x)` on an enum value prints the member's own name (e.g. `Red`), not a number
- Unlike structs, an enum can't be used as a list element yet

Type conversion builtins (ordinary function-call syntax, resolved before user functions):
`int_to_string`, `float_to_string`, `bool_to_string`, `string_to_int`, `string_to_float`,
`int_to_float`, `float_to_int`.

Shape builtins — print ASCII art to stdout, no window or SDL2 needed:
`draw_square(n)`, `draw_rect(w, h)`, `draw_triangle(n)`. (For an actual window with actual
shapes, see "Windows and graphics" below.)

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

Random numbers: `random() -> float` (`0.0` up to but not including `1.0`), `random_int(lo,
hi) -> int` (inclusive both ends), `random_seed(seed);` for reproducible output — the RNG
auto-seeds from the current time on first use if you never call this. See `examples/random.fy`.

File I/O: `read_file(path) -> string` (returns `""` if the file doesn't exist or can't be
read — no exceptions in Fractyne, so this mirrors `input()`'s EOF behavior rather than
crashing), `write_file(path, content) -> bool` (overwrites; `bool` reports success),
`append_file(path, content) -> bool`, `file_exists(path) -> bool`. See `examples/files.fy`.

`launch_args() -> list<string>` — the arguments the program was launched with (not including
the program's own name/path). See `examples/args.fy`, and "Run a program in one step" above
for how `fractyne run` forwards its own trailing arguments into this.

## Windows and graphics

Real windows, not ASCII art — backed by SDL2. This is the one part of Fractyne with a
dependency beyond a C compiler, and it's opt-in: `fractyne` only adds `#include <SDL2/SDL.h>`
and links `-lSDL2` into a program's generated C if that program actually calls one of these
builtins, so every other program (including everything else in `examples/`) stays exactly as
dependency-free as before. If you use one of these, install SDL2's development package first
(`libsdl2-dev` on Debian/Ubuntu, `sdl2` on Arch, `SDL2-devel` on Fedora, or `brew install sdl2`
on macOS) — `fractyne` will tell you if gcc can't find it.

There's a single window at a time (no handle/pointer type to address more than one), and
colors are always `int` 0–255 per channel:

- `window_open(width, height, title) -> bool` — returns `false` if it couldn't open a window
  (e.g. no display available); `window_close()`
- `window_should_close() -> bool` — true once the user clicks the window's close button;
  `window_poll_events();` — pumps input/close events, call this once per frame
- `window_clear(r, g, b);` — fill the window with a color; `window_present();` — show what's
  been drawn since the last clear (drawing is double-buffered, so nothing appears until this
  is called)
- `gfx_set_color(r, g, b);` sets the color used by every draw call below, until changed again
- `gfx_pixel(x, y);`, `gfx_line(x1, y1, x2, y2);`, `gfx_rect(x, y, w, h);` (filled),
  `gfx_circle(cx, cy, radius);` (filled)
- `key_down(name) -> bool` — e.g. `"escape"`, `"a"`, `"up"`, `"space"` (SDL scancode names);
  `mouse_x() -> int`, `mouse_y() -> int`, `mouse_down(button) -> bool` — `"left"`, `"right"`,
  or `"middle"`
- `delay_ms(ms);` — pace the frame rate; `ticks_ms() -> int` — milliseconds since program start

See `examples/window.fy` for a small animated demo. It's not part of `make test` (opening a
real window isn't something a stdout diff can check) — run it yourself:
`fractyne run examples/window.fy`.

See `examples/` for sample programs covering each feature.
