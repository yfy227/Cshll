#!/bin/bash
# 边界场景测试5: 控制流边界
echo "=== B5: control flow boundary ==="

# case fall-through and default
val="maybe"
case "$val" in
    yes|y) echo "affirmative" ;;
    no|n) echo "negative" ;;
    maybe) echo "uncertain" ;;
    *) echo "unknown" ;;
esac

# 嵌套if
x=10
if [ $x -gt 5 ]; then
    if [ $x -lt 20 ]; then
        echo "in range 5-20"
    else
        echo "too big"
    fi
else
    echo "too small"
fi

# 循环break/continue
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then
        continue
    fi
    if [ $i -eq 5 ]; then
        break
    fi
    echo "loop=$i"
done

# 嵌套循环break
for i in 1 2 3; do
    for j in a b c; do
        if [ "$j" = "b" ]; then
            break
        fi
        echo "outer=$i inner=$j"
    done
done

# 空case
case "" in
    "") echo "empty matched" ;;
    *) echo "not empty" ;;
esac

# while with complex condition
n=0
while [ $n -lt 3 ] && [ $n -ge 0 ]; do
    echo "while_n=$n"
    ((n++))
done
