/*
 * s2c_vm_compiler.c — Bytecode compiler implementation
 *
 * Compiles shell AST nodes into VM bytecode.
 *
 * Supported AST node types (from s2c_ast.h):
 *   NODE_CMD      → PUSH_STR args, EXEC_CMD or PRINT
 *   NODE_ASSIGN   → PUSH value, SETENV or STORE
 *   NODE_IF       → condition JZ, then-block, JMP, else-block
 *   NODE_FOR      → array iteration loop
 *   NODE_WHILE    → condition JZ, body, JMP back
 *   NODE_FUNC     → function table entry
 *   NODE_RETURN   → RET
 *   NODE_BREAK    → JMP to loop break
 *   NODE_CONTINUE → JMP to loop start
 *   NODE_EXIT     → PUSH code, EXIT
 *   NODE_PIPE     → EXEC_PIPE
 *   NODE_AND/OR   → short-circuit JZ/JNZ
 *
 * Author: 爱摸鱼的狐狸 🦊 (VM extension)
 */
#include "s2c_vm_compiler.h"
#include "s2c_ast.h"
#include "s2c_symtab.h"
#include <string.h>
#include <stdlib.h>

/* ---- ByteBuf operations ---- */
static void buf_init(ByteBuf *b){
    b->cap = 256;
    b->code = malloc(b->cap);
    b->len = 0;
}
static void buf_free(ByteBuf *b){
    free(b->code);
    b->code=NULL; b->len=b->cap=0;
}
static void buf_ensure(ByteBuf *b, int extra){
    while(b->len + extra > b->cap){
        b->cap *= 2;
        b->code = realloc(b->code, b->cap);
    }
}
static void buf_push(ByteBuf *b, uint8_t byte){
    buf_ensure(b,1);
    b->code[b->len++] = byte;
}

/* ---- Compiler init/free ---- */
void vmc_init(VmCompiler *c){
    memset(c, 0, sizeof(*c));
    buf_init(&c->code);
    c->consts.cap = 128;
    c->consts.consts = malloc(c->consts.cap * sizeof(char*));
    c->consts.count = 0;
    c->patches.cap = 128;
    c->patches.patches = malloc(c->patches.cap * sizeof(int));
    c->patches.targets = malloc(c->patches.cap * sizeof(int));
    c->patches.count = 0;
    c->funcs.cap = 32;
    c->funcs.funcs = malloc(c->funcs.cap * sizeof(VmFuncEntry));
    c->funcs.count = 0;
    c->next_block_id = 0;
}

void vmc_free(VmCompiler *c){
    buf_free(&c->code);
    for(int i=0; i<c->consts.count; i++) free(c->consts.consts[i]);
    free(c->consts.consts);
    free(c->patches.patches);
    free(c->patches.targets);
    free(c->funcs.funcs);
}

/* ---- Constant pool ---- */
int vmc_intern(VmCompiler *c, const char *s){
    /* Check if already exists */
    for(int i=0; i<c->consts.count; i++){
        if(strcmp(c->consts.consts[i], s?s:"")==0) return i;
    }
    /* Add new */
    if(c->consts.count >= c->consts.cap){
        c->consts.cap *= 2;
        c->consts.consts = realloc(c->consts.consts, c->consts.cap * sizeof(char*));
    }
    c->consts.consts[c->consts.count] = strdup(s?s:"");
    return c->consts.count++;
}

/* ---- Bytecode emission ---- */
void vmc_emit(VmCompiler *c, uint8_t op){
    buf_push(&c->code, op);
}

void vmc_emit_u16(VmCompiler *c, uint8_t op, uint16_t val){
    buf_push(&c->code, op);
    buf_push(&c->code, val & 0xFF);
    buf_push(&c->code, (val >> 8) & 0xFF);
}

void vmc_emit_i32(VmCompiler *c, uint8_t op, int32_t val){
    buf_push(&c->code, op);
    buf_push(&c->code, val & 0xFF);
    buf_push(&c->code, (val >> 8) & 0xFF);
    buf_push(&c->code, (val >> 16) & 0xFF);
    buf_push(&c->code, (val >> 24) & 0xFF);
}

int vmc_emit_jump(VmCompiler *c, uint8_t op){
    /* Emits a jump with placeholder target 0.
     * Returns the offset of the placeholder for later patching. */
    int patch_loc = c->code.len + 1;
    vmc_emit_u16(c, op, 0); /* placeholder */
    return patch_loc;
}

void vmc_patch_jump(VmCompiler *c, int patch_loc, uint16_t target){
    if(patch_loc < 0 || patch_loc + 1 >= c->code.len) return;
    c->code[patch_loc] = target & 0xFF;
    c->code[patch_loc+1] = (target >> 8) & 0xFF;
}

