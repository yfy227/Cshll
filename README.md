# Cshll — Shell-to-C Transpiler (shell2c)

**Cshll** is a deep, multi-layered shell-to-C transpiler. It reads a POSIX-ish
shell script (bash/sh dialect) and emits a single, self-contained C source file
that, when compiled, reproduces the script's behavior as a native binary — no
shell runtime required.

The transpiler itself is a single ~4 500-line C program (`shell2c.c`). It
parses the shell script into an AST and emits C code with a rich embedded
runtime library of 60+ builtin commands.

## Quick Start

```bash
# Build the transpiler
gcc -O2 -Wall -o shell2c shell2c.c

# Transpile a script
./shell2c myscript.sh myscript.c

# Compile the generated C
gcc -O2 -Wall -o myscript myscript.c

# Run
./myscript
```

### Convenience flags

| Flag          | Effect                                                    |
|---------------|-----------------------------------------------------------|
| `--makefile`  | Write a `Makefile` next to the output                     |
| `--run`       | Compile and execute the generated binary automatically   |

```bash
./shell2c demo.sh demo.c --makefile --run
```

## Architecture (10 Layers)

The transpiler is structured as a pipeline of ten cooperating layers:

| Layer | Name                 | Responsibility                                              |
|-------|----------------------|-------------------------------------------------------------|
| L1    | Tokenizer            | Word splitting with quotes, operators, `$((..))`, `;;`      |
| L2    | Preprocessor         | Line continuation, heredoc `<<EOF`, here-string `<<<`       |
| L3    | Parser               | Recursive block assembly (if/for/while/case/func)           |
| L4    | AST                  | Typed nodes with redirect metadata and child links          |
| L5    | Symbol table         | Variable kind tracking (int/str/array) + scope              |
| L6    | Expr translator      | `$var`, `${...}` family, `$((...))` arithmetic              |
| L7    | String expander      | `printf`-style format synthesis for word expansion          |
| L8    | Cond translator      | `[ ]`, `[[ ]]` with full test(1) operator matrix            |
| L9    | Code emitter         | C codegen with fd save/restore discipline                   |
| L10   | Runtime library      | 60+ builtin functions emitted into the output               |

### Key Design Decisions

**Multi-layered dispatch.** Each logical line is split on top-level `;`
(respecting quotes and bracket depth) into segments. Each segment is
dispatched independently through the full parser — block keywords,
assignments, pipes, redirects, and plain commands all flow through one
unified `dispatch_segment()` function.

**fd save/restore discipline.** Every redirect and pipe saves the original
file descriptor with `dup()`, applies the new target with `dup2()`, runs the
command, and restores the original with `dup2()` + `close()`. After any
`dup2` onto stdin, `clearerr(stdin)` is called to clear stale EOF/buffer
state — this is critical for consecutive pipes to work correctly.

**Numeric vs string operand detection.** The condition translator uses a
dedicated `translate_num_operand()` that avoids wrapping already-integer
expressions (int variables, `$?`, `$#`, `$((...))`) in `atoi()`, preventing
type errors in the generated C.

**User-function command substitution.** When `$(fn args)` references a
user-defined function, the transpiler emits a `__sh_capture_fn()` call that
forks, redirects the child's stdout to a pipe, calls the function, and reads
the pipe back — instead of trying to run the function as an external command.

## Supported Shell Features

### Variables & Assignment

```bash
name="World"          # string variable
count=42              # integer variable (auto-detected)
arr=(a b c)           # array
declare -i myint=42   # declare integer
declare -a myarray    # declare array
declare -r myconst=x  # declare readonly
local var=val         # local variable (in function)
var+=value            # string append
arr+=(d e f)          # array append
```

### Variable Expansion

