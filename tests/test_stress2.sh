#!/bin/bash
# shell2c 第二轮压力测试 — 更复杂的真实世界脚本
set +e
S2C=/home/z/my-project/Cshll/shell2c
TMP=/tmp/s2c_stress2
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
        echo "FAIL: $id (transpile error)"
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
        return
    fi
    
    gcc -O2 -o "$file" "$file.c" 2>"$file.err"
    if [ $? -ne 0 ]; then
        echo "FAIL: $id (compile error)"
        head -3 "$file.err" | sed 's/^/  /'
        FAIL=$((FAIL+1)); FAILED_LIST="$FAILED_LIST $id"
        return
    fi
    
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
echo " shell2c 第二轮压力测试 (S51-S100)"
echo "======================================================="
echo ""

# ── 51-60: 深层嵌套组合 ──
stress_test "S51_if_elif_else_deep" '
x=5
if [ $x -eq 1 ]; then echo one
elif [ $x -eq 2 ]; then echo two
elif [ $x -eq 3 ]; then echo three
elif [ $x -eq 4 ]; then echo four
elif [ $x -eq 5 ]; then echo five
elif [ $x -eq 6 ]; then echo six
else echo other
fi'

stress_test "S52_nested_if_for" '
for i in 1 2 3; do
    if [ $i -eq 1 ]; then
        echo "first"
    elif [ $i -eq 2 ]; then
        echo "second"
    else
        echo "third"
    fi
done'

stress_test "S53_case_in_for" '
for cmd in start stop status; do
    case $cmd in
        start) echo "starting" ;;
        stop) echo "stopping" ;;
        status) echo "running" ;;
        *) echo "unknown" ;;
    esac
done'

stress_test "S54_nested_while_if" '
i=0
while [ $i -lt 3 ]; do
    j=0
    while [ $j -lt 3 ]; do
        if [ $i -eq $j ]; then
            echo "eq:$i:$j"
        else
            echo "ne:$i:$j"
        fi
        j=$((j+1))
    done
    i=$((i+1))
done'

stress_test "S55_func_with_loop" '
sum_to() {
    local n=$1
    local s=0
    local i=1
    while [ $i -le $n ]; do
        s=$((s + i))
        i=$((i + 1))
    done
    echo $s
}
echo "$(sum_to 10)"
echo "$(sum_to 100)"'

stress_test "S56_recursive_fib" '
fib() {
    if [ $1 -le 1 ]; then
        echo $1
    else
        local a=$(fib $(($1 - 1)))
        local b=$(fib $(($1 - 2)))
        echo $((a + b))
    fi
}
echo "fib10=$(fib 10)"'

stress_test "S57_pipe_in_var" '
lines=$(echo -e "a\nb\nc" | wc -l)
echo "lines=$lines"'

stress_test "S58_multi_redirect" '
echo "stdout1" > /tmp/s2c_stress2/r58.txt
echo "stdout2" >> /tmp/s2c_stress2/r58.txt
cat /tmp/s2c_stress2/r58.txt
echo "count=$(wc -l < /tmp/s2c_stress2/r58.txt)"'

stress_test "S59_subshell_var" '
x=outer
( x=inner; echo "sub:$x" )
echo "main:$x"'

stress_test "S60_group_var" '
x=before
{ x=changed; echo "group:$x"; }
echo "after:$x"'

# ── 61-70: 字符串处理 ──
stress_test "S61_param_expand_all" '
s="Hello World"
echo "len=${#s}"
echo "upper=$(echo $s | tr a-z A-Z)"
echo "lower=$(echo $s | tr A-Z a-z)"
echo "rep=${s//o/0}"
echo "prefix=${s%% *}"
echo "suffix=${s##* }"'

stress_test "S62_string_methods" '
path="/usr/local/bin/app"
echo "base=${path##*/}"
echo "dir=${path%/*}"
echo "ext=${path##*.}"
echo "noext=${path%.*}"'

stress_test "S63_default_vals" '
echo "a=${UNSET1:-default_a}"
echo "b=${UNSET2:=default_b}"
echo "c=${UNSET3:+set}"
echo "UNSET2=$UNSET2"'

stress_test "S64_nested_param" '
s="  hello world  "
trimmed="${s#"${s%%[![:space:]]*}"}"
trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
echo "[$trimmed]"'