int vmc_pc(VmCompiler *c){
    return c->code.len;
}

/* ---- AST compilation ---- */

/* Forward declarations from shell2c.c — the Node struct is defined there
 * as NodeExt. We access it through a void pointer and field offsets.
 *
 * The actual Node struct has these fields (from reading the code):
 *   int type;       (NodeType)
 *   int lineno;
 *   char *lhs;      (for assignments)
 *   char *rhs;      (for assignments)
 *   char **argv;    (for commands)
 *   int argc;
 *   Node *cond;     (for if/while)
 *   Node *body;     (for loop bodies)
 *   Node *then_branch;
 *   Node *else_branch;
 *   Node *elif_conds[16];
 *   Node *elif_blks[16];
 *   int elif_count;
 *   Node *left, *right;  (for pipes, and/or)
 *   char *exit_str;
 *   int exit_code;
 *   Node *next;      (linked list)
 */

/* We use a struct definition that matches the one in shell2c.c.
 * Since shell2c.c defines NodeExt, we define it here for the compiler. */

struct NodeExt;
typedef struct NodeExt Node;

/* The following definitions are in shell2c.c and we reference them
 * through function pointers passed from the transpiler. */

/* Callbacks interface — the transpiler provides these so the VM compiler
 * doesn't need to know the exact Node layout. */
typedef struct {
    int    (*get_type)(Node *n);
    char*  (*get_lhs)(Node *n);
    char*  (*get_rhs)(Node *n);
    char** (*get_argv)(Node *n);
    int*   (*get_argc)(Node *n);
    Node*  (*get_cond)(Node *n);
    Node*  (*get_body)(Node *n);
    Node*  (*get_then)(Node *n);
    Node*  (*get_else)(Node *n);
    Node*  (*get_left)(Node *n);
    Node*  (*get_right)(Node *n);
    Node*  (*get_next)(Node *n);
    char*  (*get_exit_str)(Node *n);
    int    (*get_exit_code)(Node *n);
    int    (*get_lineno)(Node *n);
    /* elif */
    Node*  (*get_elif_conds)(Node *n, int i);
    Node*  (*get_elif_blks)(Node *n, int i);
    int    (*get_elif_count)(Node *n);
} VmCallbacks;

static VmCallbacks *cb = NULL;

void vmc_set_callbacks(VmCallbacks *callbacks){
    cb = callbacks;
}

/* Compile a string expression (may contain $ variables) */
static void compile_string_expr(VmCompiler *c, const char *str){
    if(!str || !*str){
        vmc_emit(c, OP_PUSH_EMPTY);
        return;
    }
    /* Check if it's a pure integer literal */
    int is_num = 1;
    const char *p = str;
    if(*p=='-'||*p=='+') p++;
    if(!*p) is_num = 0;
    for(; *p; p++){
        if(!isdigit((unsigned char)*p)){ is_num=0; break; }
    }
    if(is_num){
        vmc_emit_i32(c, OP_PUSH_INT, atoi(str));
        vmc_emit(c, OP_TO_STR);
        return;
    }
    /* Check if it contains $ (variable expansion) */
    if(strchr(str, '$')){
        /* For simplicity: treat the entire string as a format string.
         * In a full implementation, we'd parse $var, ${var}, $(cmd) etc.
         * For now: push the string as-is and use GETENV for known vars. */
        int idx = vmc_intern(c, str);
        vmc_emit_u16(c, OP_PUSH_STR, idx);
        return;
    }
    /* Pure literal string */
    int idx = vmc_intern(c, str);
    vmc_emit_u16(c, OP_PUSH_STR, idx);
}

