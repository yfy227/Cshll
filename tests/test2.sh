#!/bin/bash
# Comprehensive test for shell2c

# Redirects
echo "to file" > /tmp/s2c_redir_$$.txt
cat /tmp/s2c_redir_$$.txt
echo "appended" >> /tmp/s2c_redir_$$.txt
cat /tmp/s2c_redir_$$.txt
rm -f /tmp/s2c_redir_$$.txt

# Pipe
echo -e "c\nb\na\nd" | sort
echo "hello world" | wc -w

# Arithmetic with all operators
a=20
b=7
echo "a+b=$((a+b))"
echo "a-b=$((a-b))"
echo "a*b=$((a*b))"
echo "a/b=$((a/b))"
echo "a%b=$((a%b))"
echo "a**2=$((a*a))"

# Compound conditions
x=5
if [ $x -gt 0 ] && [ $x -lt 10 ]; then
    echo "x in range"
fi

# String operations
str="HelloWorld"
echo "len=${#str}"

# For loop with seq-like
for i in 1 2 3 4 5; do
    echo -n "$i "
done
echo ""

# Nested loops
for i in 1 2; do
    for j in a b; do
        echo "$i$j"
    done
done

# Until loop equivalent (while not)
n=3
while [ $n -gt 0 ]; do
    echo "countdown $n"
    n=$((n - 1))
done

# Functions with return
add() {
    echo $(($1 + $2))
}
result=$(add 3 4)
echo "3+4=$result"

# Multiple commands
echo "line1"; echo "line2"; echo "line3"

# Test operators
if [ -d /tmp ]; then
    echo "/tmp is a directory"
fi
if [ -f /etc/hostname ]; then
    echo "/etc/hostname is a file"
fi

# Case with multiple patterns
color="red"
case $color in
    red|crimson)
        echo "warm color"
        ;;
    blue|cyan)
        echo "cool color"
        ;;
    *)
        echo "other"
        ;;
esac

echo "ALL TESTS PASSED"
