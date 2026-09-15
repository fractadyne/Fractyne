#!/usr/bin/env sh
# Installs the Fractyne compiler: builds bin/fractyne from source and copies
# it to ~/.local/bin (creating that directory and adding it to your shell's
# PATH if needed). Safe to re-run -- it just rebuilds and reinstalls.
#
# Usage:
#   ./install.sh                 # run from inside a Fractyne checkout
#   FRACTYNE_GIT_URL=<url> sh install.sh   # clone from <url> first, then install
set -eu

INSTALL_DIR="$HOME/.local/bin"

say() { printf '%s\n' "$*"; }
die() { printf 'fractyne installer: %s\n' "$*" >&2; exit 1; }

# ---- 1. locate (or fetch) the source ---------------------------------

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# src/involve.c is Fractyne-specific -- checking only for src/main.c and a
# Makefile risks a false match against some unrelated C project (e.g. when
# this script is piped via `sh -c "$(curl ...)"`, $0 is just "sh", so
# dirname resolves to whatever directory the invoker happened to be in).
if [ -f "$SCRIPT_DIR/src/main.c" ] && [ -f "$SCRIPT_DIR/src/involve.c" ] && [ -f "$SCRIPT_DIR/Makefile" ]; then
    SRC_DIR="$SCRIPT_DIR"
elif [ -n "${FRACTYNE_GIT_URL:-}" ]; then
    command -v git >/dev/null 2>&1 || die "git is required to clone \$FRACTYNE_GIT_URL but isn't installed"
    CLONE_DIR=$(mktemp -d)
    say "Cloning $FRACTYNE_GIT_URL ..."
    git clone --depth 1 "$FRACTYNE_GIT_URL" "$CLONE_DIR/fractyne"
    SRC_DIR="$CLONE_DIR/fractyne"
else
    die "no Fractyne source found next to this script, and \$FRACTYNE_GIT_URL isn't set.
Run this script from inside a Fractyne checkout, or set FRACTYNE_GIT_URL to a git
URL to clone from, e.g.: FRACTYNE_GIT_URL=https://example.com/you/fractyne.git sh install.sh"
fi

# ---- 2. sanity-check the platform -------------------------------------

case "$(uname -s)" in
    Linux|Darwin) ;;
    MINGW*|MSYS*|CYGWIN*)
        die "Fractyne doesn't run natively on Windows (no fork()/gcc by default).
Install WSL (Windows Subsystem for Linux) from an elevated PowerShell:
    wsl --install
then open the WSL shell and run this installer there instead." ;;
    *) say "warning: unrecognized platform '$(uname -s)' -- continuing anyway, no promises." ;;
esac

# ---- 3. check for a C toolchain ---------------------------------------

if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1; then
    case "$(uname -s)" in
        Darwin)
            die "no C compiler found. Install one with:
    xcode-select --install" ;;
        *)
            if command -v apt-get >/dev/null 2>&1; then
                die "no C compiler found. Install one with:
    sudo apt-get install build-essential"
            elif command -v dnf >/dev/null 2>&1; then
                die "no C compiler found. Install one with:
    sudo dnf install gcc make"
            elif command -v pacman >/dev/null 2>&1; then
                die "no C compiler found. Install one with:
    sudo pacman -S base-devel"
            else
                die "no C compiler found. Install gcc and make via your distro's package manager."
            fi ;;
    esac
fi
command -v make >/dev/null 2>&1 || die "'make' is required but not installed."

# ---- 4. build -----------------------------------------------------------

say "Building Fractyne from $SRC_DIR ..."
make -C "$SRC_DIR" >/dev/null

# ---- 5. install -----------------------------------------------------------

mkdir -p "$INSTALL_DIR"
cp "$SRC_DIR/bin/fractyne" "$INSTALL_DIR/fractyne"
chmod +x "$INSTALL_DIR/fractyne"
say "Installed $INSTALL_DIR/fractyne"

# ---- 6. make sure it's on PATH --------------------------------------------

case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *)
        RC_FILE="$HOME/.bashrc"
        case "${SHELL:-}" in
            */zsh) RC_FILE="$HOME/.zshrc" ;;
        esac
        if ! grep -qF "$INSTALL_DIR" "$RC_FILE" 2>/dev/null; then
            printf '\nexport PATH="%s:$PATH"\n' "$INSTALL_DIR" >> "$RC_FILE"
            say "Added $INSTALL_DIR to PATH in $RC_FILE (open a new shell, or run: source $RC_FILE)"
        fi ;;
esac

say ""
say "Done. Try it:"
say "  fractyne run hello.fy"
