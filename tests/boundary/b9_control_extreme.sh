#!/bin/bash
# 边界场景9: 控制流边界 — 深层嵌套/中断/标签

# 1. break N (跳出多层循环)
for i in 1 2 3; do
    for j in a b c; do
        if [ "$j" = "b" ]; then
            break 2
        fi
        echo "i=$i j=$j"
    done
done
echo "after_break2"

# 2. continue N
for i in 1 2 3; do
    for j in 1 2 3; do
        if [ $j -eq 2 ]; then
            continue 2
        fi
        echo "i=$i j=$j"
    done
done
echo "after_continue2"

# 3. case嵌套
val="outer1"
case "$val" in
    outer1)
        inner="inner1"
        case "$inner" in
            inner1) echo "nested_case_ok" ;;
            *) echo "nested_case_fail" ;;
        esac
        ;;
    *) echo "outer_default" ;;
esac

# 4. 空循环体
for i in 1 2 3; do :; done
echo "empty_loop=$i"

# 5. select模拟 (用case替代)
opt="quit"
case "$opt" in
    quit) echo "quitting" ;;
    *)   echo "unknown option" ;;
esac

# 6. 条件赋值链
x=5
[ $x -gt 3 ] && x="big" || x="small"
echo "cond_assign=$x"

# 7. 函数中return中断循环
find_item() {
    for i in "$@"; do
        if [ "$i" = "target" ]; then
            echo "found"
            return 0
        fi
    done
    echo "not_found"
    return 1
}
find_item a b target d
find_item a b c d

# 8. while read + 嵌套
echo -e "1 red\n2 blue\n3 green" | while read num color; do
    case $color in
        red)   echo "stop=$num" ;;
        blue)  echo "info=$num" ;;
        green) echo "ok=$num" ;;
    esac
done
