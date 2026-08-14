/*
 * s2c_vm_isa.h — Cshll Virtual Machine ISA Definition
 *
 * Architecture: Hybrid Stack-Register Machine
 *   - Primary data interface: operand stack (4096 slots)
 *   - Auxiliary: 8 general registers (r0-r7) for hot-path native code
 *   - Each value is a tagged union (VmValue)
 *
 * Design rationale:
 *   - Stack machine = smaller bytecode, harder to reverse-engineer
 *   - Register file = enables efficient JIT native compilation for hot paths
 *   - Single-byte opcodes (0x00-0xFF) with inline operands
 *
 * Author: 爱摸鱼的狐狸 🦊 (VM extension)
 */
#ifndef S2C_VM_ISA_H
#define S2C_VM_ISA_H

#include <stdint.h>

/* ---- Bytecode format ---- */
#define S2C_VM_MAGIC0 'S'
#define S2C_VM_MAGIC1 '2'
#define S2C_VM_MAGIC2 'C'
#define S2C_VM_MAGIC3 'V'
#define S2C_VM_VERSION  1

/* VM flags */
#define VM_FLAG_OBFUSCATED  0x01
#define VM_FLAG_JIT_ENABLED 0x02

/* Stack and register limits */
#define VM_STACK_SIZE   4096
#define VM_REG_COUNT    8
#define VM_LOCAL_SLOTS  256
#define VM_MAX_CONSTS   4096
#define VM_MAX_CODE     65536
#define VM_MAX_FUNCS    256

/* JIT hot-path threshold */
#define JIT_HOT_THRESHOLD 100

