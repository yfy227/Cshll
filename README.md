# Cshll — Shell-to-C 转译器 (shell2c)

> **作者：爱摸鱼的狐狸** 🦊
>
> "把 shell 脚本编译成原生 C 二进制，让摸鱼更高效。"

---

## 项目简介

**Cshll** 是一个深度多层的 Shell-to-C 转译器。它读取 POSIX 风格的 shell 脚本（bash/sh 方言），输出一个自包含的 C 源文件。编译后即可作为原生二进制运行——无需 shell 运行时。

---

## 快速开始


# 构建转译器
gcc -O2 -Wall -o shell2c shell2c.c

# 转译脚本
./shell2c myscript.sh myscript.c

# 编译生成的 C 代码
gcc -O2 -Wall -o myscript myscript.c

# 运行
./myscript
```

### 便捷参数

| 参数          | 效果                                    |
|---------------|-----------------------------------------|
| `--makefile`  | 在输出旁生成 `Makefile`                 |
| `--run`       | 自动编译并执行生成的二进制              |

```bash
./shell2c demo.sh demo.c --makefile --run
```

---

## 架构设计（10 层）

转译器由十个协作层组成：

| 层   | 名称                 | 职责                                                |
|------|----------------------|-----------------------------------------------------|
| L1   | 分词器               | 词法分析：引号、运算符、`$((..))`、`;;`             |
| L2   | 预处理器             | 行续接、heredoc `<<EOF`、here-string `<<<`          |
| L3   | 解析器               | 递归块组装（if/for/while/case/func）                |
| L4   | AST                  | 类型化节点，带重定向元数据和子节点链接              |
| L5   | 符号表               | 变量类型跟踪（int/str/array）+ 作用域               |
| L6   | 表达式翻译器         | `$var`、`${...}` 系列、`$((...))` 算术              |
| L7   | 字符串展开器         | `printf` 风格格式合成                               |
| L8   | 条件翻译器           | `[ ]`、`[[ ]]`，完整 test(1) 运算符矩阵             |
| L9   | 代码生成器           | C 代码生成，fd 保存/恢复纪律                        |
| L10  | 运行时库             | 117+ 内置函数，嵌入输出文件                         |

### 关键设计决策

**多层分发。** 每个逻辑行在顶层 `;` 处分割（尊重引号和括号深度），每个片段独立通过完整的解析器分发——块关键字、赋值、管道、重定向和普通命令都流经统一的 `dispatch_segment()` 函数。

**fd 保存/恢复纪律。** 每个重定向和管道用 `dup()` 保存原始文件描述符，用 `dup2()` 应用新目标，运行命令，然后用 `dup2()` + `close()` 恢复。在任何 `dup2` 到 stdin 后调用 `clearerr(stdin)` 清除陈旧的 EOF/缓冲区状态——这对连续管道正确工作至关重要。

**数值 vs 字符串操作数检测。** 条件翻译器使用专用的 `translate_num_operand()`，避免将已经是整数的表达式（int 变量、`$?`、`$#`、`$((...))`）包裹在 `atoi()` 中，防止生成代码中的类型错误。

**用户函数命令替换。** 当 `$(fn args)` 引用用户定义函数时，转译器生成 `__sh_capture_fn()` 调用——fork 子进程，将子进程的 stdout 重定向到管道，调用函数，然后读回管道——而不是尝试将函数作为外部命令运行。

**性能优化。** for 循环使用栈分配的 `const char*` 数组（零堆操作），函数调用对 ≤8 个参数使用栈数组（无 malloc/strdup/free），临时缓冲区从 4KB 缩减到 1KB。

---

## 支持的 Shell 特性

### 变量与赋值

```bash
name="World"          # 字符串变量
count=42              # 整数变量（自动检测）
arr=(a b c)           # 数组
declare -i myint=42   # 声明整数
declare -a myarray    # 声明数组
declare -r myconst=x  # 声明只读
local var=val         # 局部变量（函数内）
var+=value            # 字符串追加
arr+=(d e f)          # 数组追加
```

### 变量展开

