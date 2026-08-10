#!/bin/bash
# 回归测试矩阵：覆盖之前未被外部测试驱动发现的边界场景
# 这些用例针对的是"报告里没点名就搁着"的问题

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

S2C=./shell2c
PASS=0
FAIL=0
FAILED_TESTS=""

regression_test() {
    local name="$1"
    local script="$2"
    
    # Write script to temp file
    local sf="/tmp/reg_$name.sh"
    echo "$script" > "$sf"
    
    # Get bash output
    local bash_out
    bash_out=$(bash "$sf" 2>&1)
    
    # Compile with shell2c
    local cf="/tmp/reg_$name.c"
    if ! $S2C "$sf" "$cf" 2>/dev/null; then
        echo "FAIL: $name (transpile error)"
        FAIL=$((FAIL+1))
        FAILED_TESTS="$FAILED_TESTS $name"
        return
    fi
    
    # Compile C
    local bin="/tmp/reg_$name"
    if ! gcc -O2 -o "$bin" "$cf" 2>/dev/null; then
        echo "FAIL: $name (compile error)"
        FAIL=$((FAIL+1))
        FAILED_TESTS="$FAILED_TESTS $name"
        return
    fi
    
    # Run and compare
    local s2c_out
    s2c_out=$("$bin" 2>&1)
    
    if [ "$bash_out" = "$s2c_out" ]; then
        echo "PASS: $name"
        PASS=$((PASS+1))
    else
        echo "FAIL: $name (diff)"
        echo "  bash: $(echo "$bash_out" | head -3 | tr '\n' '|')"
        echo "  s2c:  $(echo "$s2c_out" | head -3 | tr '\n' '|')"
        FAIL=$((FAIL+1))
        FAILED_TESTS="$FAILED_TESTS $name"
    fi
}

echo "=== 回归测试矩阵 ==="
echo ""

# ── 1. printf 格式串转义 ──
echo "--- printf 格式串 ---"
regression_test "printf_basic_nl" 'printf "line1\nline2\n"'
regression_test "printf_tab" 'printf "col1\tcol2\n"'
regression_test "printf_var_nl" 'name="World"; printf "Hello, %s!\n" "$name"'
regression_test "printf_multi_args" 'printf "%-10s %3d %s\n" "Alice" 30 "Beijing"'
regression_test "printf_percent" 'printf "%d%% done\n" 50'
regression_test "printf_escape_mix" 'printf "a\tb\nc\td\n"'

# ── 2. while + heredoc 多列读取 ──
echo ""
echo "--- while + heredoc ---"
regression_test "while_heredoc_ifs" 'while IFS=":" read -r a b c; do echo "$a/$b/$c"; done << HD
Alice:30:Beijing
Bob:25:Shanghai
HD'

regression_test "while_heredoc_space" 'while read -r a b; do echo "[$a][$b]"; done << HD
hello world
foo bar
HD'

regression_test "while_heredoc_single" 'while read -r line; do echo "got: $line"; done << HD
first line
second line
HD'

# ── 3. while + 文件重定向 ──
echo ""
echo "--- while + file redirect ---"
echo -e "x:1\ny:2\nz:3" > /tmp/reg_data.txt
regression_test "while_file_redirect" 'while IFS=":" read -r k v; do echo "$k=$v"; done < /tmp/reg_data.txt'
rm -f /tmp/reg_data.txt

# ── 4. for 循环 + heredoc ──
echo ""
echo "--- for + heredoc ---"
regression_test "for_heredoc" 'for line in "alpha" "beta" "gamma"; do echo "item: $line"; done'

# ── 5. 嵌套 heredoc ──
echo ""
echo "--- nested heredoc ---"
regression_test "nested_heredoc" 'cat <<OUTER
before inner
$(cat <<INNER
inner content
INNER
)
after inner
OUTER'

# ── 6. heredoc 变量展开 ──
echo ""
echo "--- heredoc with vars ---"
regression_test "heredoc_var_expand" 'x=42; cat <<HD
value is $x
done
HD'

regression_test "heredoc_no_expand" 'x=42; cat <<"HD"
value is $x
done
HD'

# ── 7. xargs 管道多行 ──
echo ""
echo "--- xargs multi-line ---"
regression_test "xargs_multiline" 'echo "a b c" | tr " " "\n" | xargs -I{} echo "item:{}"'

