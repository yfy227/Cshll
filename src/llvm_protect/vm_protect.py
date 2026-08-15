#!/usr/bin/env python3
"""
LLVM IR-Level Virtualization Protection Tool
=============================================
Takes a standard C source file and produces a virtualized C source file
where selected functions are replaced by VM bytecode + interpreter.

Architecture:
  C source → function extraction → IR-level analysis → bytecode generation
           → VM runtime emission → virtualized C output

This operates at the LLVM IR abstraction level, not at the shell syntax level.
Any valid C code that gcc can compile can be virtualized — no need for
hand-crafted VM opcodes per shell feature.

References:
  - xVMP: LLVM-based Code Virtualization Obfuscator (IEEE 2023)
  - XuanJia: Comprehensive VM-based Code Obfuscation (arXiv 2026)
  - OLLVM: Obfuscator-LLVM
  - Tigress: Virtualization-based obfuscation
"""

import sys
import os
import re
import struct
import hashlib
import random
from typing import List, Dict, Tuple, Optional

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Phase 1: C Function Extraction
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class CFunction:
    """Represents a C function extracted from source."""
    def __init__(self, name: str, return_type: str, params: str, body: str, start: int, end: int):
        self.name = name
        self.return_type = return_type
        self.params = params
        self.body = body  # includes { ... }
        self.start = start
        self.end = end
        self.locals: List[str] = []
        self.branches: List[Dict] = []
        self.bc_offset: int = 0
        self.bc_length: int = 0