/* ---- Opcodes (0x00 - 0xFF) ---- */
typedef enum {
    /* 0x00-0x0F: Stack and data movement */
    OP_NOP       = 0x00,  /* no operation */
    OP_PUSH_STR  = 0x01,  /* push const pool string [u16 idx] */
    OP_PUSH_INT  = 0x02,  /* push integer [i32 value] */
    OP_PUSH_VAR  = 0x03,  /* push variable [u16 name_idx] */
    OP_POP       = 0x04,  /* pop and discard */
    OP_DUP       = 0x05,  /* duplicate top */
    OP_SWAP      = 0x06,  /* swap top two */
    OP_LOAD      = 0x07,  /* load local [u8 slot] */
    OP_STORE     = 0x08,  /* store local [u8 slot] */
    OP_LOAD_REG  = 0x09,  /* load register [u8 reg] */
    OP_STORE_REG = 0x0A,  /* store register [u8 reg] */
    OP_PUSH_EMPTY= 0x0B,  /* push empty string "" */
    OP_PUSH_ZERO = 0x0C,  /* push integer 0 */

    /* 0x10-0x1F: Arithmetic */
    OP_ADD       = 0x10,  /* pop b, pop a, push (a+b) */
    OP_SUB       = 0x11,
    OP_MUL       = 0x12,
    OP_DIV       = 0x13,
    OP_MOD       = 0x14,
    OP_INC       = 0x15,  /* increment top (integer) */
    OP_DEC       = 0x16,  /* decrement top (integer) */
    OP_POW         = 0x18,  /* power: pop exp, pop base, push base^exp */
    OP_SHL         = 0x19,  /* shift left: pop b, pop a, push a<<b */
    OP_SHR         = 0x1A,  /* shift right: pop b, pop a, push a>>b */
    OP_BITAND      = 0x1B,  /* bitwise and: pop b, pop a, push a&b */
    OP_BITOR       = 0x1C,  /* bitwise or: pop b, pop a, push a|b */

    /* 0x20-0x2F: Comparison and logic */
    OP_EQ        = 0x20,
    OP_NE        = 0x21,
    OP_LT        = 0x22,
    OP_GT        = 0x23,
    OP_LE        = 0x24,
    OP_GE        = 0x25,
    OP_NOT       = 0x26,  /* logical not */
    OP_AND       = 0x27,  /* logical and (short-circuit handled by JZ/JNZ) */
    OP_OR        = 0x28,  /* logical or */
    OP_STR_EQ    = 0x29,  /* string equality */
    OP_STR_NE    = 0x2A,  /* string inequality */

    /* 0x30-0x3F: String operations */
    OP_STRCAT    = 0x30,  /* concatenate two strings */
    OP_STRLEN    = 0x31,  /* push string length */
    OP_SUBSTR    = 0x32,  /* pop len, pop off, pop str, push substring */
    OP_STRCMP    = 0x33,
    OP_TOUPPER   = 0x34,
    OP_TOLOWER   = 0x35,
    OP_STRREP    = 0x36,  /* string replace */
    OP_STRFMT    = 0x37,  /* format string (like printf) */

    /* 0x40-0x4F: Control flow */
    OP_JMP       = 0x40,  /* unconditional [u16 target] */
    OP_JZ        = 0x41,  /* jump if zero/false [u16 target] */
    OP_JNZ       = 0x42,  /* jump if non-zero/true [u16 target] */
    OP_CALL      = 0x43,  /* call function [u16 func_idx] */
    OP_RET       = 0x44,  /* return from function */
    OP_HALTCALL  = 0x45,  /* halt-protected call (always goes through VM) */
    OP_HALT      = 0x46,  /* stop execution, exit code = top of stack */
    OP_ENTER     = 0x47,  /* function prologue [u8 nlocals] */
    OP_LEAVE     = 0x48,  /* function epilogue */

    /* 0x50-0x5F: I/O */
    OP_PRINT     = 0x50,  /* print string top of stack */
    OP_PRINTLN   = 0x51,  /* print string + newline */
    OP_PRINT_INT = 0x52,  // print integer top of stack
    OP_READ      = 0x53,  // read line from stdin
    OP_CAP_START = 0x54,  /* start capturing PRINT output to buffer */
    OP_CAP_END   = 0x55,  /* stop capturing, push buffer as string */
    OP_EXEC_CAP  = 0x56,  /* execute external command via popen, push captured output */

    /* 0x60-0x6F: Shell interop */
    OP_EXEC_CMD  = 0x60,  /* execute external command via system() */
    OP_EXEC_PIPE  = 0x61,  /* pipe two commands */
    OP_GETENV    = 0x62,  /* getenv(name) */
    OP_SETENV    = 0x63,  /* setenv(name, value) */
    OP_EXIT      = 0x64,  /* exit(status) */
    OP_BG        = 0x65,  /* background execution */
    OP_SUBSHELL  = 0x66,  /* subshell execution */

    /* 0x70-0x7F: Array operations */
    OP_ARR_NEW   = 0x70,  /* create new empty array */
    OP_ARR_PUSH  = 0x71,  /* push element to array */
    OP_ARR_GET   = 0x72,  /* get element by index */
    OP_ARR_LEN   = 0x73,  /* array length */
    OP_ARR_JOIN  = 0x74,  /* join array to string */
    OP_ARR_SLICE = 0x75,  /* slice array */
    OP_ARR_FREE  = 0x76,  /* free array */

    /* 0x80-0x8F: Type conversion and inspection */
    OP_TO_INT    = 0x80,  /* string to int (atoi) */
    OP_TO_STR    = 0x81,  /* int to string */
    OP_TYPEOF    = 0x82,  /* push type tag */

    /* 0xC0-0xCF: File I/O */
    OP_OPEN       = 0xC0,  /* open file */
    OP_CLOSE      = 0xC1,
    OP_READFILE   = 0xC2,  /* read entire file */
    OP_WRITEFILE  = 0xC3,  /* write to file */
    OP_APPENDFILE = 0xC4,

    /* 0xF0-0xFF: VM meta and JIT */
    OP_PROFILE    = 0xF0,  /* profiling marker [u16 block_id] */
    OP_JIT_COMPILE= 0xF1,  /* trigger JIT compilation for current block */
    OP_NATIVE_CALL= 0xF2,  /* call native function pointer */
    OP_TRACE_START= 0xF3,  /* start tracing for JIT */
    OP_TRACE_STOP = 0xF4,  /* stop tracing */
    OP_EVAL_ARITH  = 0xF5,  /* evaluate arithmetic expression string → int */
    OP_EXEC_PRINTF = 0xF6,  /* pop fmt, pop args, printf to stdout */
    OP_STR_TOUPPER = 0xF7,  /* pop str, push uppercased str */
    OP_STR_TOLOWER = 0xF8,  /* pop str, push lowercased str */
    OP_STR_SUBSTR  = 0xF9,  /* pop len, pop off, pop str, push substr */
    OP_STR_LEN     = 0xFA,  /* pop str, push length */
    OP_COALESCE    = 0xFB,  /* pop b, pop a: push a if non-empty, else push b */
    OP_STRIP_PREFIX= 0xFC,  /* pop pat, pop str: strip prefix matching pat */
    OP_STRIP_SUFFIX= 0xFD,  /* pop pat, pop str: strip suffix matching pat */
    OP_ALL_ARGS    = 0xFE,  /* push all positional params (space-joined) */

    OP_INVALID   = 0xFF
} VmOpcode;

