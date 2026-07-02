#!/bin/bash
# Common shell patterns test

# String quoting variations
echo 'single quotes: $HOME stays literal'
echo "double quotes: $HOME expands"
echo "mixed: '$HOME' and \"$HOME\""

# Variable default in echo
echo "path: ${PATH:-/usr/bin}"

# Conditional assignment
config_file="${1:-/etc/default/config}"
echo "config: $config_file"

# Test command variations
[ -f /etc/hostname ] && echo "hostname exists"
[ ! -f /nonexistent ] && echo "nonexistent doesn't exist"
[ -n "$HOME" ] && echo "HOME is set"

# String operations
s="hello world"
echo "${s#hello}"    # remove prefix
echo "${s/world/WORLD}"  # replace
echo "${#s}"         # length

# Numeric comparisons
a=10
b=20
[ $a -lt $b ] && echo "a < b"
[ $a -eq 10 ] && echo "a == 10"

# If-elif-else chain
grade=85
if [ $grade -ge 90 ]; then
    echo "A"
elif [ $grade -ge 80 ]; then
    echo "B"
elif [ $grade -ge 70 ]; then
    echo "C"
else
    echo "F"
fi

# For loop variations
for i in 1 2 3; do echo "num: $i"; done
for s in a b c; do echo "str: $s"; done

# While with condition
n=3
while [ $n -gt 0 ]; do
    echo "count: $n"
    n=$((n - 1))
done

# Function with arguments
greet() {
    echo "Hello, $1!"
    echo "You have $# arguments"
}
greet World
greet Alice Bob

# Command substitution
dir=$(pwd)
echo "current dir: $dir"

# Pipe
echo "hello" | tr 'a-z' 'A-Z'

# Multiple commands
echo "first"; echo "second"

# Test operators
file="/etc/hostname"
if [ -f "$file" ] && [ -r "$file" ]; then
    echo "$file is readable"
fi

# Case statement
os="linux"
case $os in
    linux)
        echo "Linux system"
        ;;
    darwin|macos)
        echo "macOS system"
        ;;
    *)
        echo "Unknown: $os"
        ;;
esac

# Array basics
arr=(one two three)
echo "first: ${arr[0]}"
echo "count: ${#arr[@]}"

# Arithmetic
x=5
y=3
echo "$((x + y))"
echo "$((x * y))"

echo "PATTERNS TEST DONE"
