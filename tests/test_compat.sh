#!/bin/bash
# Comprehensive compatibility test

# declare statement
declare -i myint=42
declare -r myconst="readonly"
declare -a myarray=(a b c)
echo "declare: $myint $myconst ${myarray[1]}"

# Array operations
arr=(apple banana cherry date)
echo "array count: ${#arr[@]}"
echo "array all: ${arr[@]}"
echo "array first: ${arr[0]}"
echo "array last: ${arr[3]}"
echo "array slice: ${arr[@]:1:2}"

# Array iteration
for item in "${arr[@]}"; do
    echo "item: $item"
done

# Array append
arr+=(elderberry fig)
echo "after append: ${arr[@]}"

# String length
str="Hello World"
echo "strlen: ${#str}"

# Substring
echo "substr: ${str:0:5}"
echo "substr2: ${str:6}"

# Case conversion
echo "upper: ${str^^}"
echo "lower: ${str,,}"

# Pattern removal
file="path/to/file.tar.gz"
echo "basename: ${file##*/}"
echo "dirname: ${file%/*}"
echo "ext: ${file##*.}"
echo "noext: ${file%.*}"

# String replacement
text="hello world hello"
echo "replace: ${text/hello/hi}"
echo "replace all: ${text//hello/hi}"

# Default values
unset undef
echo "default: ${undef:-fallback}"
echo "default set: ${undef:=set_now}"
echo "now: $undef"

# Arithmetic
x=10
y=3
echo "add: $((x + y))"
echo "sub: $((x - y))"
echo "mul: $((x * y))"
echo "div: $((x / y))"
echo "mod: $((x % y))"
echo "pow: $((x ** 2))"

# Arithmetic with assignment
((z = x * y))
echo "z = $z"

# Increment/decrement
i=5
((i++))
echo "i++: $i"
((i--))
echo "i--: $i"

# Conditional
if [ $x -gt 5 ] && [ $x -lt 20 ]; then
    echo "x in range"
fi

if [[ "$str" == Hello* ]]; then
    echo "starts with Hello"
fi

# Case statement
fruit="apple"
case $fruit in
    apple|pear)
        echo "pome fruit"
        ;;
    orange|lemon)
        echo "citrus fruit"
        ;;
    *)
        echo "unknown fruit"
        ;;
esac

# Functions
greet() {
    local name="$1"
    echo "Hello, $name!"
}
greet "World"

# Function with return value
factorial() {
    local n=$1
    if [ $n -le 1 ]; then
        echo 1
    else
        local prev=$(factorial $((n - 1)))
        echo $((n * prev))
    fi
}
echo "5! = $(factorial 5)"

# Command substitution
current_dir=$(pwd)
echo "dir: $current_dir"

# Pipe
echo -e "3\n1\n2" | sort -n | head -1

# While loop
n=0
while [ $n -lt 3 ]; do
    echo "while $n"
    n=$((n + 1))
done

# For loop with seq
for i in $(seq 1 3); do
    echo "seq $i"
done

# Until loop (simulated)
n=3
while [ $n -gt 0 ]; do
    echo "until $n"
    n=$((n - 1))
done

echo "COMPAT TEST DONE"
