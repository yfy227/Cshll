#!/bin/bash
# Stress test — edge cases and real-world patterns

# 1. Nested command substitution
echo "nested: $(echo $(echo deep))"

# 2. Arithmetic in various contexts
x=5
y=10
echo $((x + y))
echo "$((x * y))"
echo $((x > 3 ? 1 : 0))

# 3. String with special chars
echo "tab:      end"
echo "quote: \"hello\""
echo 'single: $HOME'

# 4. Variable in various positions
prefix="pre"
suffix="suf"
echo "${prefix}middle${suffix}"

# 5. Multiple assignments on one line
a=1; b=2; c=3
echo "$a $b $c"

# 6. Conditional with command substitution
if [ "$(echo yes)" = "yes" ]; then
    echo "match"
fi

# 7. For loop with command substitution
for f in $(echo "a b c"); do
    echo "file: $f"
done

# 8. While read from pipe
echo -e "x\ny\nz" | while read -r line; do
    echo "got: $line"
done

# 9. Case with glob patterns
ext="JPG"
case "$ext" in
    jpg|JPG|jpeg|JPEG)
        echo "image"
        ;;
    *)
        echo "unknown"
        ;;
esac

# 10. Function with return value
is_positive() {
    if [ $1 -gt 0 ]; then
        return 0
    else
        return 1
    fi
}
if is_positive 5; then echo "positive"; fi
if ! is_positive -3; then echo "not positive"; fi

# 11. Nested if
n=15
if [ $n -gt 10 ]; then
    if [ $n -lt 20 ]; then
        echo "in range 10-20"
    fi
fi

# 12. Break and continue
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then continue; fi
    if [ $i -eq 5 ]; then break; fi
    echo "loop: $i"
done

# 13. String operations
s="hello world"
echo "upper: ${s^^}"
echo "lower: ${s,,}"
echo "replace: ${s/world/WORLD}"

# 14. Array iteration
arr=(red green blue)
for color in "${arr[@]}"; do
    echo "color: $color"
done
echo "count: ${#arr[@]}"

# 15. Default values chain
echo "${UNSET1:-${UNSET2:-${UNSET3:-deep_default}}}"

# 16. Arithmetic increment patterns
i=0
i=$((i + 1))
echo "i=$i"
i=$((i + 1))
echo "i=$i"

# 17. Echo with multiple args
echo a b c d e

# 18. Printf
printf "%s=%d\n" "key" 42

# 19. Test with && (modern style)
if [ -n "$x" ] && [ "$x" = "5" ]; then
    echo "x is 5 and non-empty"
fi

# 20. Subshell variable isolation
v="outer"
(v="inner"; echo "in sub: $v")
echo "after sub: $v"

echo "STRESS TEST DONE"