| Syntax             | Meaning                                  |
|--------------------|------------------------------------------|
| `$var`             | Simple expansion                         |
| `${var}`           | Braced expansion                         |
| `${#var}`          | String length                            |
| `${#arr[@]}`       | Array element count                      |
| `${var:-default}`  | Use default if unset/empty               |
| `${var:=default}`  | Assign default if unset/empty            |
| `${var:+alt}`      | Use alt if set & non-empty               |
| `${var:offset}`    | Substring from offset                    |
| `${var:off:len}`   | Substring with length                    |
| `${var#pat}`       | Remove shortest prefix match             |
| `${var##pat}`      | Remove longest prefix match              |
| `${var%pat}`       | Remove shortest suffix match             |
| `${var%%pat}`      | Remove longest suffix match              |
| `${var/old/new}`   | Replace first occurrence                 |
| `${var//old/new}`  | Replace all occurrences                  |
| `${var^^}`         | Uppercase conversion                     |
| `${var,,}`         | Lowercase conversion                     |
| `${arr[@]}`        | All array elements                       |
| `${arr[*]}`        | All array elements (single word)         |
| `${arr[@]:1:2}`    | Array slice (elements 1-2)               |
| `${arr[0]}`        | Single array element                     |
| `$1`..`$9`         | Positional parameters                    |
| `$#`               | Argument count                           |
| `$?`               | Last exit status                         |
| `$$`               | Process ID                               |
| `$!`               | Last background PID                      |
| `` `cmd` ``        | Backtick command substitution            |

### Arithmetic `$((...))` and `(( ))`

All C arithmetic operators are supported:

```bash
echo $((a + b))      # addition
echo $((a * b))      # multiplication
echo $((a % b))      # modulo
echo $((a ** 2))     # power (via __sh_pow)
echo $((a << 2))     # bit shift
echo $((a & b))      # bitwise and
echo $((a > b ? 1 : 0))  # ternary
echo $((x++))        # post-increment
((y = x * 2))       # arithmetic assignment
if (( x > 5 )); then # arithmetic test
    echo "x > 5"
fi
```

### Conditionals

**`[ ]` (test command) — full operator matrix:**

| Category  | Operators                                               |
|-----------|---------------------------------------------------------|
| Numeric   | `-eq -ne -lt -le -gt -ge`                              |
| String    | `= == != < >`                                          |
| File test | `-e -f -d -s -r -w -x -h -L -p -S -b -c -t -g -u -k`  |
| File cmp  | `-nt -ot -ef`                                          |
| String    | `-z -n`                                                |
| Var       | `-v` (is set)                                          |
| Logic     | `! -a -o`                                              |

**`[[ ]]` (extended test):**

```bash
if [[ $x -gt 5 && $x -lt 10 ]]; then ...
if [[ $str == *pattern* ]]; then ...
if [[ $str =~ ^[0-9]+$ ]]; then ...
```

### Control Flow

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

### Functions

```bash
greet() {
    echo "Hello, $1!"
}
greet "World"

# With return value via command substitution
add() {
    echo $(($1 + $2))
}
result=$(add 3 4)
```

### Pipes & Redirects

```bash
cmd1 | cmd2 | cmd3           # multi-stage pipeline
cmd > file                   # stdout to file (truncate)
cmd >> file                  # stdout to file (append)
cmd 2> file                  # stderr to file
cmd > file 2>&1              # stdout+stderr to file
cmd < file                   # stdin from file
cmd <<< "text"               # here-string
cmd <<EOF                    # heredoc (variables expanded)
line1
line2
EOF
cmd <<'EOF'                  # heredoc (literal, no expansion)
$var stays literal
EOF
cmd <<-EOF                   # heredoc (strip leading tabs)
        indented
EOF
diff <(cmd1) <(cmd2)         # process substitution
```

### Brace Expansion

```bash
echo {a,b,c}                 # → a b c
echo {1..10}                 # → 1 2 3 4 5 6 7 8 9 10
echo {a..z}                  # → a b c ... z
echo pre{x,y}post            # → prexpost preypost
echo file.{txt,sh,c}         # → file.txt file.sh file.c
```

### Compound Commands

