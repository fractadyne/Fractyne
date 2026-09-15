#!/usr/bin/env bash
# Runs every examples/*.fy that has a matching examples/*.expected file via
# `fractyne run` (compiles to a scratch dir and cleans up after itself, so
# this script never leaves .c/binary clutter in examples/) and diffs its
# stdout against the expected output.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRACTYNE="$ROOT/bin/fractyne"

pass=0
fail=0

for fy in "$ROOT"/examples/*.fy; do
    name="$(basename "$fy" .fy)"
    expected="$ROOT/examples/$name.expected"
    [ -f "$expected" ] || continue

    if ! actual="$("$FRACTYNE" run "$fy" 2>/tmp/fractyne_test_run.log)"; then
        echo "FAIL  $name (build/run failed)"
        cat /tmp/fractyne_test_run.log
        fail=$((fail + 1))
        continue
    fi

    if [ "$actual" == "$(cat "$expected")" ]; then
        echo "PASS  $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name"
        echo "  expected: $(cat "$expected" | tr '\n' '|')"
        echo "  actual:   $(echo "$actual" | tr '\n' '|')"
        fail=$((fail + 1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