stress_test "S65_ansi_c_quote" '
echo $'\t\ttabbed'
echo $'line1\nline2''

stress_test "S66_array_iteration" '
fruits=(apple banana cherry date elderberry)
for f in "${fruits[@]}"; do
    echo "fruit: $f"
done
echo "count=${#fruits[@]}"
echo "last=${fruits[-1]}"'

stress_test "S67_array_slice" '
arr=(a b c d e f g)
echo "${arr[@]:2:3}"
echo "${arr[@]:0:2}"
echo "${arr[@]:4}"'

stress_test "S68_assoc_complex" '
declare -A m
m[name]="test"
m[version]="1.0"
m[path]="/usr/bin"
for key in name version path; do
    echo "$key=${m[$key]}"
done
echo "name=${m[name]}"'

stress_test "S69_ifs_complex" '
data="a:b:c,d:e:f"
IFS=:
for x in $data; do
    echo "[$x]"
done
unset IFS
echo "back: $data"'

stress_test "S70_read_parse" '
echo "name age city" > /tmp/s2c_stress2/r70.txt
while read name age city; do
    echo "N=$name A=$age C=$city"
done < /tmp/s2c_stress2/r70.txt'

# ── 71-80: 管道与命令替换 ──
stress_test "S71_multi_pipe" '
echo "hello world" | tr a-z A-Z | tr " " "_" | rev'

stress_test "S72_cmd_subst_nested" '
inner=$(echo $(echo $(echo deep)))
echo "result=$inner"'

stress_test "S73_arith_in_cmd" '
x=5
y=$((x * 3 + 2))
echo "y=$y"
z=$((y / 2))
echo "z=$z"
echo "mod=$((y % 4))"'

stress_test "S74_redirect_in_func" '
logger() {
    echo "$1" >> /tmp/s2c_stress2/r74.log
}
logger "first"
logger "second"
logger "third"
cat /tmp/s2c_stress2/r74.log'

stress_test "S75_func_return_val" '
get_max() {
    if [ $1 -gt $2 ]; then
        echo $1
    else
        echo $2
    fi
}
max=$(get_max 10 20)
echo "max=$max"
max2=$(get_max 30 5)
echo "max2=$max2"'

stress_test "S76_while_read_pipe" '
echo -e "line1\nline2\nline3" | while read line; do
    echo "got: $line"
done'

stress_test "S77_tee_redirect" '
echo "data" | tee /tmp/s2c_stress2/r77a.txt | tr a-z A-Z
cat /tmp/s2c_stress2/r77a.txt'

stress_test "S78_xargs_pipe" '
echo "a b c" | tr " " "\n" | xargs -I{} echo "item:{}"'

stress_test "S79_here_string" '
cat <<< "hello here"'

stress_test "S80_process_subst" '
diff <(echo "a") <(echo "a") && echo "same"'

# ── 81-90: 算术与逻辑 ──
stress_test "S81_complex_arith2" '
a=10; b=3
echo "result=$(( (a + b) * (a - b) ))"
echo "div=$((a / b))"
echo "mod=$((a % b))"
echo "shift=$((a >> 1))"
echo "and=$((a & b))"
echo "or=$((a | b))"
echo "xor=$((a ^ b))"
echo "not=$((~a))"'

stress_test "S82_pre_post_increment" '
x=5
echo "x=$((x++))"
echo "x=$x"
echo "x=$((x--))"
echo "x=$x"
echo "x=$((++x))"
echo "x=$x"
echo "x=$((--x))"
echo "x=$x"'

stress_test "S83_ternary_arith" '
x=10
y=$(( x > 5 ? 100 : 200 ))
echo "y=$y"
z=$(( x > 20 ? 100 : 200 ))
echo "z=$z"'

stress_test "S84_float_arith" '
echo "result=$(echo "scale=2; 10/3" | bc)"'

stress_test "S85_logical_chain" '
x=5
[ $x -gt 0 ] && [ $x -lt 10 ] && echo "in range"
[ $x -gt 0 ] && [ $x -lt 10 ] && echo "pass1" || echo "fail1"
[ $x -gt 100 ] && echo "pass2" || echo "fail2"'

