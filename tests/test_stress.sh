#!/bin/bash
# shell2c 综合压力测试套件
# 逐个生成复杂脚本, 对比 bash vs shell2c 输出
# 任何差异都算 FAIL

set +e
S2C=/home/z/my-project/Cshll/shell2c
TMP=/tmp/s2c_stress
mkdir -p $TMP
PASS=0; FAIL=0
FAILED_LIST=""

stress_test() {
    local id="$1"
    local script="$2"
    local file="$TMP/$id"
    echo "$script" > "$file.sh"
    
    # bash baseline
    local expected=$(bash "$file.sh" 2>&1)
    
    # transpile
    $S2C "$file.sh" "$file.c" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "FAIL: $id (transpile error)"
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
        return
    fi
    
    # compile
    gcc -O2 -o "$file" "$file.c" 2>"$file.err"
    if [ $? -ne 0 ]; then
        echo "FAIL: $id (compile error)"
        head -3 "$file.err" | sed 's/^/  /'
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
        return
    fi
    
    # run and compare
    local got=$("$file" 2>&1)
    if [ "$got" = "$expected" ]; then
        echo "PASS: $id"
        PASS=$((PASS+1))
    else
        echo "FAIL: $id (output diff)"
        echo "  expected: $(echo "$expected" | head -3 | tr '\n' '|')"
        echo "  got:      $(echo "$got" | head -3 | tr '\n' '|')"
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
    fi
}

echo "======================================================="
echo " shell2c 综合压力测试"
echo "======================================================="
echo ""

# ── 1. 深层嵌套综合 ──
stress_test "S01_deep_if_50" '
x=0
for ((i=0;i<50;i++)); do x=$((x+1)); done
echo "x=$x"
if [ $x -eq 50 ]; then echo "if_50_ok"; fi'

stress_test "S02_deep_if_200" 'x=0
for ((i=0;i<200;i++)); do x=$((x+1)); done
if [ $x -eq 200 ]; then echo "deep_200_ok"; fi'

stress_test "S03_nested_for_arith" 'sum=0
for ((i=1;i<=10;i++)); do
  for ((j=1;j<=10;j++)); do
    sum=$((sum + i*j))
  done
done
echo "sum=$sum"'

stress_test "S04_while_until" 'x=5
y=0
while [ $x -gt 0 ]; do
  y=$((y + x))
  x=$((x - 1))
done
echo "y=$y"'

# ── 2. 函数综合 ──
stress_test "S05_func_recursive" 'fib(){
    local n=$1
    if [ $n -le 1 ]; then echo $n; return; fi
    local a=$(fib $((n-1)))
    local b=$(fib $((n-2)))
    echo $((a + b))
}
echo "fib(10)=$(fib 10)"'

stress_test "S06_func_params" 'greet(){
    echo "hello $1 $2 $3"
    echo "count=$#"
    echo "all=$@"
}
greet "a" "b" "c"'

stress_test "S07_func_local_scope" 'x="global"
modify(){
    local x="local"
    echo "inside: $x"
}
modify
echo "outside: $x"'

stress_test "S08_func_single_line" 'hi(){ echo hi; }
bye(){ echo bye; }
hi
bye'

stress_test "S09_func_nested_call" 'a(){ echo "a"; b; }
b(){ echo "b"; c; }
c(){ echo "c"; }
a'

# ── 3. 字符串处理 ──
stress_test "S10_string_ops" 's="hello world"
echo "len=${#s}"
echo "upper=${s^^}"
echo "lower=${s,,}"
echo "sub=${s:0:5}"
echo "sub2=${s:6}"
echo "rep=${s/world/WORLD}"
echo "repall=${s//o/0}"
echo "strip=${s#hel}"
echo "strip2=${s%orld}"'

stress_test "S11_string_append" 'x="hello"
x+=" world"
x+="!"
echo "$x"'

