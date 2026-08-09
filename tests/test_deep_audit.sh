#!/bin/bash
# 深度定制压力测试 — 逐项验证每个修复的精确行为
set +e
S2C=/home/z/my-project/Cshll/shell2c
TMP=/tmp/s2c_deep
mkdir -p $TMP
PASS=0; FAIL=0
FAILED_LIST=""

deep_test() {
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

echo "=== test3 深度验证 ==="

# D001: break/continue
deep_test "D001_break_continue" '
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then continue; fi
    if [ $i -eq 5 ]; then break; fi
    echo "i=$i"
done'

# D002: 函数+算术
deep_test "D002_func_arith" '
calc() { echo $(($1 * $2)); }
echo "5*7=$(calc 5 7)"'

# D003: ${#var} 字符串长度
deep_test "D003_string_length" '
s="hello"
echo "len=${#s}"
if [ ${#s} -eq 5 ]; then echo "ok"; fi'

# D004: stderr 2>/dev/null
deep_test "D004_stderr_redirect" '
echo "stderr test" >&2 2>/dev/null
echo "after stderr"'

# D005: here-string <<<
deep_test "D005_here_string" '
read -r first second <<< "hello world"
echo "first=$first second=$second"'

# D006: case with | patterns
deep_test "D006_case_pipe" '
day="sat"
case $day in
    sat|sun) echo "weekend" ;;
    mon|tue|wed|thu|fri) echo "weekday" ;;
esac'

# D007: 算术累加
deep_test "D007_arith_accumulate" '
counter=0
for i in 1 2 3; do counter=$((counter + i)); done
echo "total=$counter"'

# D008: 字符串比较 \<
deep_test "D008_str_compare_lt" '
a="apple"
b="banana"
if [ "$a" \< "$b" ]; then echo "$a before $b"; fi'

# D009: 字符串比较 \>
deep_test "D009_str_compare_gt" '
a="zebra"
b="apple"
if [ "$a" \> "$b" ]; then echo "$a after $b"; fi'

# D010: while 循环
deep_test "D010_while_loop" '
n=3
while [ $n -gt 0 ]; do echo "n=$n"; n=$((n - 1)); done'

# D011: -e 文件测试
deep_test "D011_file_test_e" '
if [ -e /tmp ]; then echo "/tmp exists"; fi'

# D012: -f -d 文件测试
deep_test "D012_file_test_fd" '
if [ -d /tmp ]; then echo "dir"; fi
if [ -f /etc/hostname ]; then echo "file"; fi'

echo ""
echo "=== test_realworld 深度验证 ==="

# D013: backtick 命令替换
deep_test "D013_backtick" '
YEAR=$(date +%Y)
echo "year=$YEAR"'

# D014: 嵌套命令替换
deep_test "D014_nested_cmd" '
dir=$(dirname /a/b/c.txt)
base=$(basename $dir)
echo "nested: $base"'

# D015: heredoc 到变量
deep_test "D015_heredoc_var" '
VAR=$(cat <<HEREDOC
line1
line2
line3
HEREDOC
)
echo "lines: $(echo "$VAR" | wc -l)"'

# D016: 不展开的 heredoc
deep_test "D016_heredoc_noexpand" '
cat <<'\''EOF'\''
$VAR stays literal
EOF'

# D017: $@ 在全局作用域
deep_test "D017_global_at" '
echo "args: $@"'

# D018: 关联数组
deep_test "D018_assoc_array" '
declare -A m
m["key"]="value"
echo "${m[key]}"}'

# D019: 函数递归
deep_test "D019_recursive_func" '
fact() {
    if [ $1 -le 1 ]; then echo 1
    else echo $(($1 * $(fact $(($1 - 1)))))
    fi
}
echo "5!=$(fact 5)"'

# D020: for 循环+数组
deep_test "D020_for_array" '
arr=(a b c)
for x in "${arr[@]}"; do echo "item:$x"; done'

echo ""
echo "=== BUG 修复深度验证 ==="

# D021: BUG-01 函数体为空
deep_test "D021_empty_func" '
empty() { :; }
empty
echo "done"'

# D022: BUG-02 [[ ]] && ||
deep_test "D022_brackets" '
x=5
[[ $x -gt 3 && $x -lt 10 ]] && echo "range"
[[ $x -eq 5 || $x -eq 6 ]] && echo "or"'

# D023: BUG-06 x+=str
deep_test "D023_append_str" '
s="hello"
s+=" world"
echo "$s"'

# D024: BUG-08 local
deep_test "D024_local_scope" '
f() { local x=10; echo "inner:$x"; }
x=5
f
echo "outer:$x"'

# D025: BUG-09 trap EXIT
deep_test "D025_trap_exit" '
cleanup() { echo "cleanup"; }
trap cleanup EXIT
echo "working"'

# D026: BUG-14 declare -i +=
deep_test "D026_declare_i" '
declare -i n=10
n+=5
echo "n=$n"'