# ── 8. tr 转义序列 ──
echo ""
echo "--- tr escape ---"
regression_test "tr_newline" 'echo "a,b,c" | tr "," "\n"'
regression_test "tr_tab" 'echo "a,b,c" | tr "," "\t"'
regression_test "tr_delete" 'echo "hello world" | tr -d " "'

# ── 9. 算术深度 ──
echo ""
echo "--- deep arithmetic ---"
regression_test "arith_100" 'x=$((1+(2+(3+(4+(5+(6+(7+(8+(9+(10+(11+(12+(13+(14+(15+(16+(17+(18+(19+(20+(21+(22+(23+(24+(25+(26+(27+(28+(29+(30+(31+(32+(33+(34+(35+(36+(37+(38+(39+(40+(41+(42+(43+(44+(45+(46+(47+(48+(49+(50+(51+(52+(53+(54+(55+(56+(57+(58+(59+(60+(61+(62+(63+(64+(65+(66+(67+(68+(69+(70+(71+(72+(73+(74+(75+(76+(77+(78+(79+(80+(81+(82+(83+(84+(85+(86+(87+(88+(89+(90+(91+(92+(93+(94+(95+(96+(97+(98+(99+100))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
echo "x=$x"'

# ── 10. 除零运行时检测 ──
echo ""
echo "--- division by zero ---"
regression_test "div_zero_continue" 'echo $((5 / 0))
echo "survived"'

# ── 11. 函数递归 ──
echo ""
echo "--- recursive function ---"
regression_test "recursive_fac" 'fac() {
    if [ $1 -le 1 ]; then echo 1
    else echo $(($1 * $(fac $(($1 - 1)))))
    fi
}
echo "5!=$(fac 5)"'

# ── 12. 关联数组引号下标 ──
echo ""
echo "--- assoc array quoted key ---"
regression_test "assoc_quoted" 'declare -A m
m["key"]="val"
echo "${m[key]}"
echo "${m["key"]}"'

# ── 13. IFS 前置赋值 + read -ra ──
echo ""
echo "--- IFS prefix + read -ra ---"
regression_test "ifs_read_ra" 'IFS=: read -ra p <<< "a:b:c"
echo "${p[0]} ${p[1]} ${p[2]}"'

# ── 14. bash -c 参数保留 $VAR ──
echo ""
echo "--- bash -c var expand ---"
regression_test "bash_c_var" 'export MYVAR="hello"
bash -c "echo $MYVAR"'

# ── 15. 内置命令 stderr 进管道 ──
echo ""
echo "--- builtin stderr to pipe ---"
regression_test "stderr_pipe" 'ls /nonexistent 2>&1 | grep -c "No such"'

# ── 16. let ** 幂运算 ──
echo ""
echo "--- let power ---"
regression_test "let_power" 'let x=2**8
echo "x=$x"'

# ── 17. 超长变量 ──
echo ""
echo "--- long var ---"
regression_test "long_var" 'x=$(printf "%0.sA" {1..5000})
echo "len=${#x}"'

# ── 18. 字符串比较 \< \> ──
echo ""
echo "--- string compare ---"
regression_test "str_compare" 'a="apple"
b="banana"
if [ "$a" \< "$b" ]; then echo "a before b"; fi
if [ "$b" \> "$a" ]; then echo "b after a"; fi'

# ── 19. case + heredoc ──
echo ""
echo "--- case + heredoc ---"
regression_test "case_heredoc" 'cat <<HD
line1
line2
HD'

# ── 20. if/else 单行 ──
echo ""
echo "--- single-line if/else ---"
regression_test "if_else_inline" 'x=3
if [ $x -gt 5 ]; then echo "big"; else echo "small"; fi'

# ── 21. 多重命令替换嵌套 ──
echo ""
echo "--- nested cmd subst ---"
regression_test "nested_cmd_subst" 'x=$(echo $(($1 + 1)))
echo "x=$x"'

# ── 22. printf -v ──
echo ""
echo "--- printf -v ---"
regression_test "printf_v" 'printf -v buf "%05d" 42
echo "[$buf]"'

echo ""
echo "================================"
echo "回归测试矩阵: PASS=$PASS FAIL=$FAIL"
if [ -n "$FAILED_TESTS" ]; then
    echo "失败: $FAILED_TESTS"
fi
echo "================================"
