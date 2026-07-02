#!/bin/bash
# Edge case tests

# Empty echo
echo
echo "after empty"

# Multiple semicolons
a=1; b=2; c=3
echo "a=$a b=$b c=$c"

# Nested if
x=5
if [ $x -gt 0 ]; then
    if [ $x -gt 3 ]; then
        echo "x > 3"
    else
        echo "0 < x <= 3"
    fi
fi

# 3-stage pipe
echo -e "3\n1\n2" | sort | head -2

# Arithmetic edge cases
echo $((10 / 3))
echo $((10 % 3))
echo $((2 * 3 + 1))
echo $(((2 + 3) * 4))

# String with spaces
s="hello world foo"
echo "[$s]"
echo "words: $(echo $s | wc -w)"

# Negative numbers
n=-5
echo "n=$n"
echo "abs=$((n < 0 ? -n : n))"

# For with command substitution
for f in a b c; do
    echo "item: $f"
done

# While with break
i=0
while true; do
    i=$((i + 1))
    if [ $i -ge 3 ]; then
        break
    fi
    echo "loop $i"
done

echo "EDGE CASES DONE"
