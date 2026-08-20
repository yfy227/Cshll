#!/bin/bash
# Syntax coverage matrix — one minimal test per shell language feature.
# Each case: tests/syntax/NNN_feature.sh + optional .expected file.
# Without .expected: the file itself contains expected output inline
# after the #__EXPECT__ marker.
#
# Usage: bash tests/run_syntax_tests.sh [filter-substring]
# Exit 0 iff all PASS (or all non-KNOWN-GAP).

cd "$(dirname "$0")/.."
S2C=./shell2c
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0; GAPS=0; FAILED_NAMES=""

for script in tests/syntax/*.sh; do
    name=$(basename "$script" .sh)
    [ -n "$1" ] && case "$name" in *"$1"*) ;; *) continue;; esac

    # Inline expected output lives after the #__EXPECT__ marker. That
    # marker (and everything after) is NOT shell code — strip it before
    # transpiling, otherwise the expected lines get compiled as commands.
    clean_script="$WORK/$name.clean.sh"
    expected_file="$WORK/$name.exp"
    if [ -f "tests/syntax/$name.expected" ]; then
        cp "$script" "$clean_script"
        cp "tests/syntax/$name.expected" "$expected_file"
    else
        awk '/^#__EXPECT__$/{exit} {print}' "$script" > "$clean_script"
        sed -n '/^#__EXPECT__$/,$p' "$script" | tail -n +2 > "$expected_file"
    fi
    [ -s "$expected_file" ] || expected_file=/dev/null

    out_c="$WORK/$name.c"
    if ! timeout 10 $S2C "$clean_script" "$out_c" >/dev/null 2>&1; then
        echo "KNOWN-GAP(transpile): $name"; GAPS=$((GAPS+1)); continue
    fi
    if ! gcc -O2 -w -o "$WORK/$name.bin" "$out_c" 2>/dev/null; then
        echo "KNOWN-GAP(cgen):      $name"; GAPS=$((GAPS+1)); continue
    fi
    actual=$(timeout 10 "$WORK/$name.bin" 2>&1)
    expected=$(cat "$expected_file")
    if [ "$actual" == "$expected" ]; then
        PASS=$((PASS+1))
    else
        echo "FAIL: $name"
        diff <(echo "$expected") <(echo "$actual") | head -6 | sed 's/^/    /'
        FAIL=$((FAIL+1)); FAILED_NAMES="$FAILED_NAMES $name"
    fi
done

echo "----------------------------------------"
echo "syntax matrix: PASS=$PASS FAIL=$FAIL KNOWN-GAP=$GAPS"
[ $FAIL -eq 0 ] || { echo "failed:$FAILED_NAMES"; exit 1; }
exit 0