/* ---- Value types ---- */
typedef enum {
    VAL_NULL = 0,
    VAL_INT  = 1,
    VAL_STR  = 2,
    VAL_ARR  = 3,
    VAL_FILE = 4,
} VmType;

/* Bytecode header (embedded in generated C) */
typedef struct {
    char magic[4];       /* "S2CV" */
    uint8_t version;
    uint8_t flags;
    uint16_t nconsts;    /* number of string constants */
    uint16_t nfuncs;      /* number of functions */
    uint32_t code_size;   /* code segment size in bytes */
} VmHeader;

/* Function table entry */
typedef struct {
    uint16_t name_idx;   /* index into const pool for function name */
    uint32_t entry;       /* code offset */
    uint8_t nlocals;
    uint8_t nargs;
} VmFuncEntry;

/* Opcode name table (for debugging) */
static const char *VM_OPCODE_NAMES[] __attribute__((unused)) = {
    [OP_NOP]        = "NOP",
    [OP_PUSH_STR]   = "PUSH_STR",
    [OP_PUSH_INT]   = "PUSH_INT",
    [OP_PUSH_VAR]   = "PUSH_VAR",
    [OP_POP]        = "POP",
    [OP_DUP]        = "DUP",
    [OP_SWAP]       = "SWAP",
    [OP_ADD]        = "ADD",
    [OP_SUB]        = "SUB",
    [OP_MUL]        = "MUL",
    [OP_DIV]        = "DIV",
    [OP_MOD]        = "MOD",
    [OP_INC]        = "INC",
    [OP_DEC]        = "DEC",
    [OP_EQ]         = "EQ",
    [OP_NE]         = "NE",
    [OP_LT]         = "LT",
    [OP_GT]         = "GT",
    [OP_LE]         = "LE",
    [OP_GE]         = "GE",
    [OP_NOT]        = "NOT",
    [OP_STRCAT]     = "STRCAT",
    [OP_STRLEN]     = "STRLEN",
    [OP_SUBSTR]     = "SUBSTR",
    [OP_JMP]        = "JMP",
    [OP_JZ]         = "JZ",
    [OP_JNZ]        = "JNZ",
    [OP_CALL]       = "CALL",
    [OP_RET]        = "RET",
    [OP_HALT]       = "HALT",
    [OP_PRINT]      = "PRINT",
    [OP_PRINTLN]    = "PRINTLN",
    [OP_EXEC_CMD]   = "EXEC_CMD",
    [OP_GETENV]     = "GETENV",
    [OP_SETENV]     = "SETENV",
    [OP_EXIT]       = "EXIT",
    [OP_ARR_NEW]    = "ARR_NEW",
    [OP_ARR_PUSH]   = "ARR_PUSH",
    [OP_ARR_GET]    = "ARR_GET",
    [OP_ARR_LEN]    = "ARR_LEN",
    [OP_ARR_JOIN]   = "ARR_JOIN",
    [OP_TO_INT]     = "TO_INT",
    [OP_TO_STR]     = "TO_STR",
    [OP_PROFILE]    = "PROFILE",
    [OP_INVALID]    = "INVALID",
};

#endif /* S2C_VM_ISA_H */
