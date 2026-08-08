#!/bin/bash
# shell2c 全面 Bug 测试套件
# 逐个测试 BUG-01 ~ BUG-19，输出 PASS/FAIL/PARTIAL
set +e
S2C=/home/z/my-project/Cshll/shell2c
TMP=/tmp/s2c_test
mkdir -p $TMP
PASS=0; FAIL=0; PARTIAL=0

run_test() {
    local id="$1" name="$2" script="$3" expected="$4"
    local file="$TMP/$id"
    echo "$script" > "$file.sh"
    $S2C "$file.sh" "$file.c" 2>/dev/null
    local tc_rc=$?
    if [ $tc_rc -ne 0 ]; then
        echo "FAIL: $id $name (transpile failed rc=$tc_rc)"
        FAIL=$((FAIL+1)); return
    fi
    gcc -O2 -o "$file" "$file.c" 2>"$file.gccerr"
    if [ $? -ne 0 ]; then
        echo "COMPILE_FAIL: $id $name"
        head -3 "$file.gccerr" | sed 's/^/  /'
        FAIL=$((FAIL+1)); return
    fi
    local got=$("$file" 2>&1)
    if [ "$got" = "$expected" ]; then
        echo "PASS: $id $name"
        PASS=$((PASS+1))
    else
        echo "DIFF: $id $name"
        echo "  expected: [$expected]"
        echo "  got:      [$got]"
        FAIL=$((FAIL+1))
    fi
}

echo "============================================"
echo " shell2c Bug 测试套件 (BUG-01 ~ BUG-19)"
echo "============================================"
echo ""

# BUG-01: 函数体为空
run_test "BUG-01" "函数体为空" \
'greet(){
    echo "hi $1"
}
greet "world"' \
"hi world"

# BUG-02: [[ ]] 走 system()
run_test "BUG-02" "[[ ]]条件测试" \
'x=7
if [[ $x -gt 5 && $x -lt 10 ]]; then echo "yes"; else echo "no"; fi' \
"yes"

# BUG-03: export 首次引入变量
run_test "BUG-03" "export首次引入变量" \
'export X=42
echo $X' \
"42"

# BUG-04: stderr 重定向
run_test "BUG-04a" "stderr重定向2>/dev/null" \
'ls /nonexistent_dir_xyz 2>/dev/null
echo "done"' \
"done"

# BUG-04b: 2>&1
run_test "BUG-04b" "stderr重定向2>&1" \
'echo "stdout line"
echo "stderr line" >&2' \
$'stdout line\nstderr line'

# BUG-05: printf 格式字符串
run_test "BUG-05" "printf格式%05d" \
'printf "%05d\n" 42' \
"00042"

# BUG-06: x+=str 追加
run_test "BUG-06" "字符串追加+=" \
'x="hello"
x+=" world"
echo $x' \
"hello world"

# BUG-07: return 退出码
run_test "BUG-07" "return退出码" \
'func(){
    return 3
}
func
echo $?' \
"3"

# BUG-08: local 作用域
run_test "BUG-08" "local作用域" \
'x="global"
func(){
    local x="local"
    echo $x
}
func
echo $x' \
$'local\nglobal'

# BUG-09: trap EXIT
run_test "BUG-09" "trap EXIT" \
'trap "echo bye" EXIT
echo hello' \
$'hello\nbye'

# BUG-10: 关联数组
run_test "BUG-10" "关联数组declare -A" \
'declare -A colors
colors[red]="#ff0000"
colors[blue]="#0000ff"
echo ${colors[red]}' \
"#ff0000"

# BUG-11: let 命令
run_test "BUG-11" "let算术" \
'let x=5+3
echo $x' \
"8"

# BUG-12: IFS 分割
run_test "BUG-12" "IFS分割" \
'IFS=":"
parts="a:b:c"
for p in $parts; do echo "[$p]"; done' \
$'[a]\n[b]\n[c]'

# BUG-13: 算术表达式嵌套
run_test "BUG-13" "算术嵌套((expr))" \
'x=$((1 + (2 * 3) - (4 / 2)))
echo $x' \
"5"

# BUG-14: declare -i +=
run_test "BUG-14" "declare -i +=" \
'declare -i n=5
n+=3
echo $n' \
"8"

# BUG-15: set -e
run_test "BUG-15" "set -e退出" \
'set -e
echo ok
false
echo not_reached' \
"ok"

# BUG-16: printf -v
run_test "BUG-16" "printf -v" \
'printf -v y "%d" 42
echo $y' \
"42"

# BUG-17: source
run_test "BUG-17" "source执行" \
'echo "MYVAR=hello" > /tmp/s2c_test/sourcelib.sh
source /tmp/s2c_test/sourcelib.sh
echo $MYVAR' \
"hello"

# BUG-18: $(...)内花括号展开
run_test "BUG-18" "\$(...)内花括号展开" \
'x=$(echo {1..5})
echo $x' \
"1 2 3 4 5"

# BUG-19: glob 展开
run_test "BUG-19" "glob展开" \
'touch /tmp/s2c_test/g_a.txt /tmp/s2c_test/g_b.txt
files=(/tmp/s2c_test/g_*.txt)
echo ${#files[@]}' \
"2"

echo ""
echo "============================================"
echo " 总计: PASS=$PASS  FAIL=$FAIL  PARTIAL=$PARTIAL"
echo "============================================"
