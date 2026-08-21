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

# ---- VM-path consistency cases (REG-08..REG-10, added 2026-08-22) ----
# Transpile with --vm, compile, diff vs bash.
vm_check() {  # name, script
    local name="$1" script="$2"
    local base="$WORK/vm_$(basename "$script" .sh)"
    if ! timeout 90 $S2C "$script" "$base.c" --vm >/dev/null 2>&1; then
        echo "FAIL: $name (VM transpile)"; FAIL=1; return
    fi
    if ! gcc -O2 -w -o "$base" "$base.c" 2>/dev/null; then
        echo "FAIL: $name (VM cgen)"; FAIL=1; return
    fi
    if ! diff <(timeout 20 bash "$script" 2>&1) <(timeout 20 "$base" 2>&1) >/dev/null 2>&1; then
        echo "FAIL: $name (VM behavior diff)"; FAIL=1; return
    fi
    echo "PASS: $name"
}

# REG-08: $$ and $? expansion in VM path (used to print literal "$$")
cat > "$WORK/reg08.sh" << 'SCRIPT'
#!/bin/bash
echo "pid-is-num: $$" | grep -qE 'pid-is-num: [0-9]+$' && echo "pid numeric"
false
echo "rc: $?"
true
echo "rc: $?"
SCRIPT

# REG-09: redirect honored by echo in VM path (fast PRINT path used to
# swallow > file entirely)
cat > "$WORK/reg09.sh" << 'SCRIPT'
#!/bin/bash
f=$(mktemp)
echo "saved" > "$f"
cat "$f"
rm -f "$f"
SCRIPT

# REG-10: pipe trailing-newline survival + echo -e under /bin/sh
# (capture/re-print merged consecutive outputs into one line; dash echo
# printed "-e" literally)
cat > "$WORK/reg10.sh" << 'SCRIPT'
#!/bin/bash
echo -e "c\nb\na\nd" | sort
echo "hello world" | wc -w
echo "tail"
SCRIPT

vm_check "REG-08 VM pid and status expansion" "$WORK/reg08.sh"
vm_check "REG-09 VM echo redirect" "$WORK/reg09.sh"
vm_check "REG-10 VM pipe newline + echo -e" "$WORK/reg10.sh"

# REG-11: read -p / -rp / -s flag support (transpile path)
# Pipe stdin: no prompt must appear (bash shows prompt only on tty);
# variables and REPLY must be read correctly. tty-specific prompt
# display verified manually via pty (see REVIEW_LOG 2026-08-22).
cat > "$WORK/reg11.sh" << 'SCRIPT'
#!/bin/bash
default="Alice"
read -p "Enter name [$default]: " name
echo "Hello, $name"
read -rp "Age: " age
echo "Next year: $((age + 1))"
read -p "Plain: "
echo "REPLY: $REPLY"
read -s -p "Password: " pw
echo "got ${#pw} chars"
SCRIPT

if [ ! -f "$WORK/reg11.in" ]; then
    printf 'bob\n25\nplainline\nsecret123\n' > "$WORK/reg11.in"
fi
run_reg11() {
    local base="$WORK/reg11bin"
    if ! timeout 10 $S2C "$WORK/reg11.sh" "$base.c" >/dev/null 2>&1; then
        echo "FAIL: REG-11 read -p (transpile)"; FAIL=1; return
    fi
    if ! gcc -O2 -w -o "$base" "$base.c" 2>/dev/null; then
        echo "FAIL: REG-11 read -p (cgen)"; FAIL=1; return
    fi
    local actual expected
    actual=$(timeout 10 "$base" < "$WORK/reg11.in" 2>&1)
    expected=$(timeout 10 bash "$WORK/reg11.sh" < "$WORK/reg11.in" 2>&1)
    if [ "$actual" == "$expected" ] && ! echo "$actual" | grep -q "Enter name\|Age:\|Plain:\|Password:"; then
        echo "PASS: REG-11 read -p flags"
    else
        echo "FAIL: REG-11 read -p (behavior diff or prompt leaked to non-tty)"
        diff <(echo "$expected") <(echo "$actual") | head -6 | sed 's/^/    /'
        FAIL=1
    fi
}
run_reg11

# REG-12: pipeline on the left of && (parser used to swallow '|' as argv)
cat > "$WORK/reg12.sh" << 'SCRIPT'
#!/bin/bash
echo "pid-is-num: $$" | grep -qE 'pid-is-num: [0-9]+$' && echo "pid numeric"
echo hi | grep hi && echo matched
echo -e "c\nb\na\nd" | sort | head -2
false | cat && echo "WRONG" || echo "or-branch"
SCRIPT
check "REG-12 pipe inside && chain" "$WORK/reg12.sh"

if [ $FAIL -eq 0 ]; then echo "ALL REGRESSION TESTS PASSED"; else echo "REGRESSION DETECTED"; exit 1; fi