```bash
cmd1 && cmd2                 # and
cmd1 || cmd2                 # or
cmd1 ; cmd2                  # sequential
cmd &                        # background
( cmd1; cmd2 )               # subshell
{ cmd1; cmd2; }              # group
```

## Builtin Commands (60+)

The generated C binary includes a runtime library implementing these commands
natively (no external process spawning):

### Output
`echo` `printf` `yes` `seq`

### File operations
`cat` `cp` `mv` `rm` `ln` `touch` `mkdir` `rmdir` `mktemp` `install`
`basename` `dirname` `realpath` `readlink` `stat` `file` `du` `df`

### Directory
`ls` `pwd` `pushd` `popd` `dirs` `tree` `find`

### Text processing
`head` `tail` `wc` `grep` `sort` `uniq` `cut` `paste` `tr` `rev` `tac`
`nl` `fold` `fmt` `expand` `unexpand` `columns` `tee` `pr`

### Search
`grep` `find` `which` `whereis` `locate`

### System information
`date` `whoami` `hostname` `uname` `id` `env` `logname` `tty` `uptime`
`free` `ps` `kill`

### Shell builtins
`cd` `pwd` `export` `unset` `set` `source` `eval` `read` `exit` `true`
`false` `test` `[` `type` `command` `alias` `unalias` `history` `trap`
`sleep` `wait` `jobs` `bg` `fg` `clear` `nohup` `time` `xargs` `getopts`

### String/math
`expr` `test`

## Generated Code Structure

The output C file has this structure:

```c
#include <stdio.h>
/* ... standard headers ... */

/* === Runtime library (60+ builtin functions) === */
static int __b_echo(int argc, char **argv, ...);
static int __b_cat(int argc, char **argv, ...);
/* ... etc ... */
static const char *__sh_capture_fn(void (*fn)(int,char**), ...);
static char *__sh_cmd_output(const char *cmd);
/* ... helpers ... */

/* === Runtime globals === */
static int __exit_status = 0;
static int __sh_argc = 0;
static char __sh_arg1[1024] = "";
/* ... */

/* === User globals (variables) === */
static char __sh_name[65536] = "World";
static int __sh_count = 0;

/* === User functions === */
static void __sh_greet(int __sh_argc, char **__sh_args) { ... }

/* === Main === */
int main(int _argc, char **_argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    /* ... script body ... */
    return __exit_status;
}
```

## Testing

```bash
# Run the test suite
cd tests
for t in test1.sh test2.sh; do
    ../shell2c $t ${t%.sh}.c
    gcc -O2 -Wall -o ${t%.sh} ${t%.sh}.c
    diff <(bash $t) <(./${t%.sh}) && echo "PASS: $t" || echo "FAIL: $t"
done
```

## Limitations

- **External commands:** Commands not in the builtin list fall back to
  `system()` / `popen()`, which requires a shell at runtime. For fully
  self-contained binaries, stick to builtins.
- **Arrays:** Basic array support (`arr=(a b c)`, `${arr[0]}`); advanced
  array operations (`${arr[@]:1:2}`, associative arrays) are limited.
- **Regex in `[[ =~ ]]`:** Translated to `regcomp`/`regexec` for basic
  patterns; complex regexes may not translate perfectly.
- **Dynamic eval:** `eval` and `source` fall back to runtime shell calls.
- **Signal handling:** `trap` is supported at a basic level.

## Building from Source

```bash
gcc -O2 -Wall -o shell2c shell2c.c
```

No external dependencies — only standard C library and POSIX headers.

## Project Structure

```
Cshll/
├── shell2c.c          # The transpiler (single file, ~3400 lines)
├── README.md          # This file
├── Makefile           # Build the transpiler
└── tests/
    ├── test1.sh       # Basic features test
    ├── test2.sh       # Comprehensive features test
    └── twopipe.sh     # Consecutive pipe test
```

## License

MIT — see source header.

## Credits

Original concept by 爱摸鱼的狐狸. Deep optimization with multi-layered
architecture, 60+ builtins, fd save/restore discipline, and user-function
command substitution.
