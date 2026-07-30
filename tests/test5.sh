#!/bin/bash
# Advanced feature test round 2

# Arrays
arr=(red green blue)
echo "first=${arr[0]}"
echo "second=${arr[1]}"
echo "third=${arr[2]}"

# Array iteration
colors="red green blue"
for c in $colors; do
    echo "color: $c"
done

# String manipulation
s="Hello, World!"
echo "upper test"
echo "len=${#s}"

# Parameter expansion
default_val=""
echo "default=${default_val:-fallback}"
default_val="set"
echo "default=${default_val:-fallback}"

# Prefix/suffix removal
file="archive.tar.gz"
echo "basename=${file##*.}"
echo "name=${file%.*}"

# Nested arithmetic
result=$(( (2 + 3) * (4 - 1) ))
echo "nested=$result"

# Increment/decrement
i=5
i=$((i + 1))
echo "i=$i"
i=$((i - 1))
echo "i=$i"

# Multiple assignments
x=10; y=20; z=30
echo "sum=$((x + y + z))"

# Conditional assignment
a=""
b=${a:-default}
echo "b=$b"

# String comparison in [[ ]]
str1="abc"
str2="def"
if [[ "$str1" < "$str2" ]]; then
    echo "$str1 comes before $str2"
fi

# Glob-like pattern in case
ext="jpg"
case $ext in
    jpg|jpeg|png|gif)
        echo "image file"
        ;;
    mp4|avi|mkv)
        echo "video file"
        ;;
    *)
        echo "unknown"
        ;;
esac

# Function with return value via echo
square() {
    echo $(($1 * $1))
}
echo "5^2=$(square 5)"
echo "7^2=$(square 7)"

# Until-like loop
n=3
while [ $n -gt 0 ]; do
    echo "countdown: $n"
    n=$((n - 1))
done

# Test file operators
if [ -d /tmp ]; then
    echo "/tmp is a directory"
fi
if [ -w /tmp ]; then
    echo "/tmp is writable"
fi

echo "ADVANCED TEST 2 DONE"
