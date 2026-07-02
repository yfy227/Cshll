#!/bin/bash
# Advanced feature test

# Break and continue
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then
        continue
    fi
    if [ $i -eq 5 ]; then
        break
    fi
    echo "i=$i"
done

# Nested function with local-like behavior
calc() {
    local_result=$(($1 * $2))
    echo $local_result
}
echo "5*7=$(calc 5 7)"

# String length and comparison
s="hello"
echo "length of '$s' is ${#s}"
if [ ${#s} -eq 5 ]; then
    echo "correct length"
fi

# Multiple redirects
echo "stderr test" >&2 2>/dev/null
echo "after stderr"

# Here-string
read -r first second <<< "hello world"
echo "first=$first second=$second"

# Case with pipe patterns
day="sat"
case $day in
    sat|sun)
        echo "weekend"
        ;;
    mon|tue|wed|thu|fri)
        echo "weekday"
        ;;
esac

# Arithmetic increment
counter=0
for i in 1 2 3; do
    counter=$((counter + i))
done
echo "total=$counter"

# String comparison operators
a="apple"
b="banana"
if [ "$a" \< "$b" ]; then
    echo "$a before $b"
fi

# Until loop (simulated with while not)
n=5
while [ $n -gt 0 ]; do
    echo "n=$n"
    n=$((n - 1))
done

# Test -e -f -d
if [ -e /tmp ]; then
    echo "/tmp exists"
fi

echo "ADVANCED TEST DONE"
