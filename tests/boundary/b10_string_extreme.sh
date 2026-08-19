#!/bin/bash
# 边界场景10: 字符串边界 — 空串/特殊字符/Unicode/超长

# 1. 空字符串各种操作
s=""
echo "len=${#s}"
echo "upper=${s^^}"
echo "lower=${s,,}"
echo "rep=${s//x/y}"
echo "strip=${s#a}"
echo "default=${s:-empty}"

# 2. 单字符
s1="x"
echo "len1=${#s1}"
echo "sub1=${s1:0:1}"
echo "sub_oob=${s1:5}"

# 3. 超长字符串
long=$(printf 'a%.0s' {1..100})
echo "long_len=${#long}"
echo "long_sub=${long:0:5}...${long: -5}"

# 4. Unicode
u="héllo wörld"
echo "unicode_len=${#u}"
echo "unicode_upper=${u^^}"

# 5. 特殊字符组合
echo "tab_end	tab"
echo "multi    space"
echo "special: !@#\$%^&*()_+-=[]{}|;':\",./<>?"

# 6. 字符串替换边界
s2="aaa.bbb.ccc"
echo "replace_first=${s2/./-}"
echo "replace_last=${s2/%.ccc/.end}"
echo "replace_all=${s2//./-}"
echo "replace_prefix=${s2/#aaa/AAA}"
echo "no_match=${s2/xyz/NEW}"

# 7. 字符串截取边界
s3="hello"
echo "sub_start0=${s3:0}"
echo "sub_start_neg=${s3: -2}"
echo "sub_neg_len=${s3: -5:3}"
echo "sub_oob_neg=${s3: -100}"

# 8. 模式匹配
s4="test.tar.gz"
echo "longest_prefix=${s4##*.}"
echo "shortest_prefix=${s4#*.}"
echo "longest_suffix=${s4%%.*}"
echo "shortest_suffix=${s4%.*}"
