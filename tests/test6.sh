#!/bin/bash
# Final comprehensive test

# String substitution ${var/old/new}
path="/usr/local/bin/script.sh"
echo "dir=${path%/*}"
echo "file=${path##*/}"
echo "noext=${path%.sh}"

# Multiple variable types
count=42
name="test"
flag=true
echo "count=$count name=$name flag=$flag"

# Nested command substitution
inner=$(echo "nested")
outer="result: $inner"
echo "$outer"

# Arithmetic with variables
a=15
b=4
echo "a/b=$((a / b))"
echo "a%b=$((a % b))"
echo "a*b=$((a * b))"

# Conditional with multiple operators
score=75
if [ $score -ge 90 ]; then
    echo "A"
elif [ $score -ge 80 ]; then
    echo "B"
elif [ $score -ge 70 ]; then
    echo "C"
else
    echo "F"
fi

# String test
str=""
if [ -z "$str" ]; then
    echo "empty string"
fi
str="hello"
if [ -n "$str" ]; then
    echo "non-empty: $str"
fi

# For with range-like
total=0
for n in 1 2 3 4 5 6 7 8 9 10; do
    total=$((total + n))
done
echo "sum 1-10 = $total"

# Function with multiple args
max() {
    if [ $1 -gt $2 ]; then
        echo $1
    else
        echo $2
    fi
}
echo "max(3,7)=$(max 3 7)"
echo "max(9,2)=$(max 9 2)"

# Pipe chain
echo -e "banana\napple\ncherry" | sort | head -1

# Redirect and append
echo "line1" > /tmp/s2c_final_$$.txt
echo "line2" >> /tmp/s2c_final_$$.txt
echo "line3" >> /tmp/s2c_final_$$.txt
echo "file contents:"
cat /tmp/s2c_final_$$.txt
rm -f /tmp/s2c_final_$$.txt

echo "FINAL TEST DONE"
