# Cshll 深度审视优化日志

## 审视目标
让 Cshll 项目逐步逼近"完备、完全独立、sh语法全覆盖、编译器解析器完备、模块化标准化"的终态。

## 审视标准（从严苛企业级视角）
- 缓冲区：所有固定大小缓冲区必须有边界检查
- 内存：每个malloc必须有对应的free，ownership清晰
- 错误处理：所有系统调用返回值必须检查
- 类型安全：尽可能在编译期推断类型
- 测试覆盖：每个bug修复必须有回归测试
- 模块化：每个模块有明确接口，单向依赖

---

### 2026-08-20 05:15 — 初始审视

**发现的问题：**
1. [P0-已修复] `var_hash_find()` 在 `var_hash_init()` 之前被调用时无限循环
   - 根因：`var_hash_buckets` BSS初始化为0（非-1），`var_hash_next[0]=0` 形成环
   - 影响：转译含 `heredoc + $VAR` 的脚本时无限挂起
   - 修复：在 `var_hash_find()` 内添加懒初始化守卫
2. [P0-已修复] 缺少 LICENSE 文件（README引用了MIT许可证但文件不存在）
3. [P1-待修复] test_realworld.sh 转译后编译失败（`__sfd_6_0` 重定义等）
4. [P1-待修复] 单文件巨石架构（24K行通过#include拼装）
5. [P1-待修复] 无单元测试框架
6. [P2-待修复] 生成的C代码有大量编译器警告

**测试基线：** 12/13 PASS（test_realworld 除外）

**已提交：** commit 45b6bcf

---

### 2026-08-21 01:50 — 第二轮审视（P1 正确性批次）

**发现并修复的问题：**

1. [P1-已修复] REG-02: `emit_redirs_save` 生成 `__sfd_<id>_<fd>` 变量名碰撞
   - 根因：同一命令的 Redir 链中两个条目都指向 fd=0（进程替换 + heredoc），变量名相同导致 C 重定义错误
   - 修复：改用 Redir 链位置索引 `<idx>` 命名，并将 `(myid<<16)|idx` 打包存入 `fd_high` 字段供 restore 解包

2. [P1-已修复] REG-03: 条件中用户函数调用的参数类型错误
   - 根因：`cond.inc` 两处用 `translate_expr` 生成 `char *__av[]` 元素，裸字（如 `4`）返回 int 字面量导致 `char*` 数组初始化类型错误
   - 修复：新增 `av_arg_expr()` 辅助函数——`$` 开头走 translate_expr（char* 表达式），裸字/引号字转义为 C 字符串字面量，保证类型正确

3. [P1-已修复] REG-04: heredoc 内容整体旋转错位（严重正确性 bug）
   - 根因：`done` 处理器在**解析时**用 `heredoc_peek()`（表非空）判断本行是否有 heredoc，但普通命令的 heredoc 在**生成时**才消费——`done < <(...)` 行偷走了前面 `cat <<EOF` 的 heredoc，全部错位一格
   - 修复：仅在 `done` 行 token 中真正含有 `<<` 运算符时才消费 heredoc
   