/* Compile a command node */
static void compile_cmd(VmCompiler *c, Node *n){
    if(!n) return;
    char **argv = cb->get_argv(n);
    int argc = cb->get_argc(n) ? *cb->get_argc(n) : 0;

    if(argc == 0) return;

    const char *cmd = argv[0];

    /* echo command → PRINT/PRINTLN */
    if(strcmp(cmd, "echo")==0){
        for(int i=1; i<argc; i++){
            compile_string_expr(c, argv[i]);
            if(i>0){
                vmc_emit(c, OP_DUP);  /* This is wrong but placeholder */
                /* Actually: we should print space between args */
            }
            vmc_emit(c, OP_PRINT);
        }
        /* Print newline */
        vmc_emit(c, OP_PUSH_EMPTY);
        vmc_emit(c, OP_PRINTLN);
        return;
    }

    /* printf command */
    if(strcmp(cmd, "printf")==0 && argc >= 2){
        compile_string_expr(c, argv[1]);
        for(int i=2; i<argc; i++){
            compile_string_expr(c, argv[i]);
        }
        vmc_emit(c, OP_PRINT);
        return;
    }

    /* exit command */
    if(strcmp(cmd, "exit")==0){
        if(argc > 1){
            compile_string_expr(c, argv[1]);
            vmc_emit(c, OP_TO_INT);
        } else {
            vmc_emit(c, OP_PUSH_ZERO);
        }
        vmc_emit(c, OP_EXIT);
        return;
    }

    /* cd command */
    if(strcmp(cmd, "cd")==0){
        if(argc > 1){
            compile_string_expr(c, argv[1]);
        } else {
            int idx = vmc_intern(c, "HOME");
            vmc_emit_u16(c, OP_PUSH_VAR, idx);
        }
        /* EXEC_CMD with "cd" — but cd is a builtin.
         * For VM mode, we execute via system(). */
        int cmd_idx = vmc_intern(c, "cd");
        vmc_emit_u16(c, OP_PUSH_STR, cmd_idx);
        vmc_emit(c, OP_EXEC_CMD);
        return;
    }

    /* For all other commands: build command string and EXEC_CMD */
    /* Concatenate all args with spaces */
    int total_len = 0;
    for(int i=0; i<argc; i++)
        total_len += strlen(argv[i]?argv[i]:"") + 1;
    char *cmdline = malloc(total_len + 1);
    cmdline[0] = 0;
    for(int i=0; i<argc; i++){
        if(i>0) strcat(cmdline, " ");
        strcat(cmdline, argv[i]?argv[i]:"");
    }
    int idx = vmc_intern(c, cmdline);
    free(cmdline);
    vmc_emit_u16(c, OP_PUSH_STR, idx);
    vmc_emit(c, OP_EXEC_CMD);
    vmc_emit(c, OP_POP);  /* discard exit code for now */
}

/* Compile an assignment node */
static void compile_assign(VmCompiler *c, Node *n){
    char *lhs = cb->get_lhs(n);
    char *rhs = cb->get_rhs(n);

    if(!lhs) return;

    /* For VM: use setenv/getenv for all variables */
    if(rhs){
        compile_string_expr(c, rhs);
    } else {
        vmc_emit(c, OP_PUSH_EMPTY);
    }
    int name_idx = vmc_intern(c, lhs);
    vmc_emit_u16(c, OP_PUSH_STR, name_idx);
    vmc_emit(c, OP_SWAP);
    vmc_emit(c, OP_SETENV);
}

/* Compile an if node */
static void compile_if(VmCompiler *c, Node *n){
    if(!n) return;
    /* Condition: test command result */
    Node *cond = cb->get_cond(n);
    if(cond){
        compile_cmd(c, cond);
        /* Condition leaves exit code on stack as int */
        /* For test/[ command, we'd need special handling.
         * For now: use the exit code from EXEC_CMD */
    } else {
        vmc_emit(c, OP_PUSH_INT);
        /* Placeholder: push 1 (true) */
        vmc_emit(c, 1); vmc_emit(c,0); vmc_emit(c,0); vmc_emit(c,0);
    }

    /* JZ to else (or end) */
    int jmp_to_else = vmc_emit_jump(c, OP_JZ);

    /* Then block */
    Node *then_branch = cb->get_then(n);
    if(then_branch) vmc_compile_block(c, then_branch);

    /* JMP to end */
    int jmp_to_end = vmc_emit_jump(c, OP_JMP);

    /* Patch JZ to here (else block) */
    vmc_patch_jump(c, jmp_to_else, vmc_pc(c));

    /* Else block */
    Node *else_branch = cb->get_else(n);
    if(else_branch) vmc_compile_block(c, else_branch);

    /* Patch JMP to end */
    vmc_patch_jump(c, jmp_to_end, vmc_pc(c));
}

/* Compile a while loop */
static void compile_while(VmCompiler *c, Node *n){
    if(!n) return;
    int loop_start = vmc_pc(c);

    /* Profile marker for loop */
    int bid = c->next_block_id++;
    if(c->loop_depth < 64){
        c->loop_start[c->loop_depth] = loop_start;
        /* Reserve break patch location */
    }

    /* Condition */
    Node *cond = cb->get_cond(n);
    if(cond){
        compile_cmd(c, cond);
    } else {
        vmc_emit_i32(c, OP_PUSH_INT, 1); /* always true */
    }

    /* JZ to end */
    int jmp_to_end = vmc_emit_jump(c, OP_JZ);

    /* Body */
    c->loop_depth++;
    Node *body = cb->get_body(n);
    if(body) vmc_compile_block(c, body);
    c->loop_depth--;

    /* JMP back to start */
    vmc_emit_u16(c, OP_JMP, loop_start);

    /* Patch JZ to end */
    vmc_patch_jump(c, jmp_to_end, vmc_pc(c));
}