stress_test "S12_printf_formats" 'printf "%d\n" 42
printf "%05d\n" 42
printf "%-10s|\n" "hi"
printf "%.2f\n" 3.14159
printf "%x\n" 255
printf "%o\n" 8'

stress_test "S13_printf_v" 'printf -v result "%d+%d=%d" 2 3 5
echo "$result"
printf -v hex "%08X" 255
echo "$hex"'

# ── 4. 数组综合 ──
stress_test "S14_array_basic" 'arr=(apple banana cherry)
echo "count=${#arr[@]}"
echo "first=${arr[0]}"
echo "all=${arr[@]}"
echo "last=${arr[2]}"'

stress_test "S15_array_iter" 'arr=(1 2 3 4 5)
sum=0
for x in ${arr[@]}; do
  sum=$((sum + x))
done
echo "sum=$sum"'

stress_test "S16_assoc_array" 'declare -A m
m[red]=1
m[green]=2
m[blue]=3
echo "red=${m[red]}"
echo "blue=${m[blue]}"'

stress_test "S17_array_append" 'arr=(a b)
arr+=(c d)
echo "${arr[@]}"
echo "${#arr[@]}"'

# ── 5. 条件表达式 ──
stress_test "S18_test_brackets" 'x=5
[[ $x -gt 3 && $x -lt 10 ]] && echo "range"
[[ $x -eq 5 || $x -eq 6 ]] && echo "or"
[[ "abc" == abc ]] && echo "eq"
[[ "abc" != xyz ]] && echo "ne"
[[ -z "" ]] && echo "empty"
[[ -n "x" ]] && echo "nonempty"'

stress_test "S19_regex" 's="hello123"
[[ $s =~ ^hello[0-9]+$ ]] && echo "match"
[[ $s =~ ^world ]] && echo "nomatch" || echo "no"'

stress_test "S20_glob_pattern" '[[ "test.txt" == *.txt ]] && echo "txt"
[[ "test.py" == *.py ]] && echo "py"
[[ "test" != *.txt ]] && echo "nottxt"'

# ── 6. 算术表达式 ──
stress_test "S21_arith_complex" 'echo $((1 + 2 * 3 - 4 / 2))
echo $(((1 + 2) * (3 + 4)))
echo $((2 ** 10))
echo $((100 % 7))
echo $((10 > 5 ? 1 : 0))'

stress_test "S22_arith_nested" 'x=$((1 + (2 * (3 + 4) - (5 / 2))))
echo $x'

stress_test "S23_arith_increment" 'x=5
echo $((x++))
echo $x
echo $((++x))
echo $x'

# ── 7. 管道与重定向 ──
stress_test "S24_pipe_basic" 'echo "hello world" | grep "world"'

stress_test "S25_pipe_multi" 'echo -e "c\nb\na" | sort | head -1'

stress_test "S26_redirect_stderr" 'ls /nonexistent 2>/dev/null
echo "survived"'

stress_test "S27_heredoc" 'cat <<HEREDOC
line1
line2
HEREDOC'

stress_test "S28_heredoc_strip" 'cat <<-EOF
		stripped
	EOF'

# ── 8. 变量扩展 ──
stress_test "S29_default_value" 'echo ${UNSET:-default}
echo ${UNSET:=assigned}
echo $UNSET'

stress_test "S30_indirect" 'x=hello
echo ${!x}'

stress_test "S31_special_vars" 'echo "random=$([[ $RANDOM -ge 0 ]])"
echo "lineno exists=$([[ -n $LINENO ]])"
echo "hosttype=$([[ -n $HOSTTYPE ]])"'

# ── 9. IFS 与分词 ──
stress_test "S32_ifs_colon" 'IFS=":"
for x in $(echo "a:b:c"); do
  echo "[$x]"
done'

stress_test "S33_ifs_comma" 'IFS=","
for x in $(echo "x,y,z"); do
  echo "[$x]"
done'

