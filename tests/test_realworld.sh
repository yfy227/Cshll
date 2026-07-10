#!/bin/bash
# Real-world shell script compatibility test

# Backtick command substitution
TODAY=`date +%Y`
echo "year via backticks: $TODAY"

# Nested command substitution
DIRNAME=$(basename $(dirname /a/b/c/d.txt))
echo "nested: $DIRNAME"

# Heredoc with variable expansion
cat <<EOF
Hello from heredoc
Variable expands: $TODAY
EOF

# Heredoc without expansion (quoted delimiter)
cat <<'NOEXPAND'
$TODAY stays literal
NOEXPAND

# Heredoc to variable
VAR=$(cat <<HEREDOC
line1
line2
line3
HEREDOC
)
echo "heredoc var lines: $(echo "$VAR" | wc -l)"

# Process substitution
while IFS= read -r line; do
    echo "ps: $line"
done < <(echo "test1"; echo "test2")

# String operations
STR="Hello World"
echo "upper: ${STR^^}"
echo "lower: ${STR,,}"
echo "replace: ${STR/o/0}"
echo "replace all: ${STR//o/0}"
echo "substring: ${STR:0:5}"
echo "length: ${#STR}"

# Default values
unset UNSET_VAR
echo "default: ${UNSET_VAR:-mydefault}"
echo "default assign: ${UNSET_VAR:=assigned}"
echo "now: $UNSET_VAR"

# Alternative values
SET_VAR="hello"
echo "alt: ${SET_VAR:+yes}"

# Array operations
ARR=(one two three four)
echo "array count: ${#ARR[@]}"
echo "array all: ${ARR[*]}"
echo "array slice: ${ARR[@]:1:2}"

# Associative-like access via index
echo "array[2]: ${ARR[2]}"

# Arithmetic with increment
X=5
echo "post-inc: $((X++))"
echo "after: $X"
echo "pre-inc: $((++X))"
echo "after pre: $X"

# Ternary
Y=10
echo "ternary: $((Y > 5 ? 100 : 200))"

# Bitwise
echo "bitwise and: $((12 & 10))"
echo "bitwise or: $((12 | 10))"
echo "bitwise xor: $((12 ^ 10))"
echo "shift left: $((1 << 4))"
echo "shift right: $((256 >> 2))"

# Logical
echo "logical: $((1 && 0))"
echo "logical or: $((1 || 0))"

# String comparison in [[ ]]
if [[ "abc" == "a*" ]]; then
    echo "glob match"
fi
if [[ "abc" != "xyz" ]]; then
    echo "not equal"
fi

# Regex match
if [[ "hello123" =~ ^[a-z]+[0-9]+$ ]]; then
    echo "regex matched"
fi

# Case with complex patterns
VAL="test123.txt"
case "$VAL" in
    *.txt)
        echo "text file: $VAL"
        ;;
    *.sh|*.bash)
        echo "script file"
        ;;
    [0-9]*)
        echo "starts with digit"
        ;;
    *)
        echo "unknown"
        ;;
esac

# Functions with local variables
counter_demo() {
    local count=0
    count=$((count + 10))
    echo "local count: $count"
}
counter_demo

# Function returning status
is_even() {
    if [ $(($1 % 2)) -eq 0 ]; then
        return 0
    else
        return 1
    fi
}
if is_even 4; then
    echo "4 is even"
fi
if ! is_even 7; then
    echo "7 is odd"
fi

# Multiple commands with && and ||
true && echo "and succeeded" || echo "and failed"
false && echo "wont show" || echo "or fallback"

# Subshell
(SUBVAR="in subshell"; echo "sub: $SUBVAR")
echo "outside: $SUBVAR"

# Brace expansion
echo "brace: a{b,c,d}e"

# Tilde expansion
echo "home: $HOME"

# Special parameters
echo "script name: $0"
echo "pid: $$"
echo "params: $@"

echo "REALWORLD TEST DONE"