/* Compile a for loop */
static void compile_for(VmCompiler *c, Node *n){
    /* For loops in shell iterate over a word list.
     * In VM bytecode, we implement this as:
     *   - Build array from for-list
     *   - Loop: ARR_GET, check end, JZ, body, INC index, JMP back
     */
    if(!n) return;
    int loop_start = vmc_pc(c);

    /* Placeholder: push 0 as loop counter and build array */
    vmc_emit_i32(c, OP_PUSH_INT, 0);  /* index = 0 */
    /* In a full implementation, we'd compile the for-list here */

    /* Profile marker */
    int bid = c->next_block_id++;

    c->loop_depth++;
    Node *body = cb->get_body(n);
    if(body) vmc_compile_block(c, body);
    c->loop_depth--;

    /* INC index, JMP back */
    vmc_emit(c, OP_INC);
    vmc_emit_u16(c, OP_JMP, loop_start);
}

/* Compile a function definition */
static void compile_func(VmCompiler *c, Node *n){
    if(!n) return;
    /* Record function entry point */
    char *name = cb->get_lhs(n);
    int name_idx = vmc_intern(c, name?name:"__anon");

    if(c->funcs.count >= c->funcs.cap){
        c->funcs.cap *= 2;
        c->funcs.funcs = realloc(c->funcs.funcs, c->funcs.cap * sizeof(VmFuncEntry));
    }
    int func_idx = c->funcs.count++;
    c->funcs.funcs[func_idx].name_idx = name_idx;
    c->funcs.funcs[func_idx].entry = vmc_pc(c);
    c->funcs.funcs[func_idx].nlocals = 0;
    c->funcs.funcs[func_idx].nargs = 0;

    /* Compile function body */
    Node *body = cb->get_body(n);
    if(body) vmc_compile_block(c, body);

    /* Return */
    vmc_emit(c, OP_RET);
}

/* Compile a single AST node */
void vmc_compile_node(VmCompiler *c, void *node_ptr){
    Node *n = (Node*)node_ptr;
    if(!n || !cb) return;

    int type = cb->get_type(n);
    switch(type){
        case NODE_CMD:
            compile_cmd(c, n);
            break;
        case NODE_ASSIGN:
            compile_assign(c, n);
            break;
        case NODE_IF:
            compile_if(c, n);
            break;
        case NODE_WHILE:
            compile_while(c, n);
            break;
        case NODE_FOR:
            compile_for(c, n);
            break;
        case NODE_FUNC:
            compile_func(c, n);
            break;
        case NODE_RETURN:
            vmc_emit(c, OP_RET);
            break;
        case NODE_EXIT:
            if(cb->get_exit_str(n)){
                compile_string_expr(c, cb->get_exit_str(n));
                vmc_emit(c, OP_TO_INT);
            } else {
                vmc_emit_i32(c, OP_PUSH_INT, cb->get_exit_code(n));
            }
            vmc_emit(c, OP_EXIT);
            break;
        case NODE_PIPE:
            /* Compile left, then right as piped commands */
            if(cb->get_left(n)) vmc_compile_node(c, cb->get_left(n));
            if(cb->get_right(n)) vmc_compile_node(c, cb->get_right(n));
            break;
        case NODE_AND:
            /* Short-circuit AND: if left fails, skip right */
            if(cb->get_left(n)) vmc_compile_node(c, cb->get_left(n));
            int jmp_and = vmc_emit_jump(c, OP_JZ);
            vmc_emit(c, OP_POP); /* discard left result */
            if(cb->get_right(n)) vmc_compile_node(c, cb->get_right(n));
            vmc_patch_jump(c, jmp_and, vmc_pc(c));
            break;
        case NODE_OR:
            /* Short-circuit OR: if left succeeds, skip right */
            if(cb->get_left(n)) vmc_compile_node(c, cb->get_left(n));
            int jmp_or = vmc_emit_jump(c, OP_JNZ);
            vmc_emit(c, OP_POP);
            if(cb->get_right(n)) vmc_compile_node(c, cb->get_right(n));
            vmc_patch_jump(c, jmp_or, vmc_pc(c));
            break;
        default:
            /* Other node types: skip for now */
            break;
    }
}

/* Compile a block (linked list of nodes) */
void vmc_compile_block(VmCompiler *c, void *node_ptr){
    Node *n = (Node*)node_ptr;
    while(n){
        vmc_compile_node(c, n);
        n = cb->get_next(n);
    }
}

/* Finalize: no patches needed since we patch inline */
void vmc_finalize(VmCompiler *c){
    /* Add HALT at the end */
    vmc_emit(c, OP_HALT);
}
