#!/bin/bash
# Comprehensive stress test for shell2c — tests edge cases, tricky syntax, extreme scenarios

# === 1. Special characters in strings ===
echo "tab	here"
echo "newline
here"
echo 'single quote: $HOME'
echo "double quote: $HOME"
echo "mixed: 'single' and \"double\""

# === 2. Escape sequences ===
echo "backslash: \\"
echo "dollar: \$HOME"
echo "backtick: \`date\`"
echo "quote: \"hello\""

# === 3. Variable edge cases ===
EMPTY=""
echo "empty: '${EMPTY}'"
echo "empty_len: ${#EMPTY}"
UNSET_VAR="test"
unset UNSET_VAR
echo "default: ${UNSET_VAR:-fallback}"
echo "alt: ${UNSET_VAR:+set}"
X=5
echo "neg default: ${X:-default}"
echo "assign: ${Y:=assigned}"
echo "Y is now: $Y"

# === 4. Arithmetic edge cases ===
a=10
b=3
echo "add: $((a + b))"
echo "sub: $((a - b))"
echo "mul: $((a * b))"
echo "div: $((a / b))"
echo "mod: $((a % b))"
echo "pow: $((a ** 2))"
echo "neg: $((-a))"
echo "paren: $(((a + b) * 2))"
echo "assign: $((c = a + b))"
echo "c is: $c"
echo "postinc: $((c++))"
echo "c after inc: $c"
echo "preinc: $((++c))"
echo "bitand: $((a & b))"
echo "bitor: $((a | b))"
echo "xor: $((a ^ b))"
echo "shl: $((a << 2))"
echo "shr: $((a >> 1))"
echo "not: $((~a))"
echo "logic_and: $((a && b))"
echo "logic_or: $((a || b))"
echo "ternary: $((a > b ? 1 : 0))"

# === 5. String manipulation ===
STR="Hello World"
echo "len: ${#STR}"
echo "upper: ${STR^^}"
echo "lower: ${STR,,}"
echo "substr1: ${STR:0:5}"
echo "substr2: ${STR:6}"
echo "substr3: ${STR: -5}"
echo "replace_first: ${STR/o/0}"
echo "replace_all: ${STR//o/0}"
echo "replace_start: ${STR/#Hello/Hi}"
echo "replace_end: ${STR/%World/Earth}"
echo "strip_prefix: ${STR#Hello }"
echo "strip_prefix_greedy: ${STR##H*o }"
echo "strip_suffix: ${STR% World}"
echo "strip_suffix_greedy: ${STR%%o*}"

# === 6. Arrays ===
arr=(apple banana cherry)
echo "arr0: ${arr[0]}"
echo "arr1: ${arr[1]}"
echo "arr_all: ${arr[@]}"
echo "arr_count: ${#arr[@]}"
echo "arr_slice: ${arr[@]:0:2}"
arr[3]="date"
echo "arr3: ${arr[3]}"
arr+=(elderberry)
echo "arr_last: ${arr[4]}"

# === 7. Control flow ===
for i in 1 2 3; do
    echo "for: $i"
done

for i in {1..5}; do
    echo "range: $i"
done

for i in {a..c}; do
    echo "alpha: $i"
done

j=0
while [ $j -lt 3 ]; do
    echo "while: $j"
    j=$((j + 1))
done

k=0
until [ $k -ge 3 ]; do
    echo "until: $k"
    k=$((k + 1))
done

# === 8. Conditionals ===
if [ "$X" -eq 5 ]; then
    echo "X is 5"
elif [ "$X" -gt 5 ]; then
    echo "X is greater than 5"
else
    echo "X is less than 5"
fi

if [[ "$STR" == Hello* ]]; then
    echo "starts with Hello"
fi

if [[ "$STR" == *World ]]; then
    echo "ends with World"
fi

if [[ "$STR" =~ ^Hello ]]; then
    echo "regex match"
fi

# === 9. Case/esac ===
fruit="apple"
case $fruit in
    apple|apricot)
        echo "starts with a"
        ;;
    banana)
        echo "starts with b"
        ;;
    *)
        echo "other"
        ;;
esac

# === 10. Functions ===
greet() {
    local name="$1"
    echo "Hello, $name!"
}
greet "World"
greet "Shell2C"

add() {
    echo $(($1 + $2))
}
result=$(add 3 4)
echo "3 + 4 = $result"

# Function with return value
is_positive() {
    if [ $1 -gt 0 ]; then
        return 0
    else
        return 1
    fi
}
if is_positive 5; then
    echo "5 is positive"
fi

# === 11. Pipes ===
echo "hello world" | tr 'a-z' 'A-Z'
echo -e "line1\nline2\nline3" | head -2
echo "one two three" | cut -d' ' -f2

# === 12. Command substitution ===
echo "date: $(echo today)"
echo "nested: $(echo $(echo deep))"
echo "backtick: `echo bt`"

# === 13. Heredocs ===
cat <<HEREDOC
Simple heredoc
With multiple lines
HEREDOC

cat <<'NOEXPAND'
No expansion: $HOME
NOEXPAND

cat <<EXPAND
Expansion: $HOME
EXPAND

# === 14. Redirection ===
echo "to file" > /tmp/cshll_test.txt
cat < /tmp/cshll_test.txt
echo "appended" >> /tmp/cshll_test.txt
cat /tmp/cshll_test.txt
rm -f /tmp/cshll_test.txt

# === 15. Special variables ===
echo "pid: $$"
echo "params: $@"
echo "argc: $#"

# === 16. Subshell ===
(SUB="inside"; echo "sub: $SUB")
echo "outside: $SUB"

# === 17. Brace expansion ===
echo {1..5}
echo {a,b,c}
echo pre{x,y}post

# === 18. Logical operators ===
true && echo "and"
false || echo "or"
true && false || echo "chain"

# === 19. C-style for loop ===
for ((i=0; i<3; i++)); do
    echo "c-for: $i"
done

# === 20. Break and continue ===
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then
        continue
    fi
    if [ $i -eq 5 ]; then
        break
    fi
    echo "loop: $i"
done

echo "STRESS TEST DONE"
