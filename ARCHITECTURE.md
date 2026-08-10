# Architecture — shell2c 模块化架构

## 文件结构

```
shell2c.c              主入口 — 通过 #include 拼装各模块
src/
├── s2c_common.h        通用类型和工具函数（xstrdup, ltrim, rtrim等）
├── s2c_ast.h           AST节点类型定义（NodeType, Redir, Node）
├── s2c_symtab.h        符号表接口（变量/函数/heredoc管理）
├── s2c_parse.h         解析器接口
├── s2c_emit.h          代码生成接口
├── s2c_obfuscate.h     代码混淆接口
├── s2c_mangle.h        名称混淆接口
├── s2c_vm_compiler.h   VM编译器接口
├── s2c_vm_runtime.h    VM运行时接口
├── s2c_vm_isa.h        VM指令集定义
├── s2c_obfuscate.c     代码混淆实现
├── s2c_mangle.c        名称混淆实现
├── s2c_vm_compiler.c   VM字节码编译器
├── s2c_vm_runtime.c    VM运行时
├── s2c_vm_bridge.c     VM桥接层
└── parts/              主文件各职责模块（被 shell2c.c 通过 #include 引入）
    ├── header.inc      L1  — includes + VM编译器 + 算术编译器 (1232行)
    ├── symtab.inc      L0+L2 — 符号表: C关键字, safe_cname, 变量/函数/heredoc表 (166行)
    ├── ast.inc         L3  — AST构造器: new_node, new_redir (25行)
    ├── tokenizer.inc   L4  — 词法分析: tokenize, pool_dup, expand_braces (323行)
    ├── translate.inc   L5  — 表达式翻译: translate_expr, translate_arith (831行)
    ├── expand.inc      L6  — 字符串展开: expand_string, expand_cmd_subst (369行)
    ├── cond.inc        L7  — 条件翻译: translate_cond, translate_test (644行)
    ├── emit.inc        L8  — 代码生成: emit_node, emit_word, emit_command (2401行)
    ├── parse.inc       L9  — 解析器: dispatch_segment, parse_script (1341行)
    ├── runtime.inc     L10 — 运行时头(输出到C文件的runtime) (751行)
    └── main.inc        L11 — prescan + main + 线程 (434行)
```

## 职责划分

| 模块 | 职责 | 依赖 |
|------|------|------|
| header | includes, VM编译器, 算术编译器 | 所有头文件 |
| symtab | C关键字列表, safe_cname, 变量/函数/heredoc表 | s2c_common.h, s2c_symtab.h |
| ast | AST节点构造器 | s2c_ast.h |
| tokenizer | 词法分析, 括号匹配, 引号保护 | s2c_common.h |
| translate | shell表达式→C表达式翻译 | symtab |
| expand | 字符串展开($var, ${var}, $(), $(())) | symtab, translate |
| cond | 条件测试翻译([ ], [[ ]], -eq, -lt等) | symtab, translate |
| emit | AST→C代码生成 | 所有模块(输出) |
| parse | shell脚本→AST解析 | tokenizer, symtab, ast |
| runtime | 输出C文件中的runtime函数 | (纯数据) |
| main | 预扫描, 命令行, 多线程 | 所有模块 |

## 设计原则

1. **单一职责**: 每个模块只负责一个明确的职责
2. **单向依赖**: parse → ast → emit, 不反向依赖
3. **全局状态最小化**: 符号表是全局的(简化设计), 其他状态通过参数传递
4. **增量修改**: 定位问题时直接找到对应模块文件, 不需要翻8500行单文件