| 语法              | 含义                          |
|-------------------|-------------------------------|
| `$var`            | 简单展开                      |
| `${var}`          | 花括号展开                    |
| `${#var}`         | 字符串长度                    |
| `${#arr[@]}`      | 数组元素计数                  |
| `${var:-default}` | 未设置/空时使用默认值         |
| `${var:=default}` | 未设置/空时赋默认值           |
| `${var:+alt}`     | 已设置且非空时使用替代值      |
| `${var:offset}`   | 从偏移量取子串                |
| `${var:off:len}`  | 带长度的子串                  |
| `${var#pat}`      | 删除最短前缀匹配              |
| `${var##pat}`     | 删除最长前缀匹配              |
| `${var%pat}`      | 删除最短后缀匹配              |
| `${var%%pat}`     | 删除最长后缀匹配              |
| `${var/old/new}`  | 替换第一个匹配                |
| `${var//old/new}` | 替换所有匹配                  |
| `${var^^}`        | 转大写                        |
| `${var,,}`        | 转小写                        |
| `${arr[@]}`       | 所有数组元素                  |
| `${arr[*]}`       | 所有数组元素（单个词）        |
| `${arr[@]:1:2}`   | 数组切片（元素 1-2）          |
| `${arr[0]}`       | 单个数组元素                  |
| `$1`..`$9`        | 位置参数                      |
| `$#`              | 参数个数                      |
| `$?`              | 上一个退出状态                |
| `$$`              | 进程 ID                       |
| `$!`              | 上一个后台进程 PID            |
| `` `cmd` ``       | 反引号命令替换                |

### 算术 `$((...))` 和 `(( ))`

支持所有 C 算术运算符：

```bash
echo $((a + b))          # 加法
echo $((a * b))          # 乘法
echo $((a % b))          # 取模
echo $((a ** 2))         # 幂运算（通过 __sh_pow）
echo $((a << 2))         # 位左移
echo $((a & b))          # 位与
echo $((a > b ? 1 : 0))  # 三元运算
echo $((x++))            # 后自增
((y = x * 2))           # 算术赋值
if (( x > 5 )); then     # 算术测试
    echo "x > 5"
fi
```

### 条件判断

**`[ ]`（test 命令）—— 完整运算符矩阵：**

| 类别     | 运算符                                                  |
|----------|---------------------------------------------------------|
| 数值     | `-eq -ne -lt -le -gt -ge`                              |
| 字符串   | `= == != < >`                                          |
| 文件测试 | `-e -f -d -s -r -w -x -h -L -p -S -b -c -t -g -u -k`  |
| 文件比较 | `-nt -ot -ef`                                          |
| 字符串   | `-z -n`                                                |
| 变量     | `-v`（是否设置）                                       |
| 逻辑     | `! -a -o`                                              |

**`[[ ]]`（扩展测试）：**

```bash
if [[ $x -gt 5 && $x -lt 10 ]]; then ...
if [[ $str == *pattern* ]]; then ...
if [[ $str =~ ^[0-9]+$ ]]; then ...
```

### 控制流

```bash
if [ ... ]; then ...; elif [ ... ]; then ...; else ...; fi
for x in a b c; do ...; done
for ((i=0; i<10; i++)); do ...; done
while [ ... ]; do ...; done
until [ ... ]; do ...; done
case $x in
    pat1|pat2) cmd ;;
    *) cmd ;;
esac
```

### 函数

```bash
greet() {
    echo "Hello, $1!"
}
greet "World"

# 通过命令替换获取返回值
add() {
    echo $(($1 + $2))
}
result=$(add 3 4)
```

### 管道与重定向

```bash
cmd1 | cmd2 | cmd3           # 多级管道
cmd > file                   # stdout 到文件（截断）
cmd >> file                  # stdout 到文件（追加）
cmd 2> file                  # stderr 到文件
cmd > file 2>&1              # stdout+stderr 到文件
cmd < file                   # stdin 从文件
cmd <<< "text"               # here-string
cmd <<EOF                    # heredoc（变量展开）
line1
line2
EOF
cmd <<'EOF'                  # heredoc（字面量，不展开）
$var stays literal
EOF
cmd <<-EOF                   # heredoc（去除前导 tab）
	indented
EOF
diff <(cmd1) <(cmd2)         # 进程替换
```

### 花括号展开