4. [P1-已修复] REG-05: `__sh_proc_subst` 天真 strtok 分词破坏 shell 语义
   - 根因：进程替换内容是任意 shell 代码（`;`、引号、管道），strtok+execvp 无法正确执行 `<(echo "a"; echo "b")`
   - 修复：含 shell 元字符（`;|&<>()"'$\`）时走 `execl("/bin/sh","sh","-c",...)`，纯简单命令保留 execvp 快路径

**测试结果：**
- 新增 `tests/regression_2026_08.sh` 回归测试套件（5 个用例，全部 PASS）
- 13 个既有测试：12 PASS + test_realworld 仅剩环境固有差异（$0 脚本名、$$ PID 值，属预期）

**审视发现（待后续处理）：**
- 生成的 C 代码有大量 `-Wformat-truncation` / `-Wstringop-truncation` 警告——固定 1024 字节缓冲区问题（P0 级，审核报告重点）
- `while read` 循环用 65536 字节 `__rb` 缓冲区 + `strncpy` 到 `line` 变量——超长行静默截断

---

### 2026-08-21 02:10 — 第三轮审视（P0 动态缓冲区批次）

**发现并修复的问题：**

5. [P0-已修复] REG-06: 变量自引用追加 `BIG="$BIG ..."` 生成 `snprintf(BIG,...,"%s...",BIG,...)` —— dest/src 重叠，C 标准未定义行为，glibc 下表现为每次循环丢失累积内容
   - 修复：展开赋值统一走 `__sh_fmt_dyn` 动态池临时变量再 `strncpy` 回写，彻底消除重叠类 UB（emit.inc 三处）

6. [P0-已修复] echo/命令参数插值临时缓冲 `char __tw_N[1024]` 静默截断（REG-07）
   - 修复：新增运行时 `__sh_fmt_dyn()`——8 槽旋转 + realloc 按需增长的堆缓冲格式化器，零截断、零泄漏（槽复用）；`__tw_N` 全部改走该路径
   - 附带修复：数组赋值处两处 `char __tw_N[65536]` 栈缓冲（64KB/语句的栈消耗风险）也改为堆池

**新增基础设施：**
- `__sh_fmt_dyn(fmt,...)`：动态增长格式化池（8 槽 × 按需 realloc），分配失败安全返回 ""
- 回归测试扩充至 7 用例（REG-06 自引用追加、REG-07 2KB 长变量插值）

**测试结果：** 7/7 回归 + 12/13 e2e（realworld 仅 $0/$$ 环境固有差异）

---

### 2026-08-21 04:00 — 第四轮审视（ASAN/LSan 系统性清零 + AST ownership 建立）

**发现并修复的问题：**

7. [P0-已修复] tokenizer.inc 栈缓冲区下溢读：`}` 分支读 `*(p-1)` 缺 `p>line` 守卫（`{` 分支有），行首 `}` 触发 ASAN stack-buffer-underflow（1 字节 UB 读）

8. [P1-已修复] cond.inc translate_cond 两处泄漏：`cp=strdup(...)` 和 `words[]` token 数组（每词一 malloc）用后未释放——分词后立即 free(cp)，return 前释放全部 words

9. [P1-已修复] parse.inc case 语句双重 strdup：`xstrdup(translate_expr(...))` 内层分配直接泄漏（ASAN: 6-byte direct leak）——先取值、复制、再释放内层

10. [P0-已修复] AST 全树无 ownership（审核报告"谁负责 free"条目）：新增递归 `free_node()`（ast.inc），覆盖全部 Node 字段（argv/for_list/redirs 链/case 分支/子树/兄弟链）；transpile 正常路径结束时释放 script 根 + 残留 pending_pipe_cmd；emit 消费点释放 detached 管道节点。VM 路径保守不释放（vm bridge 可能保留节点引用，短命进程退出回收），注释说明

**CI 升级（.github/workflows/ci.yml）：**
- 回归测试 REG-01..07 接入
- 12 个 e2e diff 测试接入（bash vs 转译产物逐脚本对比）
- ASAN+UBSAN+LSan 构建 + 全测试脚本扫描接入

**测试结果：** ASAN/LSan 13/13 clean（转译器侧零报告），回归 7/7，e2e 12/13（realworld 仅 $0/$$ 环境差异）

---

### 2026-08-21 04:30 — 第五轮审视（语法覆盖矩阵建立）

**新增基础设施：**
- `tests/run_syntax_tests.sh` — 语法覆盖矩阵框架：每个 sh 语法特性一个最小单测（.sh 内联 #__EXPECT__ 期望输出，框架净化后转译），支持子串过滤
- `tests/syntax/` — 23 个语法特性用例：引号/转义/变量展开/命令替换/算术/复合赋值/if-elif/for/while/case/函数/返回值/local/字符串操作/数组/管道/重定向/&&-||/heredoc/字符串比较/退出码
- `tests/syntax/gaps/` — 已知语法缺口隔离区（不阻塞基线）：
  - 021: `if ! cmd` 条件取反被丢弃（bash 输出两行，产物只输出一行）
  - 024: `( cmds )` 子 shell 回退 system() 且引号损坏

**框架自身修复：** 内联期望输出曾作为脚本内容被转译（#__EXPECT__ 之后的裸词被编译成 system() 调用，导致假失败）——净化剥离后转译

**测试结果：** 语法矩阵 23/23 PASS；全部回归 7/7；e2e 12/13（realworld 仅环境差异）

**CI：** 语法矩阵接入 GitHub Actions
