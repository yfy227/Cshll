#!/bin/bash
# 边界场景测试2: 嵌套引号/${}
echo "=== B2: nested quotes / ${} ==="

name="world"
echo "hello ${name}"
echo "hello ${name}!"
echo "nested: ${name}_${name}"

# 嵌套引号
echo "she said 'hi'"
echo 'he said "bye"'

# ${} 复杂用法
str="HelloWorld"
echo "len=${#str}"
echo "upper=${str^^}"
echo "lower=${str,,}"
echo "substr=${str:0:5}"
echo "substr2=${str:5}"
echo "default=${undef:-default_val}"
echo "alt=${undef:+set}"

# 命令替换嵌套在${}中
val="test"
echo "result=${val}_$(echo suffix)"
