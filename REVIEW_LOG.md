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

---

### 2026-08-21 05:10 — 第六轮审视（语法 gap 清零：条件语义 + 子 shell）

**发现并修复的问题：**

11. [P0-已修复] gap-021: `if <command>` 条件语义根本性错误——`__sh_test_cmd` 用 popen 检查"是否产生输出"而非退出状态
    - 后果：`if grep -q pat file`（静默成功）判为假；`if ! false` 判为假
    - 修复：新增运行时 `__sh_cmd_status()`（system() + WEXITSTATUS，输出透传同 bash）；cond.inc 兜底路径改用之，并支持重复 `!` 前缀（极性：shell 真 ⟺ status==0 ⟺ C `!st`，奇数个 `!` 恰好抵消）
    - 语句级 `! cmd || ...` / `! cmd && ...` 同时验证通过

12. [P1-已修复] gap-024: 多行子 shell `( ... )` 完全不支持——裸 `(` 行 fallthrough 成 system() 损坏调用 + 作用域泄漏
    - 修复：parse.inc 新增多行 opener（BLK_SUBSHELL 帧 + 行内体按 `;` 分派）和 `)` closer（后随 token 回归外层流）；嵌套子 shell 亦通过（变量隔离语义正确）

**新增语法用例：**
- 026_negation_deep: `!` 五种形态（if 内/语句级/双重否定）
- 027_subshell_nested: 嵌套子 shell 变量隔离 + cwd 锁定

**测试结果：** 语法矩阵 27/27 PASS（gaps 目录清空）；回归 7/7；e2e 12/13（realworld 仅环境差异）；ASAN/LSan 新路径全清

---

### 2026-08-22 00:30 — 第七轮审视（外部严苛评审驱动的整体整改）

**背景**：外部评审指出核心结构性问题（单文件巨石、虚假性能声称、system() 承诺矛盾、不可验证的测试声称）+ 对本循环工作方式的直接批评（敷衍、引入新 bug、不考虑整体）。

**自我检讨（有据可查的失误）**：
- 编译产物误提交两次（6+2 个二进制）——提交前未检查 git status 完整性
- 测试期望值凭记忆写错两次（020 heredoc、027 子 shell 隔离语义）——未先跑 bash 验证
- __sh_fmt_dyn 重构时 fprintf 多传 2 个参数——粗心编辑
- 模块化拆分做一半留下悬空工作区——缺"要么完成要么回滚"纪律

**本轮整改成果：**

13. [P1-完成] 模块化拆分（审核报告首要结构性问题）
    - 11 个编译器 TU + 4 个支持 TU，src/s2c_all.h 单一 include 点
    - 跨模块符号声明归位到各自头文件；6 个共享 static 提升为外部链接（.c 与 .inc 双源同步修改——先前的"改一半"错误即源于只改了生成文件）
    - **验证纪律**：模块化 vs amalgamation 双构建零警告零错误；40/40 语料输出逐字节一致；全量三件套绿（语法 27/27、回归 7/7、e2e 12/12）
    - 死数据清除：AES S-box 常量字符串（零引用，20 行×2 处）
    - Makefile：模块化为默认构建，amalgamation 保留为交叉验证目标

14. [P0-完成] README 诚信化改造
    - 删除无 benchmark 支撑的 "-100%/-75%/-50%/-67%" 性能声称，改为分配行为描述表 + benchmark 计划说明
    - "无需 shell 运行时"的自包含声称加限定（仅限 117+ 内置命令子集）
    - VM 模式定位诚实化 + 写入实测一致性数字
    - 项目结构/构建说明/测试章节全部更新为真实状态
    - 许可证指向实际 LICENSE 文件

15. [P0-发现] VM 路径真实一致性实测：**2/6**（抽样 6 个代表性 e2e 脚本 vs bash）
    - 历史声称"192/200 零回归"在仓库中无可复现的测试基础设施支撑，已从声称体系移除
    - 失败定性：非挂起，为输出差异；主要缺口是 VM 路径文件重定向使用合成文件名（/tmp/s2c_redir_00.txt）
    - 后续迭代最高优先级：VM 重定向语义对齐 + 建立可复现的 VM 一致性测试套件

**工作纪律固化（响应批评）**：
- 提交前必查 git status 完整性（杜绝产物入库）
- 测试期望值必须 bash 实测生成（禁止凭记忆）
- 任何重构要么完成并用输出等价性证明，要么回滚
- 验证统计用精确模式（"warning:"/"error:"），避免把命令行标志误计为诊断

---

### 2026-08-22 04:30 — 第八轮审视（read -p 支持 + && 链管道解析修复）

**用户指令**：优先实现 read -p，再修其他 bug。

16. [P1-完成] `read -p` 提示符支持（此前 -p 和提示串被解析器直接丢弃，静默降级为裸 fgets）
    - emit.c：组合短标志解析（-r/-p/-s/-a 及 -rp/-pr/-rs 混合）；-p 提示符经 emit_word 展开（支持 $var 插值）后输出到 stderr，且仅当 stdin 是终端时显示（bash 精确语义：fputs+fflush(stderr)+isatty(0) 守卫）；-s 静默模式用 termios 关闭 ECHO，读毕恢复并补换行
    - parse.c make_cmd：同样的组合标志解析；无变量目标时注册 REPLY（修复 `read -p "x"` 编译错误）；while-read 条件路径同步跳过 -p 的参数（此前提示串会被当变量注册）
    - 验证：pipe 下零提示符泄漏、pty 下提示符正确显示（与 bash 同场景逐字节一致）、-s 密码不回显、REPLY/$((age+1))/${#pw} 全部正确
    - 测试期望值教训复发一次：028 用例先凭想象写了期望值失败，立即用 bash 实测（EOF stdin → `got:   REPLY= pwlen=0`）修正

17. [P1-已修复] REG-12: `&&` 链中管道被吞（两条执行路径共有）
    - 根因：parse.c 的 && 分支对左侧直接 make_cmd(toks,ai,...)，`echo hi | grep hi && echo matched` 把 `|` 和 `grep hi` 当成 echo 的字面参数
    - 修复：镜像 || 路径的正确模式——先 parser_append(NODE_AND)，再 parse_insert=&nd->left 递归 dispatch 左侧（无管道时自然落回 make_cmd）
    - 语义正确性：管道优先级高于 &&（`a | b && c` = `(a|b) && c`），递归 dispatch 天然满足
    - 该修复同时解锁 REG-08（VM $$/$? 用例曾因含 `| ... &&` 结构而失败）

**测试基线更新**：语法矩阵 28/28（新增 028_read_prompt）；回归 12/12（新增 REG-11/12）；e2e 12/12；双构建输出逐字节一致；VM 抽样 3/6（无回归，剩余缺口已记录：$(func) 赋值捕获、case 多模式 |）

**VM 一致性批次（上轮验证、本轮一并提交）**：$$（OP_GETPID 0x83 + dlsym）、$?（EXEC_CMD/EXEC_PIPE 导出 "?"）、echo/printf 重定向回退 EXEC_CMD、管道直通（EXEC_CAP 捕获改 EXEC_CMD，尾随换行存活）、dash 下 echo -e/-n 重写为 printf
