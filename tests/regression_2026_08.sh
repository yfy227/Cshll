#!/bin/bash
# Regression tests for bugs found in 2026-08-20/21 review cycle.
# Each case documents a specific bug; if any fails, that bug has regressed.
#
# Bug registry:
#   REG-01: var_hash_find infinite loop when hash uninitialized (heredoc + $VAR)
#   REG-02: __sfd_N_M variable redefinition when two redirects share fd 0
#   REG-03: function args in [[ ]] / [ ] emitted bare (int literal in char*[])
#   REG-04: heredoc rotation — done-handler steals heredocs of earlier commands
#   REG-05: process substitution naive strtok tokenization breaks sh syntax

cd "$(dirname "$0")/.."
S2C=./shell2c
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAIL=0

check() {  # name, script
    local name="$1" script="$2"
    local base="$WORK/$(basename "$script" .sh)"
    if ! timeout 10 $S2C "$script" "$base.c" >/dev/null 2>&1; then
        echo "FAIL: $name (transpile hang/error)"; FAIL=1; return
    fi
    if ! gcc -O2 -w -o "$base" "$base.c" 2>/dev/null; then
        echo "FAIL: $name (generated C does not compile)"; FAIL=1; return
    fi
    if ! diff <(timeout 10 bash "$script" 2>&1) <(timeout 10 "$base" 2>&1) >/dev/null 2>&1; then
        echo "FAIL: $name (output mismatch)"; FAIL=1; return
    fi
    echo "PASS: $name"
}

# REG-01: heredoc with unregistered variable expansion — used to hang forever
cat > "$WORK/reg01.sh" << 'SCRIPT'
#!/bin/bash
cat <<EOF
home is $HOME
EOF
echo done
SCRIPT

# REG-02: two fd-0 redirects in one command (heredoc after process subst context)
cat > "$WORK/reg02.sh" << 'SCRIPT'
#!/bin/bash
cat <<EOF
first
EOF
cat <<'B2'
second
B2
echo ok
SCRIPT

# REG-03: numeric/bare args to user functions inside [[ ]] and if
cat > "$WORK/reg03.sh" << 'SCRIPT'
#!/bin/bash
is_even() {
    if [ $(($1 % 2)) -eq 0 ]; then return 0; else return 1; fi
}
if is_even 4; then echo "4 is even"; fi
if ! is_even 7; then echo "7 is odd"; fi
greet() { echo "hello $1"; }
if [[ $(greet world) == "hello world" ]]; then echo "subst ok"; fi
SCRIPT

# REG-04: heredoc rotation — heredocs must stay with their own commands
cat > "$WORK/reg04.sh" << 'SCRIPT'
#!/bin/bash
cat <<EOF
heredoc-A
EOF
cat <<B2
heredoc-B
B2
while IFS= read -r line; do
  echo "L: $line"
done < <(echo "ps1"; echo "ps2")
SCRIPT

# REG-05: process substitution with quotes and semicolons
cat > "$WORK/reg05.sh" << 'SCRIPT'
#!/bin/bash
while IFS= read -r line; do
  echo "got: $line"
done < <(echo "a b"; echo "c d")
echo "end"
SCRIPT

# REG-06: variable self-append must not truncate (snprintf overlap UB)
cat > "$WORK/reg06.sh" << 'SCRIPT'
#!/bin/bash
BIG=""
for i in 1 2 3 4 5; do
  BIG="$BIG padding-block-$i-abcdefghij"
done
echo "len: ${#BIG}"
echo "tail: ${BIG: -12}"
SCRIPT

# REG-07: long variable interpolation must not truncate at 1024
cat > "$WORK/reg07.sh" << 'SCRIPT'
#!/bin/bash
LONG="$(printf 'x%.0s' $(seq 1 2000))"
echo "size: ${#LONG}"
echo "head: ${LONG:0:10}"
SCRIPT

check "REG-01 heredoc+unregistered var no hang" "$WORK/reg01.sh"
check "REG-02 dual fd0 redirects no redefinition" "$WORK/reg02.sh"
check "REG-03 typed function args in conditions" "$WORK/reg03.sh"
check "REG-04 heredoc ordering" "$WORK/reg04.sh"
check "REG-05 proc subst shell syntax" "$WORK/reg05.sh"
check "REG-06 self-append no truncation" "$WORK/reg06.sh"
check "REG-07 2KB var interpolation" "$WORK/reg07.sh"

if [ $FAIL -eq 0 ]; then echo "ALL REGRESSION TESTS PASSED"; else echo "REGRESSION DETECTED"; exit 1; fi
