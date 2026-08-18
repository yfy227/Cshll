#!/bin/bash
# 边界场景测试1: 空字符串/特殊字符
echo "=== B1: empty string / special chars ==="

# 空字符串赋值和输出
s=""
echo "empty=[$s]"

# 特殊字符
echo "tab=[	end]"
echo "newline_in_echo=test"
echo 'single quote with $var'
echo "dollar_sign=\$HOME"
echo "backtick=\`echo hi\`"
echo "pipe=|literal|"
echo "ampersand=&literal&"
echo "semicolon=;literal;"
echo "parens=(literal)"
echo "brackets=[literal]"
echo "braces={literal}"

# 空参数
echo ""

# 连续空字符串拼接
a=""
b=""
c="$a$b$abc"
echo "concat_empty=[$c]"
