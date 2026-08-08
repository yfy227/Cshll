#!/bin/bash
# shell2c 第三轮刁钻测试 — 边界条件、混淆场景、不常见但合法的bash语法
set +e
S2C=/home/z/my-project/Cshll/shell2c
TMP=/tmp/s2c_stress3
mkdir -p $TMP
PASS=0; FAIL=0
FAILED_LIST=""

stress_test() {
    local id="$1"
    local script="$2"
    local file="$TMP/$id"
    echo "$script" > "$file.sh"
    local expected=$(bash "$file.sh" 2>&1)
    $S2C "$file.sh" "$file.c" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "FAIL: $id (transpile)"
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"; return
    fi
    gcc -O2 -o "$file" "$file.c" 2>"$file.err"
    if [ $? -ne 0 ]; then
        echo "FAIL: $id (compile)"
        head -2 "$file.err" | sed 's/^/  /'
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"; return
    fi
    local got=$("$file" 2>&1)
    if [ "$got" = "$expected" ]; then
        echo "PASS: $id"
        PASS=$((PASS+1))
    else
        echo "FAIL: $id (diff)"
        echo "  exp: $(echo "$expected"|head -2|tr '\n' '|')"
        echo "  got: $(echo "$got"|head -2|tr '\n' '|')"
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
    fi
}

echo "======================================================="
echo " shell2c 第三轮刁钻测试 (S101-S150)"
echo "======================================================="
echo ""

# ── 变量赋值边界 ──
stress_test "S101_empty_var" 'x=""
echo "[$x]"'

stress_test "S102_var_with_spaces" 'x="hello world  multiple   spaces"
echo "[$x]"'

stress_test "S103_var_equals_in_value" 'x="a=b"
echo "$x"'

stress_test "S104_var_with_special" 'x="!@#$%^&*()"
echo "$x"'

stress_test "S105_nested_var_assign" 'x=y
z=$x
echo "$z"'

# ── 算术边界 ──
stress_test "S106_negative_arith" 'x=-5
echo $((0 - x))'

stress_test "S107_mod_neg" 'echo $((-7 % 3))'

stress_test "S108_div_trunc" 'echo $((7 / 2))
echo $((-7 / 2))'

stress_test "S109_zero_div" 'echo $((5 / 0))'  # bash returns error

stress_test "S110_pre_inc" 'x=5
echo $((++x))
echo $x'

stress_test "S111_post_inc" 'x=5
echo $((x++))
echo $x'

stress_test "S112_compound_assign" 'x=10
x+=5
echo $x
x-=3
echo $x
x*=2
echo $x'

stress_test "S113_ternary" 'x=5
echo $((x > 3 ? 100 : 200))'

stress_test "S114_bit_ops" 'echo $((5 & 3))
echo $((5 | 2))
echo $((5 ^ 1))
echo $((~5))
echo $((1 << 4))
echo $((16 >> 2))'

stress_test "S115_chained_arith" 'x=$((1 + 2 * 3 - 4 / 2))
echo $x'

# ── 字符串边界 ──
stress_test "S116_empty_str_len" 'x=""
echo ${#x}'

stress_test "S117_substr" 'x="hello world"
echo "${x:0:5}"
echo "${x:6}"
echo "${x: -5}"'

stress_test "S118_replace_first" 'x="a.b.c.d"
echo "${x/./-}"'

stress_test "S119_replace_all" 'x="a.b.c.d"
echo "${x//./-}"'

stress_test "S120_strip_prefix" 'x="hello.txt"
echo "${x#*.}"'

stress_test "S121_strip_suffix" 'x="hello.txt"
echo "${x%.*}"'

stress_test "S122_case_mod" 'x="hello"
echo "${x^^}"
y="WORLD"
echo "${y,,}"'

stress_test "S123_default_val" 'unset x
echo "${x:-default}"
echo "${x:=set}"
echo "$x"'

stress_test "S124_alt_val" 'x="exists"
echo "${x:+yes}"
unset y
echo "${y:+yes}"'

# ── 控制流边界 ──
stress_test "S125_empty_for" 'for x in; do
    echo "should not print"
done
echo "done"'

stress_test "S126_for_with_glob" 'touch /tmp/s2c_stress3/glob_a /tmp/s2c_stress3/glob_b
for f in /tmp/s2c_stress3/glob_*; do
    echo "found: $f"
done'

stress_test "S127_while_true_break" 'i=0
while true; do
    i=$((i + 1))
    if [ $i -ge 5 ]; then
        break
    fi
done
echo "i=$i"'

stress_test "S128_until_loop" 'i=0
until [ $i -ge 3 ]; do
    i=$((i + 1))
done
echo "i=$i"'

stress_test "S129_nested_break" 'for i in 1 2 3; do
    for j in 1 2 3; do
        if [ $j -eq 2 ]; then break; fi
        echo "i=$i j=$j"
    done
done'

stress_test "S130_continue" 'for i in 1 2 3 4 5; do
    if [ $((i % 2)) -eq 0 ]; then continue; fi
    echo "odd: $i"
done'

# ── 函数边界 ──
stress_test "S131_func_no_args" 'myfunc() {
    echo "argc=$#"
    echo "all=$@"
}
myfunc'

