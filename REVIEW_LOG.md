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
