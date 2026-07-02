#!/bin/bash
# Complex real-world script test

# String manipulation chains
filename="report_2024_01_15.txt"
base=${filename%.txt}
echo "base: $base"
year=${base#report_}
echo "year prefix removed: $year"
parts=(${year//_/ })
echo "year: ${parts[0]} month: ${parts[1]} day: ${parts[2]}"

# Associative array simulation
declare -A config
config[host]="localhost"
config[port]="8080"
config[debug]="true"

# Case with complex patterns
input="test.PY"
case "${input##*.}" in
    py|PY)
        echo "Python file"
        ;;
    sh|SH)
        echo "Shell file"
        ;;
    *)
        echo "Unknown: ${input##*.}"
        ;;
esac

# Nested function calls
get_path() {
    echo "/usr/local/$1"
}
get_config() {
    echo "$(get_path bin)/$1"
}
echo "config path: $(get_config app)"

# While read loop
echo -e "line1\nline2\nline3" | while IFS= read -r line; do
    echo "processed: $line"
done

# For loop with find-like
for f in file1 file2 file3; do
    ext="${f##*.}"
    name="${f%.*}"
    echo "$name has extension .$ext"
done

# String testing
str1="hello"
str2="world"
if [ "$str1" != "$str2" ]; then
    echo "strings differ"
fi
if [[ "$str1" < "$str2" ]]; then
    echo "$str1 before $str2"
fi

# Arithmetic in conditions
count=0
max=5
while [ $count -lt $max ]; do
    count=$((count + 1))
    if [ $((count % 2)) -eq 0 ]; then
        echo "$count is even"
    else
        echo "$count is odd"
    fi
done

# Function with local and global
g=100
test_scope() {
    local g=200
    echo "local g: $g"
}
test_scope
echo "global g: $g"

# Command substitution chains
result=$(echo $(echo $(echo nested)))
echo "nested: $result"

# Array manipulation
nums=(10 20 30 40 50)
sum=0
for n in "${nums[@]}"; do
    sum=$((sum + n))
done
echo "sum: $sum avg: $((sum / ${#nums[@]}))"

# String replacement
path="/usr/local/bin:/usr/bin:/bin"
echo "first: ${path%%:*}"
echo "rest: ${path#*:}"

echo "COMPLEX TEST DONE"