stress_test "S86_not_operator" '
x=5
if [ ! $x -eq 0 ]; then echo "nonzero"; fi
if [[ ! -z "$x" ]]; then echo "not empty"; fi'

stress_test "S87_for_arith_complex" '
sum=0
for ((i=1; i<=100; i++)); do
    sum=$((sum + i))
done
echo "sum=$sum"'

stress_test "S88_while_arith" '
i=0
sum=0
while ((i < 10)); do
    sum=$((sum + i))
    ((i++))
done
echo "sum=$sum"'

stress_test "S89_until_loop" '
x=0
until [ $x -ge 5 ]; do
    echo "x=$x"
    x=$((x + 1))
done'

stress_test "S90_break_continue" '
for i in 1 2 3 4 5 6 7 8 9 10; do
    if [ $((i % 2)) -eq 0 ]; then continue; fi
    if [ $i -eq 7 ]; then break; fi
    echo "odd: $i"
done'

# ── 91-100: 真实世界脚本片段 ──
stress_test "S91_config_parser" '
config_file() {
    echo "host=localhost"
    echo "port=8080"
    echo "debug=true"
}
while IFS="=" read -r key value; do
    echo "key=$key value=$value"
done < <(config_file)'

stress_test "S92_csv_parser" '
echo "name,age,city" > /tmp/s2c_stress2/r92.csv
echo "Alice,30,NYC" >> /tmp/s2c_stress2/r92.csv
echo "Bob,25,LA" >> /tmp/s2c_stress2/r92.csv
while IFS="," read name age city; do
    echo "Name: $name, Age: $age, City: $city"
done < /tmp/s2c_stress2/r92.csv'

stress_test "S93_file_counter" '
mkdir -p /tmp/s2c_stress2/dir93
touch /tmp/s2c_stress2/dir93/a.txt /tmp/s2c_stress2/dir93/b.txt /tmp/s2c_stress2/dir93/c.log
txt=0
for f in /tmp/s2c_stress2/dir93/*.txt; do
    txt=$((txt + 1))
done
echo "txt=$txt"'

stress_test "S94_env_manager" '
export APP_HOME=/opt/app
export APP_PORT=9090
export APP_DEBUG=0
echo "home=$APP_HOME"
echo "port=$APP_PORT"
echo "debug=$APP_DEBUG"
echo "all=$(env | grep APP_ | wc -l)"'

stress_test "S95_trap_cleanup" '
cleanup() {
    echo "cleaning up"
}
trap cleanup EXIT
echo "working"'

stress_test "S96_func_dispatch" '
greet() { echo "hello"; }
bye() { echo "goodbye"; }
action=$1
case $action in
    greet) greet ;;
    bye) bye ;;
    *) echo "unknown action" ;;
esac
' # Note: this test needs an argument

stress_test "S97_math_table" '
for i in 1 2 3 4 5; do
    for j in 1 2 3; do
        printf "%d " $((i * j))
    done
    echo ""
done'

stress_test "S98_text_processor" '
text="The quick brown fox"
words=0
for w in $text; do
    words=$((words + 1))
done
echo "words=$words"
echo "first=$(echo $text | cut -d" " -f1)"
echo "last=$(echo $text | rev | cut -d" " -f1 | rev)"'

stress_test "S99_state_machine" '
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
        stopped)
            echo "done"
            break
            ;;
    esac
done'

stress_test "S100_final_mega" '
#!/bin/bash
# 综合测试: 函数 + 递归 + 数组 + 关联数组 + 管道 + heredoc + case + for + 算术
factorial() {
    if [ $1 -le 1 ]; then echo 1
    else
        local prev=$(factorial $(($1 - 1)))
        echo $(($1 * prev))
    fi
}
declare -A stats
for i in 1 2 3 4 5; do
    f=$(factorial $i)
    stats[$i]=$f
    echo "$i! = $f"
done
total=0
for key in 1 2 3 4 5; do
    total=$((total + stats[$key]))
done
echo "total=$total"
echo "done"'

echo ""
echo "======================================================="
echo " 总计: PASS=$PASS  FAIL=$FAIL"
if [ -n "$FAILED_LIST" ]; then
    echo " 失败: $FAILED_LIST"
fi
echo "======================================================="
