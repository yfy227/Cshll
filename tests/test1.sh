#!/bin/bash
# Test script for shell2c transpiler

# Variable assignment
name="World"
count=5

# Echo with variable
echo "Hello, $name!"
echo "Count is $count"

# Arithmetic
x=10
y=3
sum=$((x + y))
echo "Sum: $sum"
echo "Product: $((x * y))"
echo "Modulo: $((x % y))"

# Conditionals
if [ $x -gt $y ]; then
    echo "x is greater than y"
elif [ $x -eq $y ]; then
    echo "x equals y"
else
    echo "x is less than y"
fi

# String test
if [ "$name" = "World" ]; then
    echo "Name matches"
fi

# For loop
for i in 1 2 3; do
    echo "Iteration $i"
done

# While loop
n=0
while [ $n -lt 3 ]; do
    echo "n = $n"
    n=$((n + 1))
done

# Functions
greet() {
    echo "Greetings from function"
}
greet

# Case
fruit="apple"
case $fruit in
    apple)
        echo "It's an apple"
        ;;
    banana)
        echo "It's a banana"
        ;;
    *)
        echo "Unknown fruit"
        ;;
esac

# Builtins
pwd
echo "Done!"
