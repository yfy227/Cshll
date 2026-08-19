#!/bin/bash
# 边界场景6: 高级边界 — 深层引号嵌套/转义/特殊扩展

# 1. 嵌套命令替换中的引号
echo "test1: $(echo "inner quoted")"

# 2. 命令替换中的管道
echo "test2: $(echo a | tr a-z A-Z)"

# 3. 参数扩展中的特殊字符
s='a:b:c'
echo "test3: ${s//:/_}"

# 4. 默认值嵌套
echo "test4: ${UNSET:-${DEFAULT:-fallback}}"

# 5. 字符串中的$转义
echo "test5: \$HOME literal vs $HOME expanded"

# 6. 数组间接引用
arr=(x y z)
ref=arr
echo "test6: ${!ref}"

# 7. 特殊变量组合
echo "test7: $?=$$=$!"

# 8. 复杂heredoc带变量和命令替换
cat <<EOF
test8: PID=$$ line=$LINENO
test8b: $(echo nested)
EOF

# 9. 算术中的变量引用
x=10
y=20
echo "test9: $((x + y))"

# 10. 多行字符串拼接
multi="line1
line2
line3"
echo "test10: ${#multi}"

# 11. 正则中带特殊字符
s2='test_123'
if [[ $s2 =~ ^[a-z]+_[0-9]+$ ]]; then
    echo "test11: match"
fi

# 12. case with complex patterns
val="hello.tar.gz"
case "$val" in
    *.tar.gz) echo "test12: compressed" ;;
    *.tar)    echo "test12: tar only" ;;
    *)        echo "test12: unknown" ;;
esac