stress_test "S132_func_many_args" 'myfunc() {
    echo "count=$#"
    echo "first=$1"
    echo "last=${!#}"  # indirect: last arg
}
myfunc a b c d e'

stress_test "S133_func_return_in_loop" 'findit() {
    for x in "$@"; do
        if [ "$x" = "target" ]; then
            echo "found"
            return 0
        fi
    done
    echo "not found"
    return 1
}
findit apple banana target
findit apple banana cherry'

stress_test "S134_func_recursive_depth" 'countdown() {
    if [ $1 -le 0 ]; then
        echo "done"
        return
    fi
    echo -n "$1 "
    countdown $(($1 - 1))
}
countdown 5'

stress_test "S135_func_var_visibility" 'x="global"
myfunc() {
    echo "before: $x"
    local x="local"
    echo "after: $x"
}
myfunc
echo "outside: $x"'

# ── 条件测试边界 ──
stress_test "S136_negated_test" 'x=""
if [ ! -n "$x" ]; then
    echo "empty"
fi'

stress_test "S137_and_chain" 'x=5
[ $x -gt 0 ] && [ $x -lt 10 ] && echo "in range"'

stress_test "S138_or_chain" 'x=15
[ $x -lt 0 ] || [ $x -gt 10 ] && echo "out of range"'

stress_test "S139_nested_conditionals" 'x=5
y=10
if [ $x -gt 0 ]; then
    if [ $y -gt 5 ]; then
        if [ $x -lt $y ]; then
            echo "deep nested ok"
        fi
    fi
fi'

stress_test "S140_case_with_pipe" 'x="a|b"
case "$x" in
    *"|"*) echo "has pipe" ;;
    *) echo "no pipe" ;;
esac'

# ── 数组边界 ──
stress_test "S141_empty_array" 'arr=()
echo "count=${#arr[@]}"'

stress_test "S142_array_append" 'arr=()
arr+=("one")
arr+=("two")
arr+=("three")
echo "${arr[@]}"
echo "${#arr[@]}"'

stress_test "S143_array_index_gap" 'arr[0]=zero
arr[5]=five
arr[10]=ten
echo "${arr[0]} ${arr[5]} ${arr[10]}"
echo "${#arr[@]}"'

# ── 命令替换边界 ──
stress_test "S144_nested_cmd_subst" 'x=$(echo $(echo hello))
echo "$x"'

stress_test "S145_cmd_subst_in_string" 'x="result: $(echo value)"
echo "$x"'

stress_test "S146_arith_in_string" 'x=$((1 + 2))
y="value=$x"
echo "$y"'

stress_test "S147_backtick" 'x=`echo hello`
echo "$x"'

# ── heredoc 边界 ──
stress_test "S148_heredoc_no_expand" 'cat <<'\''EOF'\''
$HOME is not expanded
EOF'

stress_test "S149_heredoc_nested" 'cat <<OUTER
before inner
$(cat <<INNER
inner content
INNER
)
after inner
OUTER'

# ── 混合刁钻 ──
stress_test "S150_mixed_chaos" '# 函数定义+调用+数组+算术+条件+case
process() {
    local items=("$@")
    local total=0
    for item in "${items[@]}"; do
        case "$item" in
            [0-9]*) total=$((total + item)) ;;
            *) total=$((total + 1)) ;;
        esac
    done
    echo "total=$total"
}
process 1 2 3
process a b c
process 10 x 20'

echo ""
echo "======================================================="
echo " 总计: PASS=$PASS  FAIL=$FAIL"
if [ -n "$FAILED_LIST" ]; then
    echo " 失败: $FAILED_LIST"
fi
echo "======================================================="