# D027: BUG-17 source
deep_test "D027_source" '
echo "VAR=42" > /tmp/s2c_deep/src.sh
source /tmp/s2c_deep/src.sh
echo "VAR=$VAR"'

# D028: BUG-18 花括号展开
deep_test "D028_brace_expand" '
x=$(echo {1..5})
echo "$x"'

echo ""
echo "=== 压力测试修复深度验证 ==="

# D029: S18 裸单词比较
deep_test "D029_bareword_cmp" '
[[ "abc" == abc ]] && echo "eq"
[[ "abc" != xyz ]] && echo "ne"'

# D030: S19 regex
deep_test "D030_regex" '
s="hello123"
[[ $s =~ ^hello[0-9]+$ ]] && echo "match"
[[ $s =~ ^world ]] && echo "nomatch" || echo "no"'

# D031: S60 group变量
deep_test "D031_group_var" '
x=before
{ x=changed; echo "group:$x"; }
echo "after:$x"'

# D032: S69 IFS分割
deep_test "D032_ifs_split" '
data="a:b:c"
IFS=:
for x in $data; do echo "[$x]"; done
unset IFS'

# D033: S70 while read < file
deep_test "D033_while_read_file" '
echo "line1 line2 line3" > /tmp/s2c_deep/r033.txt
while read a b c; do echo "a=$a b=$b c=$c"; done < /tmp/s2c_deep/r033.txt'

# D034: S91 进程替换 <(func)
deep_test "D034_proc_subst" '
gen() { echo "x=1"; echo "y=2"; }
while IFS="=" read -r k v; do echo "k=$k v=$v"; done < <(gen)'

# D035: S92 while IFS=, read
deep_test "D035_csv_read" '
echo "Alice,30,NYC" > /tmp/s2c_deep/r035.csv
while IFS="," read name age city; do echo "n=$name a=$age c=$city"; done < /tmp/s2c_deep/r035.csv'

# D036: S95 trap 函数名
deep_test "D036_trap_func" '
on_exit() { echo "exiting"; }
trap on_exit EXIT
echo "main"'

# D037: S98 $(...) 内引号
deep_test "D037_cmd_subst_quotes" '
text="a b c"
echo "first=$(echo $text | cut -d\" \" -f1)"'

# D038: S99 case body 赋值
deep_test "D038_case_assign" '
state="init"
for event in start run stop; do
    case $state in
        init)
            case $event in
                start) state="running"; echo "->running" ;;
                *) echo "invalid" ;;
            esac
            ;;
        running)
            case $event in
                stop) state="stopped"; echo "->stopped" ;;
                run) echo "already running" ;;
                *) echo "invalid" ;;
            esac
            ;;
        stopped) echo "done"; break ;;
    esac
done'

# D039: S100 关联数组+算术
deep_test "D039_assoc_arith" '
declare -A stats
stats[1]=1
stats[2]=2
stats[3]=6
total=0
for key in 1 2 3; do
    total=$((total + stats[$key]))
done
echo "total=$total"'

# D040: S109 除零保护
deep_test "D040_div_zero" '
echo $((5 / 0))
echo "survived"'

# D041: S112 复合赋值 (shell2c支持, bash不支持)
deep_test "D041_compound_arith" '
x=10
x+=5
echo "x=$x"'

# D042: S125 空 for 列表
deep_test "D042_empty_for" '
for x in; do echo "should not print"; done
echo "done"'

# D043: S126 for glob
deep_test "D043_for_glob" '
touch /tmp/s2c_deep/g_a /tmp/s2c_deep/g_b
for f in /tmp/s2c_deep/g_*; do echo "found:$(basename $f)"; done'

# D044: S132 ${!#} 间接引用
deep_test "D044_indirect_last" '
f() { echo "last=${!#}"; }
f a b c d e'

# D045: S133 for x in "$@"
deep_test "D045_for_at" '
f() {
    for x in "$@"; do echo "arg:$x"; done
}
f a b c'

# D046: S140 case引号内管道符
deep_test "D046_case_quoted_pipe" '
x="a|b"
case "$x" in
    *"|"*) echo "has pipe" ;;
    *) echo "no pipe" ;;
esac'

# D047: S141 空数组
deep_test "D047_empty_array" '
arr=()
echo "count=${#arr[@]}"}'

# D048: S142 数组追加
deep_test "D048_array_append" '
arr=()
arr+=("one")
arr+=("two")
arr+=("three")
echo "${arr[@]}"
echo "${#arr[@]}"}'

# D049: S143 稀疏数组
deep_test "D049_sparse_array" '
arr[0]=zero
arr[5]=five
arr[10]=ten
echo "${arr[0]} ${arr[5]} ${arr[10]}"
echo "count=${#arr[@]}"}'

# D050: S150 local arr=("$@")
deep_test "D050_local_array_at" '
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
echo " 深度验证总计: PASS=$PASS  FAIL=$FAIL"
if [ -n "$FAILED_LIST" ]; then
    echo " 失败: $FAILED_LIST"
fi
echo "======================================================="