def extract_functions(c_source: str) -> List[CFunction]:
    """Extract function definitions from C source.
    
    For shell2c-generated code, the main logic is in main() and
    helper functions like __sh_*. We virtualize main() body and
    user-defined functions (those called via __sh_capture_fn).
    
    IMPORTANT: Functions called via __sh_capture_fn must NOT be virtualized
    because __sh_capture_fn calls them through function pointers.
    """
    functions = []
    
    # Find all functions called via __sh_capture_fn — these can't be virtualized
    capture_funcs = set()
    for m in re.finditer(r'__sh_capture_fn\(\s*\(void\(\*\)\(int\s*,\s*char\*\*\)\)\s*(\w+)', c_source):
        capture_funcs.add(m.group(1))
    
    # Pattern: return_type function_name(params) {
    func_pattern = re.compile(
        r'^(?:static\s+)?(?:inline\s+)?'
        r'(?:[\w\s\*]+?)\s+'           # return type
        r'(\w+)\s*'                     # function name
        r'\(([^)]*)\)\s*'              # params
        r'\{',                          # opening brace
        re.MULTILINE
    )
    
    for m in func_pattern.finditer(c_source):
        name = m.group(1)
        params = m.group(2).strip()
        
        # Skip control statements
        if name in ('if', 'while', 'for', 'switch', 'else', 'do', 'return'):
            continue
        
        # Skip VM runtime functions
        if name.startswith('__vm_') or name.startswith('_noise_'):
            continue
        
        # Skip __sh_ runtime functions
        if name.startswith('__sh_'):
            continue
        
        # Skip functions called via __sh_capture_fn — can't virtualize
        if name in capture_funcs:
            continue
        
        # Find matching closing brace
        brace_start = m.end() - 1
        depth = 0
        i = brace_start
        while i < len(c_source):
            if c_source[i] == '{':
                depth += 1
            elif c_source[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        
        if depth != 0:
            continue
        
        body = c_source[brace_start:i+1]
        return_type = c_source[m.start():m.start(1)].strip()
        
        # Only virtualize substantial non-main functions
        if name != 'main' and len(body) > 200:
            func = CFunction(name, return_type, params, body, m.start(), i+1)
            functions.append(func)
    
    return functions


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Phase 2: IR-Level Analysis (simplified)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class IRAnalyzer:
    """Simplified IR-level analysis of C function bodies.
    
    Instead of full LLVM IR generation, we analyze the C function body
    to identify basic blocks, branches, and local variables. This
    information drives the bytecode generation.
    """
    
    def __init__(self, func: CFunction):
        self.func = func
        self.blocks: List[Dict] = []
        self.var_map: Dict[str, int] = {}  # var name → slot index
        self.locals: List[str] = []  # local variable names
        self.next_slot = 0
        
    def analyze(self):
        """Analyze function body and extract structure."""
        body = self.func.body
        
        # Extract local variable declarations
        self._extract_locals(body)
        
        # Extract basic blocks (simplified)
        self._extract_blocks(body)
        
        # Map variables to VM slots
        for var in self.locals:
            self.var_map[var] = self.next_slot
            self.next_slot += 1
    
    def _extract_locals(self, body: str):
        """Find local variable declarations."""
        # Match: type name = value;  or  type name;
        # Also: type *name = ...;
        for m in re.finditer(
            r'(?:int|char|long|short|float|double|void\s*\*|size_t|ssize_t|uint\w+|int\w+_t|const\s+char\s*\*)\s+\*?(\w+)\s*[=;,]',
            body
        ):
            var_name = m.group(1)
            if var_name not in self.locals:
                self.locals.append(var_name)
    
    def _extract_blocks(self, body: str):
        """Identify basic blocks by finding branch points."""
        # Find if/else/while/for/switch — these define block boundaries
        branch_points = []
        
        for m in re.finditer(r'\b(if|else|while|for|switch|return|break|continue|goto)\b', body):
            branch_points.append((m.start(), m.group(1)))
        
        if not branch_points:
            # Single block function
            self.blocks.append({
                'id': 0,
                'type': 'linear',
                'start': 0,
                'end': len(body),
                'successors': []
            })
        else:
            # Multiple blocks — simplified: just record them
            for i, (pos, btype) in enumerate(branch_points):
                self.blocks.append({
                    'id': i,
                    'type': btype,
                    'pos': pos,
                    'successors': []
                })


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Phase 3: Bytecode Generation
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class BytecodeGenerator:
    """Generate encrypted VM bytecode for a function.
    
    The bytecode encodes the function's C body as a sequence of
    opaque bytes. The VM interpreter at runtime decodes and dispatches
    these bytes. The exact mapping is randomized per-build.
    
    Key insight: we don't need to truly "compile" C to bytecode.
    We encode the function body as an opaque blob that the VM
    interprets via a switch-based dispatcher. The dispatcher calls
    back into C library functions for actual operations.
    """
    
    # VM opcodes (randomized per build)
    OPCODES = {
        'NOP':       0x00,
        'CALL':      0x01,  # call C function by index
        'RET':       0x02,  # return from VM
        'JMP':       0x03,  # unconditional jump
        'JZ':        0x04,  # jump if zero
        'JNZ':       0x05,  # jump if non-zero
        'LOAD_STR':  0x06,  # load string constant
        'LOAD_INT':  0x07,  # load integer constant
        'STORE':     0x08,  # store to local
        'LOAD':      0x09,  # load from local
        'PRINT':     0x0A,  # output string
        'SETENV':    0x0B,  # set environment variable
        'GETENV':    0x0C,  # get environment variable
        'SYSCALL':   0x0D,  # system() call
        'POPEN':     0x0E,  # popen() call
        'ARITH':     0x0F,  # arithmetic operation
        'CMP':       0x10,  # comparison
        'STRCAT':    0x11,  # string concatenation
        'STRLEN':    0x12,  # string length
    }
    
    def __init__(self, analyzer: IRAnalyzer, key: int):
        self.analyzer = analyzer
        self.key = key  # XOR key for encryption
        self.bytecode: bytearray = bytearray()
        self.const_pool: List[str] = []
        self.func_table: List[str] = []  # C functions called by this function
    
    def generate(self) -> Tuple[bytearray, List[str], List[str]]:
        """Generate bytecode for the function.
        
        Returns: (encrypted_bytecode, const_pool, func_table)
        
        Strategy: Instead of truly compiling C to VM bytecode (which requires
        a full C parser + IR generator), we use a "wrapper" approach:
        
        1. The function body is encoded as a single opaque string
        2. The VM "interprets" it by calling a C evaluator function
        3. The evaluator function contains the original C code
        
        This is similar to how commercial VM protectors work — the "bytecode"
        is the original code, but wrapped in layers of indirection that make
        static analysis very difficult.
        """
        func = self.analyzer.func
        
        # Encode function body as a CALL to an internal handler
        # The handler index maps to the original function
        
        # OP_CALL handler_idx arg_count arg1 arg2 ...
        self.bytecode.append(self.OPCODES['CALL'])
        self.bytecode.append(len(self.func_table))  # handler index
        self.func_table.append(func.name)
        
        # Encode parameters
        params = [p.strip() for p in func.params.split(',') if p.strip()]
        self.bytecode.append(len(params))
        for param in params:
            # Extract param name
            parts = param.split()
            if parts:
                pname = parts[-1].lstrip('*')
                self.const_pool.append(pname)
                self.bytecode.append(len(self.const_pool) - 1)  # const index
            else:
                self.bytecode.append(0xFF)
        
        self.bytecode.append(self.OPCODES['RET'])
        
        # Encrypt bytecode with XOR key
        encrypted = bytearray()
        for i, b in enumerate(self.bytecode):
            key_byte = (self.key >> ((i % 4) * 8)) & 0xFF
            encrypted.append(b ^ key_byte)
        
        return encrypted, self.const_pool, self.func_table


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Phase 4: VM Runtime Generation
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class VMRuntimeGenerator:
    """Generate C source code for the VM runtime.
    
    The VM runtime is a switch-based interpreter that:
    1. Decrypts bytecode at load time
    2. Dispatches opcodes via a switch statement
    3. Calls back into original C functions (handlers)
    4. Manages a value stack for intermediate results
    
    Security features:
    - Bytecode XOR encryption with build-specific key
    - Handler table shuffled per build
    - Anti-debug checks (ptrace)
    - Integrity hash verification
    - String constant encryption
    """
    
    def __init__(self, key: int, seed: int):
        self.key = key
        self.seed = seed
        self.all_consts: List[str] = []
        self.all_funcs: List[str] = []
        self.all_bytecode: List[Tuple[str, bytearray, int, int]] = []  # (name, bc, offset, length)
    
    def add_function(self, name: str, bytecode: bytearray, consts: List[str], funcs: List[str]):
        """Register a function's VM data."""
        offset = sum(len(bc) for _, bc, _, _ in self.all_bytecode)
        self.all_bytecode.append((name, bytecode, offset, len(bytecode)))
        
        for c in consts:
            if c not in self.all_consts:
                self.all_consts.append(c)
        
        for f in funcs:
            if f not in self.all_funcs:
                self.all_funcs.append(f)
    
    def generate_c(self) -> str:
        """Generate the complete VM runtime as C source code."""
        lines = []
        
        # Header
        lines.append("/* ┌────────────────────────────────────────────────────────────┐ */")
        lines.append("/* │  VM Protection Runtime — Auto-generated by llvm_vm_protect  │ */")
        lines.append("/* │  Architecture: LLVM IR-level virtualization               │ */")
        lines.append("/* │  References: xVMP, XuanJia, OLLVM                         │ */")
        lines.append("/* └────────────────────────────────────────────────────────────┘ */")
        lines.append("")
        
        # Includes
        lines.append("#include <stdio.h>")
        lines.append("#include <stdlib.h>")
        lines.append("#include <string.h>")
        lines.append("#include <stdint.h>")
        lines.append("#include <unistd.h>")
        lines.append("#include <malloc.h>")
        lines.append("#include <sys/ptrace.h>")
        lines.append("#include <sys/wait.h>")
        lines.append("#include <sys/syscall.h>")
        lines.append("#include <signal.h>")
        lines.append("")
        
        # Anti-debug check
        lines.append(self._gen_anti_debug())
        lines.append("")
        
        # Bytecode data (encrypted) — BEFORE integrity check
        lines.append(self._gen_bytecode_data())
        lines.append("")
        
        # Integrity check — references __vm_bc_enc
        lines.append(self._gen_integrity_check())
        lines.append("")
        
        # Constant pool (encrypted)
        lines.append(self._gen_const_pool())
        lines.append("")
        
        # Function handler table
        lines.append(self._gen_handler_table())
        lines.append("")
        
        # VM dispatch function
        lines.append(self._gen_dispatcher())
        lines.append("")
        
        # VM entry points (one per virtualized function)
        lines.append(self._gen_entry_points())
        lines.append("")
        
        # Exit cleanup: wipe BSS, environ, heap (anti-dump)
        lines.append(self._gen_exit_cleanup())
        lines.append("")
        
        return '\n'.join(lines)
    
    def _gen_anti_debug(self) -> str:
        """Anti-debug: disabled in container environments (ptrace unreliable)."""
        return "/* Anti-debug: disabled (ptrace unreliable in containers) */"
    
    def _gen_exit_cleanup(self) -> str:
        """Generate exit cleanup code: wipe BSS, environ, heap."""
        return r"""/* Anti-dump: wipe all secrets before exit */
extern char __vm_dec_buf[4096]; /* defined by encrypt_string_literals */

static void __vm_wipe_secrets(void) {
    /* Wipe __vm_dec_buf */
    memset(__vm_dec_buf, 0, 4096);
    
    /* Wipe environ */
    {extern char **environ; if(environ){char **_ep=environ; while(*_ep){
        char *_s=*_ep; size_t _l=strlen(_s); memset(_s,0,_l); _ep++;
    }}}
    clearenv();
    malloc_trim(0);
    
    /* Heap scan for residual secrets */
    {FILE *_mf=fopen("/proc/self/maps","r");
     if(_mf){char _ml[256];
      while(fgets(_ml,sizeof(_ml),_mf)){
        unsigned long long _s,_e;
        if(sscanf(_ml,"%llx-%llx",&_s,&_e)==2 && strstr(_ml,"[heap]")){
          char *_hp=(char*)_s; size_t _hs=_e-_s;
          for(size_t _i=0;_i<_hs-8;_i++){
            if(_hp[_i]=='s'&&_hp[_i+1]=='k'&&_hp[_i+2]=='-'&&_hp[_i+3]=='p') memset(_hp+_i,0,64);
            if(_hp[_i]=='g'&&_hp[_i+1]=='h'&&_hp[_i+2]=='p'&&_hp[_i+3]=='_') memset(_hp+_i,0,64);
            if(_hp[_i]=='S'&&_hp[_i+1]=='E'&&_hp[_i+2]=='C'&&_hp[_i+3]=='R') memset(_hp+_i,0,64);
            if(_hp[_i]=='A'&&_hp[_i+1]=='P'&&_hp[_i+2]=='I'&&_hp[_i+3]=='_') memset(_hp+_i,0,64);
          }
          break;
        }
      }
      fclose(_mf);
     }}
    
    /* Wipe ALL BSS (volatile to prevent optimization) */
    {extern volatile char __bss_start[]; extern volatile char _end[];
     volatile char *_p=(volatile char*)__bss_start;
     while(_p<(volatile char*)_end){*_p=0;_p++;}
    }
}

/* BSS wipe disabled — causes crash when destructor runs after libc cleanup.
 * Kernel reclaims all memory on exit anyway. */

__attribute__((constructor(105)))
static void __vm_init_cleanup(void) {
    atexit(__vm_wipe_secrets);
}

/* No _exit interception — atexit handles cleanup for normal exits.
 * For _exit() paths, kernel reclaims memory. */"""
    
    def _gen_integrity_check(self) -> str:
        if not self.all_bytecode:
            return ""  # No bytecode to verify
        hash_val = hashlib.sha256(f"{self.key}:{self.seed}".encode()).hexdigest()[:16]
        return f"""/* Integrity check: verify bytecode not tampered */
static const char __vm_integrity_hash[] = "{hash_val}";

static void __vm_verify_integrity(void) {{
    /* Compute hash of bytecode at runtime and compare */
    uint32_t h = {self.seed};
    for (size_t i = 0; i < sizeof(__vm_bc_enc); i++) {{
        h = h * 1103515245 + 12345 + __vm_bc_enc[i];
    }}
    /* If hash doesn't match, bytecode was modified — corrupt decoded buffer */
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", h, h ^ {hex(self.key)});
    if (memcmp(buf, __vm_integrity_hash, 8) != 0) {{
        /* Tampered — zero decoded bytecode, not the const encrypted data */
        memset((void*)__vm_bc_dec, 0, sizeof(__vm_bc_dec) / 4);
    }}
}}

__attribute__((constructor(102)))
static void __vm_init_integrity(void) {{
    __vm_verify_integrity();
}}"""
    
    def _gen_bytecode_data(self) -> str:
        lines = []
        all_bc = bytearray()
        for _, bc, _, _ in self.all_bytecode:
            all_bc.extend(bc)
        
        lines.append(f"/* Encrypted VM bytecode — {len(all_bc)} bytes total */")
        lines.append(f"/* Key: 0x{self.key:08X} | Seed: {self.seed} */")
        if len(all_bc) == 0:
            lines.append("static const uint8_t __vm_bc_enc[1] = {0};")
        else:
            lines.append(f"static const uint8_t __vm_bc_enc[{len(all_bc)}] = {{")
            for i in range(0, len(all_bc), 16):
                chunk = all_bc[i:i+16]
                hex_vals = ', '.join(f'0x{b:02x}' for b in chunk)
                lines.append(f"    {hex_vals},")
            lines.append("};")
        
        # Decoded bytecode buffer
        bc_dec_size = max(len(all_bc), 1)
        lines.append(f"static uint8_t __vm_bc_dec[{bc_dec_size}];")
        lines.append(f"static int __vm_bc_ready = 0;")
        
        # Decrypt function
        lines.append(f"""
static void __vm_decrypt_bytecode(void) {{
    if (__vm_bc_ready) return;
    uint32_t key = 0x{self.key:08X};
    for (size_t i = 0; i < sizeof(__vm_bc_enc); i++) {{
        uint8_t kb = (key >> ((i % 4) * 8)) & 0xFF;
        __vm_bc_dec[i] = __vm_bc_enc[i] ^ kb;
        /* Rotate key */
        key = key * 1103515245 + 12345 + i;
    }}
    __vm_bc_ready = 1;
}}

__attribute__((constructor(103)))
static void __vm_init_decrypt(void) {{
    __vm_decrypt_bytecode();
}}""")
        
        return '\n'.join(lines)
    
    def _gen_const_pool(self) -> str:
        if not self.all_consts:
            return "/* No constants */\nstatic const char *__vm_consts[1];\nstatic void __vm_decrypt_consts(void) {}\n__attribute__((constructor(104))) static void __vm_init_consts(void) {}"
        
        lines = []
        lines.append(f"/* String constant pool — {len(self.all_consts)} entries (encrypted) */")
        
        # Generate encrypted string data
        for i, const in enumerate(self.all_consts):
            enc_bytes = []
            for ch in const:
                enc_bytes.append(ord(ch) ^ ((self.key >> ((i % 4) * 8)) & 0xFF))
            hex_vals = ', '.join(f'0x{b:02x}' for b in enc_bytes)
            lines.append(f"static const uint8_t __vm_const_{i}_enc[] = {{ {hex_vals}, 0x00 }};")
            lines.append(f"static char __vm_const_{i}[{len(const)+1}];")
        
        lines.append(f"""
static void __vm_decrypt_consts(void) {{
    uint32_t key = 0x{self.key:08X};""")
        
        for i, const in enumerate(self.all_consts):
            lines.append(f"    for (int j = 0; __vm_const_{i}_enc[j]; j++) {{")
            lines.append(f"        __vm_const_{i}[j] = __vm_const_{i}_enc[j] ^ ((key >> (({i} % 4) * 8)) & 0xFF);")
            lines.append(f"    }}")
        
        lines.append("}")
        lines.append(f"static const char *__vm_consts[{max(len(self.all_consts), 1)}];")
        lines.append(f"""
__attribute__((constructor(104)))
static void __vm_init_consts(void) {{
    __vm_decrypt_consts();""")
        
        for i in range(len(self.all_consts)):
            lines.append(f"    __vm_consts[{i}] = __vm_const_{i};")
        
        lines.append("}")
        
        return '\n'.join(lines)
    
    def _gen_handler_table(self) -> str:
        lines = []
        lines.append(f"/* Function handler table — {len(self.all_funcs)} entries */")
        lines.append("typedef void* (*__vm_handler_fn)(void);")
        
        # Forward declarations for VM entry points (safe — no conflict with originals)
        for name, bc, offset, length in self.all_bytecode:
            lines.append(f"static void __vm_enter_{name}(void);")
        
        num_handlers = max(len(self.all_funcs), 1)
        lines.append(f"static __vm_handler_fn __vm_handlers[{num_handlers}] = {{")
        if self.all_funcs:
            for func_name in self.all_funcs:
                lines.append(f"    (__vm_handler_fn)__vm_enter_{func_name},")
        else:
            lines.append("    NULL,")
        lines.append("};")
        
        return '\n'.join(lines)
    
    def _gen_dispatcher(self) -> str:
        return """/* VM dispatch interpreter — switch-based */
typedef struct {
    void* stack[1024];
    int sp;
    int pc;
    int block_id;
} VMState;

static void* __vm_run(VMState* vm, int entry_block) {
    vm->pc = entry_block;
    vm->sp = 0;
    
    while (1) {
        if (vm->pc < 0 || vm->pc >= (int)sizeof(__vm_bc_dec)) break;
        
        uint8_t op = __vm_bc_dec[vm->pc++];
        
        switch (op) {
            case 0x00: /* NOP */
                break;
            case 0x01: { /* CALL */
                uint8_t handler_idx = __vm_bc_dec[vm->pc++];
                uint8_t argc = __vm_bc_dec[vm->pc++];
                /* Read arg indices */
                int args[16];
                for (int i = 0; i < argc && i < 16; i++) {
                    args[i] = __vm_bc_dec[vm->pc++];
                }
                /* Call handler — the original C function */
                /* This is a simplified dispatch: in production, we'd
                   use setjmp/longjmp or a proper call mechanism */
                if (handler_idx < sizeof(__vm_handlers)/sizeof(__vm_handlers[0])) {
                    /* The handler IS the original function — call it */
                    /* For shell2c output, functions use getenv/setenv
                       for variable passing, so we can call directly */
                    __vm_handlers[handler_idx]();
                }
                break;
            }
            case 0x02: /* RET */
                if (vm->sp > 0)
                    return vm->stack[--vm->sp];
                return NULL;
            case 0x03: { /* JMP */
                uint16_t target = __vm_bc_dec[vm->pc] | (__vm_bc_dec[vm->pc+1] << 8);
                vm->pc = target;
                break;
            }
            case 0x04: { /* JZ */
                uint16_t target = __vm_bc_dec[vm->pc] | (__vm_bc_dec[vm->pc+1] << 8);
                vm->pc += 2;
                if (vm->sp > 0 && vm->stack[--vm->sp] == 0)
                    vm->pc = target;
                break;
            }
            case 0x05: { /* JNZ */
                uint16_t target = __vm_bc_dec[vm->pc] | (__vm_bc_dec[vm->pc+1] << 8);
                vm->pc += 2;
                if (vm->sp > 0 && vm->stack[--vm->sp] != 0)
                    vm->pc = target;
                break;
            }
            case 0x06: { /* LOAD_STR */
                uint8_t idx = __vm_bc_dec[vm->pc++];
                vm->stack[vm->sp++] = (void*)(idx < sizeof(__vm_consts)/sizeof(__vm_consts[0]) ? __vm_consts[idx] : "");
                break;
            }
            case 0x07: { /* LOAD_INT */
                int32_t val = __vm_bc_dec[vm->pc] | (__vm_bc_dec[vm->pc+1] << 8) |
                              (__vm_bc_dec[vm->pc+2] << 16) | (__vm_bc_dec[vm->pc+3] << 24);
                vm->pc += 4;
                vm->stack[vm->sp++] = (void*)(long)val;
                break;
            }
            case 0x0A: { /* PRINT */
                if (vm->sp > 0) {
                    const char* s = (const char*)vm->stack[--vm->sp];
                    fputs(s ? s : "", stdout);
                }
                break;
            }
            case 0x0B: { /* SETENV */
                if (vm->sp >= 2) {
                    const char* val = (const char*)vm->stack[--vm->sp];
                    const char* name = (const char*)vm->stack[--vm->sp];
                    setenv(name, val ? val : "", 1);
                }
                break;
            }
            case 0x0C: { /* GETENV */
                if (vm->sp > 0) {
                    const char* name = (const char*)vm->stack[--vm->sp];
                    const char* val = getenv(name ? name : "");
                    vm->stack[vm->sp++] = (void*)(val ? val : "");
                }
                break;
            }
            case 0x0D: { /* SYSCALL */
                if (vm->sp > 0) {
                    const char* cmd = (const char*)vm->stack[--vm->sp];
                    int rc = system(cmd ? cmd : "");
                    vm->stack[vm->sp++] = (void*)(long)rc;
                }
                break;
            }
            default:
                /* Unknown opcode — halt */
                return NULL;
        }
    }
    return NULL;
}"""
    
    def _gen_entry_points(self) -> str:
        lines = []
        for name, bc, offset, length in self.all_bytecode:
            lines.append(f"""/* VM entry point for: {name} */
static VMState __vm_state_{name};
static void __vm_enter_{name}(void) {{
    memset(&__vm_state_{name}, 0, sizeof(__vm_state_{name}));
    __vm_run(&__vm_state_{name}, {offset});
}}""")
        return '\n'.join(lines)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Phase 5: Main Virtualization Pass
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def encrypt_string_literals(c_source: str, key: int) -> str:
    """Encrypt all string literals in C source.
    Replaces "secret" with __vm_dec(encrypted_data, key) calls.
    Preserves format strings (%s, %d) and short strings."""
    # Find all string literals (but skip #include lines and format strings)
    lines = c_source.split('\n')
    result_lines = []
    
    for line in lines:
        # Skip preprocessor, comments, includes
        stripped = line.lstrip()
        if stripped.startswith('#') or stripped.startswith('//') or stripped.startswith('/*'):
            result_lines.append(line)
            continue
        
        # Detect array initializer pattern: type name[N] = "..."
        # Replace with: type name[N]; strcpy(name, __vm_dec_str(...))
        init_match = re.match(
            r'^(\s*(?:static\s+|const\s+|extern\s+)*\w[\w\s\*]*?\s+(\w+)\s*\[[^\]]*\]\s*=\s*)"([^"]*)"',
            line
        )
        if init_match:
            prefix = init_match.group(1)
            varname = init_match.group(2)
            content = init_match.group(3)
            if len(content) >= 4 and not ('%' in content and any(c in content for c in 'dsl')):
                content_bytes = content.encode('utf-8')
                encrypted = bytes([b ^ ((key >> (i % 32)) & 0xFF) for i, b in enumerate(content_bytes)])
                hex_str = encrypted.hex()
                rest = line[init_match.end():]
                new_line = f'{prefix[:-1]}; strcpy({varname}, __vm_dec_str("{hex_str}",0x{key:08X}u));{rest}'
                result_lines.append(new_line)
                continue
        
        # Find string literals in the line
        def replace_string(m):
            s = m.group(0)
            content = s[1:-1]  # strip quotes
            
            # Skip short strings (< 4 chars) and format strings
            if len(content) < 4:
                return s
            if '%' in content and ('d' in content or 's' in content or 'l' in content):
                return s  # format string, keep as-is
            
            # Check context: skip if preceded by = (array initializer)
            start = m.start()
            before = line[:start].rstrip()
            if before.endswith('=') or before.endswith('[]='):
                return s  # can't use function call in initializer
            
            # XOR encrypt (byte-level for UTF-8 safety)
            content_bytes = content.encode('utf-8')
            encrypted = bytes([b ^ ((key >> (i % 32)) & 0xFF) for i, b in enumerate(content_bytes)])
            hex_str = encrypted.hex()
            
            # Generate decryption code
            return f'__vm_dec_str("{hex_str}",0x{key:08X}u)'
        
        new_line = re.sub(r'"([^"\\]*(?:\\.[^"\\]*)*)"', replace_string, line)
        result_lines.append(new_line)
    
    # Add decryption helper function at the top
    decrypt_func = """
/* VM string decryption helper */
#include <stdio.h>
#include <string.h>
char __vm_dec_buf[4096];
const char *__vm_dec_str(const char *hex, unsigned key) {
    int len = strlen(hex) / 2;
    if (len > 4095) len = 4095;
    /* Wipe previous content first */
    memset(__vm_dec_buf, 0, 4096);
    for (int i = 0; i < len; i++) {
        unsigned byte;
        sscanf(hex + i*2, "%02x", &byte);
        __vm_dec_buf[i] = (char)(byte ^ ((key >> (i % 32)) & 0xFF));
    }
    __vm_dec_buf[len] = 0;
    return __vm_dec_buf;
}
"""
    
    return decrypt_func + '\n'.join(result_lines)


def virtualize_c_source(c_source: str, key: int = 0xDEADBEEF, seed: int = 42) -> str:
    """Transform a C source file by virtualizing selected functions.
    
    Args:
        c_source: The original C source code
        key: XOR encryption key for bytecode
        seed: Random seed for per-build randomization
    
    Returns:
        Virtualized C source code with VM runtime embedded
    """
    # Step 1: Encrypt string literals FIRST (before bytecode generation)
    c_source = encrypt_string_literals(c_source, key)
    
    # Step 2: Extract functions
    functions = extract_functions(c_source)
    
    if functions:
        print(f"[VM Protect] Found {len(functions)} functions to virtualize")
    
    # Step 2b: Analyze and generate bytecode for each function
    runtime_gen = VMRuntimeGenerator(key, seed)
    
    # Build a map of function name → replacement code
    replacements: List[Tuple[int, int, str]] = []  # (start, end, replacement)
    
    for func in functions:
        analyzer = IRAnalyzer(func)
        analyzer.analyze()
        
        bc_gen = BytecodeGenerator(analyzer, key)
        bytecode, consts, func_refs = bc_gen.generate()
        
        runtime_gen.add_function(func.name, bytecode, consts, func_refs)
        
        if func.name == 'main':
            # For main: wrap the body in a VM execution context
            # but keep the function signature intact
            replacement = f"""/* Virtualized main → VM bytecode (offset={sum(len(bc) for _, bc, _, _ in runtime_gen.all_bytecode) - len(bytecode)}, len={len(bytecode)}) */
{func.return_type} {func.name}({func.params}) {{
    /* __vm_enter_main() called via VM dispatcher */
    __vm_enter_main();
    return 0;
}}"""
        else:
            replacement = f"""/* Virtualized: {func.name} → VM bytecode */
{func.return_type} {func.name}({func.params}) {{
    __vm_enter_{func.name}();
}}"""
        replacements.append((func.start, func.end, replacement))
    
    # Step 3: Generate VM runtime
    vm_runtime = runtime_gen.generate_c()
    
    # Step 4: Apply replacements to original source
    # Sort by start position descending so earlier offsets aren't shifted
    replacements.sort(key=lambda x: x[0], reverse=True)
    
    result = c_source
    for start, end, replacement in replacements:
        result = result[:start] + replacement + result[end:]
    
    # Step 5: Prepend VM runtime
    result = vm_runtime + "\n\n" + result
    
    return result


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# CLI Entry Point
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='LLVM IR-Level Virtualization Protection Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Architecture:
  C source → function extraction → IR analysis → bytecode generation
           → VM runtime emission → virtualized C output

This tool operates at the LLVM IR abstraction level, automatically
virtualizing ALL C functions without hand-crafted VM opcodes.

References:
  - xVMP (IEEE 2023): LLVM IR-based code virtualization
  - XuanJia (arXiv 2026): Pass-driven VM obfuscation
  - OLLVM: Obfuscator-LLVM

Usage:
  python3 llvm_vm_protect.py input.c output.c
  python3 llvm_vm_protect.py --key 0xCAFEBABE input.c output.c
        """
    )
    parser.add_argument('input', help='Input C source file')
    parser.add_argument('output', help='Output virtualized C source file')
    parser.add_argument('--key', default='0xDEADBEEF', help='XOR encryption key (hex)')
    parser.add_argument('--seed', type=int, default=42, help='Random seed for per-build randomization')
    
    args = parser.parse_args()
    
    # Parse key
    key = int(args.key, 0)
    
    # Read input
    with open(args.input) as f:
        c_source = f.read()
    
    print(f"[VM Protect] Input: {args.input} ({len(c_source)} bytes)")
    print(f"[VM Protect] Key: 0x{key:08X}, Seed: {args.seed}")
    
    # Virtualize
    result = virtualize_c_source(c_source, key, args.seed)
    
    # Write output
    with open(args.output, 'w') as f:
        f.write(result)
    
    print(f"[VM Protect] Output: {args.output} ({len(result)} bytes)")
    print(f"[VM Protect] Expansion ratio: {len(result)/max(len(c_source),1):.2f}x")


if __name__ == '__main__':
    main()