```bash
echo {a,b,c}                 # → a b c
echo {1..10}                 # → 1 2 3 4 5 6 7 8 9 10
echo {a..z}                  # → a b c ... z
echo pre{x,y}post            # → prexpost preypost
echo file.{txt,sh,c}         # → file.txt file.sh file.c
```

### 复合命令

```bash
cmd1 && cmd2                 # 与
cmd1 || cmd2                 # 或
cmd1 ; cmd2                  # 顺序执行
cmd &                        # 后台
( cmd1; cmd2 )               # 子shell
{ cmd1; cmd2; }              # 命令组
```

---

## 内置命令（117 个原生 + 全系统命令透传）

生成的 C 二进制包含原生运行时库，直接实现以下命令（无需外部进程）：

### 输出
`echo` `printf` `yes` `seq`

### 文件操作
`cat` `cp` `mv` `rm` `ln` `touch` `mkdir` `rmdir` `mktemp` `install`
`basename` `dirname` `realpath` `readlink` `stat` `file` `du` `df`

### 目录
`ls` `pwd` `pushd` `popd` `dirs` `tree` `find`

### 文本处理
`head` `tail` `wc` `grep` `sort` `uniq` `cut` `paste` `tr` `rev` `tac`
`nl` `fold` `fmt` `expand` `unexpand` `column` `tee` `pr` `comm` `shuf`

### 搜索
`grep` `egrep` `fgrep` `rgrep` `find` `which` `whereis` `locate`

### 系统信息
`date` `whoami` `hostname` `uname` `id` `env` `logname` `tty` `uptime`
`free` `ps` `kill` `arch` `nproc` `dmesg` `lsof` `mount` `umount`

### Shell 内置
`cd` `pwd` `export` `unset` `set` `source` `eval` `read` `exit` `true`
`false` `test` `[` `type` `command` `alias` `unalias` `history` `trap`
`sleep` `wait` `jobs` `bg` `fg` `clear` `reset` `nohup` `time` `xargs`
`getopts` `hash` `builtin` `help` `dirs` `pushd` `popd`

### 系统命令透传
任何未识别为原生内置或用户函数的命令，自动通过 `system()` 传递给系统 shell 执行，支持所有系统命令：

- **归档**：`tar` `gzip` `gunzip` `bzip2` `xz` `zip` `unzip` `7z` `rar`
- **网络**：`curl` `wget` `ssh` `scp` `rsync` `nc` `ping` `dig` `ifconfig` `ip`
- **开发**：`gcc` `g++` `make` `cmake` `git` `svn` `docker` `kubectl` `ansible`
- **编辑器**：`vi` `vim` `emacs` `nano`
- **系统**：`systemctl` `service` `top` `htop` `pgrep` `pkill`
- **用户**：`su` `sudo` `passwd` `useradd` `usermod` `groupadd`
- **调试**：`gdb` `strace` `ltrace` `valgrind` `perf`
- **以及 200+ 更多命令**

---

## 生成的 C 代码结构

输出 C 文件具有清晰的结构：

```c
/* ============================================================
 * Generated by shell2c — Shell-to-C Transpiler
 * Source: myscript.sh
 * ============================================================ */

/* ---- shell2c runtime ---- */
#include <stdio.h>
/* ... 标准头文件 ... */

/* 运行时辅助函数 */
static void __sh_puts(const char *s);      /* fputs + 换行 */
static void __sh_putf(const char *fmt, ...); /* 格式化输出 + 换行 */
static void __sh_arr_free(char **arr);     /* 数组释放 */
static const char *__sh_capture_fn(...);   /* 函数输出捕获 */
/* ... 75+ 运行时函数 ... */

/* ---- 用户变量 ---- */
static char name[1024] = "";
static int count = 0;

/* ---- 函数声明 ---- */
static void greet(int, char**);

/* ---- 函数定义 ---- */
static void greet(int __sh_argc, char **__sh_args) {
    char __sh_arg1[1024] = "";
    if (__sh_argc >= 1) strncpy(__sh_arg1, __sh_args[0], 1023);
    __sh_puts("Hello, World!");
}

/* ---- main 入口 ---- */
int main(int _argc, char **_argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    __sh_argc = _argc - 1;
    if (_argc > 1) strncpy(__sh_arg1, _argv[1], 1023);
    /* ... 脚本体 ... */
    return __exit_status;
}
```

