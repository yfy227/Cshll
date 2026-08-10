/*
 * shell2c.c — Shell-to-C Transpiler
 * Author: 爱摸鱼的狐狸
 *
 * Architecture (modular sections):
 *   header.inc   — includes, VM compiler, arithmetic compiler (L1)
 *   symtab.inc   — symbol table: C keywords, safe_cname, var/func/heredoc tables (L0+L2)
 *   ast.inc      — AST node constructors: new_node, new_redir (L3)
 *   tokenizer.inc— lexer: tokenize, pool_dup, expand_braces (L4)
 *   translate.inc— expression translation: translate_expr/arith (L5)
 *   expand.inc   — string expansion: expand_string, expand_cmd_subst (L6)
 *   cond.inc     — condition translation: translate_cond, translate_test (L7)
 *   emit.inc     — code emitter: emit_node, emit_word, emit_command (L8)
 *   parse.inc    — parser: dispatch_segment, parse_script (L9)
 *   runtime.inc  — runtime header (emitted into output C) (L10)
 *   main.inc     — prescan, main, threading (L11)
 */

#include "src/parts/header.inc"
#include "src/parts/symtab.inc"
#include "src/parts/ast.inc"
#include "src/parts/tokenizer.inc"
#include "src/parts/translate.inc"
#include "src/parts/expand.inc"
#include "src/parts/cond.inc"
#include "src/parts/emit.inc"
#include "src/parts/parse.inc"
#include "src/parts/runtime.inc"
#include "src/parts/main.inc"