# ── 10. 控制流 ──
stress_test "S34_case_fall" 'x=2
case $x in
  1) echo "one" ;;
  2) echo "two" ;;
  3) echo "three" ;;
  *) echo "other" ;;
esac'

stress_test "S35_case_default" 'x=99
case $x in
  1) echo "one" ;;
  *) echo "default" ;;
esac'

stress_test "S36_break_continue" 'for i in 1 2 3 4 5 6 7 8 9 10; do
  if [ $i -eq 5 ]; then continue; fi
  if [ $i -eq 8 ]; then break; fi
  echo $i
done'

stress_test "S37_set_e" 'set -e
echo "before"
false
echo "after"'

# ── 11. $(...) 命令替换 ──
stress_test "S38_cmd_subst" 'd=$(echo "hello")
echo "d=$d"
x=$(echo {1..5})
echo "x=$x"'

stress_test "S39_nested_cmd_subst" 'x=$(echo $(echo nested))
echo $x'

# ── 12. trap ──
stress_test "S40_trap_exit" 'trap "echo bye" EXIT
echo hello'

# ── 13. 复杂综合脚本 ──
stress_test "S41_realworld_mix" '#!/bin/bash
# 模拟配置解析
parse_config(){
    local key=$1 val=$2
    echo "setting $key=$val"
}
declare -A config
config[host]="localhost"
config[port]="8080"
config[debug]="true"
for key in host port debug; do
    parse_config "$key" "${config[$key]}"
done
echo "host=${config[host]}"
echo "done"'

stress_test "S42_math_pipeline" 'sum=0
for ((i=1;i<=100;i++)); do
  sum=$((sum + i))
done
echo "1+2+...+100=$sum"
echo "square=$((sum * sum))"'

stress_test "S43_string_manipulation" 'path="/usr/local/bin/app"
echo "basename=$(basename $path)"
echo "dirname=$(dirname $path)"
echo "replace=${path/local/global}"
echo "suffix=${path##*/}"
echo "prefix=${path%/*}"'

stress_test "S44_conditional_chain" 'x=42
[[ $x -gt 10 ]] && [[ $x -lt 100 ]] && echo "in range"
[[ $x -eq 42 ]] && echo "exact match" || echo "no match"'

# ── 14. 递归与栈深度 ──
stress_test "S45_deep_recursion" 'count_down(){
    local n=$1
    if [ $n -le 0 ]; then echo "done"; return; fi
    count_down $((n-1))
}
count_down 50'

stress_test "S46_factorial" 'fact(){
    local n=$1
    if [ $n -le 1 ]; then echo 1; return; fi
    local prev=$(fact $((n-1)))
    echo $((n * prev))
}
result=$(fact 10)
echo "10!=$result"'

# ── 15. 混合特性 ──
stress_test "S47_mixed_all" 'data="apple:banana:cherry"
IFS=":"
idx=0
for item in $data; do
    arr[$idx]=$item
    idx=$((idx+1))
done
echo "items=${#arr[@]}"
echo "first=${arr[0]}"
echo "last=${arr[2]}"'

stress_test "S48_heredoc_var" 'name="world"
cat <<EOF
Hello, $name!
EOF'

stress_test "S49_export_env" 'export MY_VAR=test123
echo "direct=$MY_VAR"
child=$(echo $MY_VAR)
echo "child=$child"'

stress_test "S50_complex_arith" 'a=10; b=3
echo "add=$((a+b))"
echo "sub=$((a-b))"
echo "mul=$((a*b))"
echo "div=$((a/b))"
echo "mod=$((a%b))"
echo "pow=$((b**2))"
echo "neg=$((0-a))"
echo "shift=$((a>>1))"
echo "and=$((a&b))"
echo "or=$((a|b))"'

echo ""
echo "======================================================="
echo " 总计: PASS=$PASS  FAIL=$FAIL"
if [ -n "$FAILED_LIST" ]; then
    echo " 失败: $FAILED_LIST"
fi
echo "======================================================="
