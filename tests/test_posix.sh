#!/bin/bash
# POSIX sh compatibility test — features from POSIX.1-2008

# 1. Simple variable expansion
x="hello"
echo "$x world"

# 2. Command substitution with $()
today=$(echo "test123")
echo "result: $today"

# 3. Arithmetic expansion
a=10
b=3
echo $((a + b))
echo $((a - b))
echo $((a * b))
echo $((a / b))
echo $((a % b))

# 4. Parameter expansion
s="HelloWorld"
echo "${s}"
echo "${#s}"
echo "${s:0:5}"
echo "${s:5}"
echo "${s:-default}"
echo "${s:+set}"

unset undef
echo "${undef:-fallback}"

# 5. Pattern removal
file="archive.tar.gz"
echo "${file##*.}"
echo "${file%.*}"
echo "${file%%.*}"
echo "${file#*.}"

# 6. String replacement
text="hello world hello"
echo "${text/hello/hi}"
echo "${text//hello/hi}"

# 7. Case conversion
echo "${s^^}"
echo "${s,,}"

# 8. For loop
for i in 1 2 3; do
    echo "item: $i"
done

# 9. While loop
n=0
while [ $n -lt 3 ]; do
    echo "n=$n"
    n=$((n + 1))
done

# 10. If/elif/else
if [ $a -gt $b ]; then
    echo "a > b"
elif [ $a -eq $b ]; then
    echo "a == b"
else
    echo "a < b"
fi

# 11. Case
fruit="apple"
case $fruit in
    apple|pear)
        echo "pome"
        ;;
    orange|lemon)
        echo "citrus"
        ;;
    *)
        echo "other"
        ;;
esac

# 12. Functions
greet() {
    echo "Hello, $1!"
    echo "args: $#"
}
greet World
greet Alice Bob

# 13. Test operators
[ -n "$x" ] && echo "x is non-empty"
[ -z "" ] && echo "empty string"
[ "$x" = "hello" ] && echo "x equals hello"
[ "$a" -ne "$b" ] && echo "a != b"
[ -d /tmp ] && echo "/tmp is dir"
[ -f /etc/hostname ] && echo "hostname is file"

# 14. Logical operators
true && echo "and succeeded"
false || echo "or fallback"
[ $a -gt 5 ] && [ $a -lt 20 ] && echo "a in range"

# 15. Multiple commands
echo "first"; echo "second"

# 16. Pipe
echo -e "3\n1\n2" | sort -n | head -1

# 17. Redirect
echo "test" > /tmp/posix_test_$$.txt
cat /tmp/posix_test_$$.txt
rm -f /tmp/posix_test_$$.txt

# 18. Here-string
read -r first second <<< "hello world"
echo "first=$first second=$second"

# 19. Subshell
(subvar="inside"; echo "sub: $subvar")
echo "outside: ${subvar:-none}"

# 20. Special variables
echo "pid: $$"
echo "args: $#"

echo "POSIX TEST DONE"