### 性能优化

生成的 C 代码经过以下优化：

| 优化项              | 优化前                          | 优化后                          | 提升   |
|---------------------|---------------------------------|---------------------------------|--------|
| for 循环            | malloc + N×strdup + N×free     | 栈分配数组，零堆操作            | -100%  |
| 函数调用 (≤8参数)   | malloc + N×strdup + free       | 栈分配数组                      | -100%  |
| 临时缓冲区          | 4096 字节                       | 1024 字节                       | -75%   |
| echo 字面量         | fputs + putchar('\n')          | `__sh_puts()` 单次调用          | -50%   |
| 数组清理            | for + free 循环                 | `__sh_arr_free()` 单次调用      | -67%   |

---

## 测试

```bash
# 运行测试套件
make test

# 或手动运行
cd tests
for t in test*.sh; do
    ../shell2c $t ${t%.sh}.c
    gcc -O2 -Wall -o ${t%.sh} ${t%.sh}.c
    diff <(bash $t 2>&1) <(./${t%.sh} 2>&1) && echo "PASS: $t" || echo "FAIL: $t"
done
```

### 测试覆盖

| 测试文件           | 覆盖内容                                           |
|--------------------|----------------------------------------------------|
| `test1.sh`         | 基础特性：变量、算术、if/for/while、case、函数     |
| `test2.sh`         | 重定向、管道、算术运算符、复合条件                 |
| `test3.sh`         | break/continue、here-string、case 模式、字符串比较 |
| `test4.sh`         | 边缘情况：嵌套 if、3 级管道、三元运算、while true  |
| `test5.sh`         | 数组、参数展开、前缀/后缀删除                      |
| `test6.sh`         | 字符串替换、嵌套命令替换、管道链、文件 I/O         |
| `test_compat.sh`   | declare、数组操作、字符串操作、函数、递归           |
| `test_complex.sh`  | 复杂真实脚本：字符串链、关联数组、case 模式         |
| `test_patterns.sh` | 常见 shell 模式：引号、条件赋值、测试运算符         |
| `test_realworld.sh`| 真实世界特性：反引号、heredoc、进程替换、正则      |
| `test_newfeat.sh`  | 新特性：花括号展开、进程替换、`(( ))`、幂运算       |
| `test_hd.sh`       | Heredoc 详细测试                                   |
| `twopipe.sh`       | 连续管道测试                                       |

---

## 构建从源码

```bash
gcc -O2 -Wall -o shell2c shell2c.c
```

无外部依赖——仅标准 C 库和 POSIX 头文件。

---

## 项目结构

```
Cshll/
├── shell2c.c          # 转译器（单文件，~5200 行）
├── README.md          # 本文件
├── Makefile           # 构建和测试
├── .gitignore
└── tests/
    ├── test1.sh       # 基础特性测试
    ├── test2.sh       # 综合特性测试
    ├── test3.sh       # break/continue、here-string
    ├── test4.sh       # 边缘情况
    ├── test5.sh       # 数组、参数展开
    ├── test6.sh       # 字符串替换、管道链
    ├── test_compat.sh # 兼容性测试
    ├── test_complex.sh# 复杂真实脚本
    ├── test_patterns.sh# 常见模式
    ├── test_realworld.sh# 真实世界特性
    ├── test_newfeat.sh # 新特性
    ├── test_hd.sh     # Heredoc 测试
    └── twopipe.sh     # 连续管道测试
```

---

## 已知限制

- **外部命令**：未识别为原生内置的命令通过 `system()` / `popen()` 回退，运行时需要 shell。对于完全自包含的二进制，请使用内置命令。
- **关联数组**：`declare -A` 支持有限，基础索引数组完整支持。
- **正则表达式**：`[[ =~ ]]` 翻译为 `regcomp`/`regexec`，复杂正则可能不完美。
- **动态 eval**：`eval` 和 `source` 回退到运行时 shell 调用。
- **信号处理**：`trap` 支持基础级别。


## 许可证

MIT — 见源文件头。

---

## 致谢

**作者：爱摸鱼的狐狸** 🦊



> "摸鱼不是为了偷懒，而是为了更高效地创造。" —— 爱摸鱼的狐狸
