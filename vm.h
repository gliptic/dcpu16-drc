#ifndef VM_H
#define VM_H

#include "asm.h"

#define INVALID_LOC ((u32)(-1))
#define MAX_BLOCKS (256)

typedef struct block {
	u8 *code_beg, *code_end;
	u16 data_beg, data_end;
} block;

typedef struct context {
	u16 data[1<<16];
	u8 blockmap[1<<16];
	u16 codeloc[1<<16];

	block blocks[MAX_BLOCKS];
	int first_used_block, num_used_blocks;
	
	u8* common_code;
	u8* common_code_end;
	
	u8* code_cache;
	u8* code_cache_cur;
	u8* code_cache_end;

	u16 regs[8];
	u16 pc, sp, o;
	
	u8 *proc_indirect_jump, *proc_indirect_skipjump,
	   *proc_indirect_jump_c, *proc_invalidate;
} context;

void context_init(context* C);

#define CODE_CACHE_SIZE (65536)
#define CODE_CACHE_REDLINE (1024)

#define OP_EX  (0)
#define OP_SET (1)
#define OP_ADD (2)
#define OP_SUB (3)
#define OP_MUL (4)
#define OP_DIV (5)
#define OP_MOD (6)
#define OP_SHL (7)
#define OP_SHR (8)
#define OP_AND (9)
#define OP_BOR (10)
#define OP_XOR (11)
#define OP_IFE (12)
#define OP_IFN (13)
#define OP_IFG (14)
#define OP_IFB (15)

#define VAL_POP    (0x18)
#define VAL_PEEK   (0x19)
#define VAL_PUSH   (0x1a)
#define VAL_SP     (0x1b)
#define VAL_PC     (0x1c)
#define VAL_O      (0x1d)
#define VAL_LITMEM (0x1e)
#define VAL_LIT    (0x1f)

// Register mapping

// Scratch: RAX, RCX, EDX
#define R_PC       (RAX) // Only set before a call/jump
#define R_CONTEXT  (RDI)
#define R_SP       (RSI)
#define R_O        (RBX)
#define R_A        (R8)
#define R_B        (R9)
#define R_C        (R10)
#define R_X        (R11)
#define R_Y        (R12)
#define R_Z        (R13)
#define R_I        (R14)
#define R_J        (R15)
#define R_CC       (RBP) // Cycle count

#endif // VM_H
