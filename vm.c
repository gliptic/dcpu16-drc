#include <stdlib.h>
#include <memory.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <udis86.h>

#include "asm.h"
#include "vm.h"
#include "parser.h"

void dump_asm(u8* beg, u8* end);

void context_init(context* C) {
	size_t newlen = CODE_CACHE_SIZE + 4096;
	u8* newcode = mcode_alloc(newlen, 0);
	u8* L;
	
	memset(C, 0, sizeof(*C));

	C->sp = 0xffff;
	
	C->code_cache = newcode;
	C->code_cache_cur = newcode + 1; // Byte 1 is reserved
	C->code_cache_end = newcode + CODE_CACHE_SIZE - CODE_CACHE_REDLINE;
	
	C->common_code = newcode + CODE_CACHE_SIZE;
	
	assert(offsetof(context, data) == 0);
	
	{
		u8* LOC = C->common_code;
		C->proc_indirect_skipjump = LOC;
		
		C->proc_indirect_jump = LOC;
		c_movzx_w(m_sib_d32(EAX, R_CONTEXT, R_PC, SS_2, offsetof(context, codeloc)));
		c_test(r(EAX, EAX));
		c_jz(0);
		L = LOC;
			c_add(mq_d32(RAX, R_CONTEXT, offsetof(context, code_cache)));
			c_jmp_ind(r(RAX));
		c_label8(L);
		
		C->proc_indirect_jump_c = LOC;
		c_push_q(RBP);
		c_push_q(RBX);
		c_push_q(RSI);
		c_push_q(RDI);
		c_push_q(R12);
		c_push_q(R13);
		c_push_q(R14);
		c_push_q(R15);
		
		c_mov(mq_d32(R_CONTEXT, EBP, 8));
		
		// Load data from context
		c_movzx_w(m_si_d32(R_SP, R_CONTEXT, SS_1, offsetof(context, pc)));
		c_movzx_w(m_si_d32(R_O, R_CONTEXT, SS_1, offsetof(context, o)));
		
		c_movzx_w(m_d32(R_A, R_CONTEXT, offsetof(context, regs) + 0));
		c_movzx_w(m_d32(R_B, R_CONTEXT, offsetof(context, regs) + 2));
		c_movzx_w(m_d32(R_C, R_CONTEXT, offsetof(context, regs) + 4));
		c_movzx_w(m_d32(R_X, R_CONTEXT, offsetof(context, regs) + 6));
		c_movzx_w(m_d32(R_Y, R_CONTEXT, offsetof(context, regs) + 8));
		c_movzx_w(m_d32(R_Z, R_CONTEXT, offsetof(context, regs) + 10));
		c_movzx_w(m_d32(R_I, R_CONTEXT, offsetof(context, regs) + 12));
		c_movzx_w(m_d32(R_J, R_CONTEXT, offsetof(context, regs) + 14));
		
		// Make sure top halves are cleared
		c_xor(r(EAX, EAX));
		c_xor(r(ECX, ECX));
		c_xor(r(EDX, EDX));
		
		c_movzx_w(m_si_d32(R_PC, R_CONTEXT, SS_1, offsetof(context, pc)));
		
		// Testing
#if 0
		c_mov16(r(AX, CX));
		c_add_k8(r(EAX), 5);
		c_or_k8(r(EAX), 5);
		c_add(r(EAX, ECX));
		c_add16(r(AX, CX));
		c_add8(r(R8B, CL));
		c_add8(r(CL, R8B));
		c_cmp8(m_sib(CL, R_CONTEXT, RAX, SS_1));
#endif
		
		c_callrel(C->proc_indirect_jump);
		c_pop_q(R15);
		c_pop_q(R14);
		c_pop_q(R13);
		c_pop_q(R12);
		c_pop_q(RDI);
		c_pop_q(RSI);
		c_pop_q(RBX);
		c_pop_q(RBP);
		c_retn();
		
		C->common_code_end = LOC;
		
		// mov al, [rdi+rax+0x20000]
		//   c_mov8(m_sib_d32(AL, R_CONTEXT, RM_RAX, SS_1, offsetof(context, blockmap)));
	}
}

#define CTX(f, ...) (LOC = f(C, LOC, __VA_ARGS__))

#define c_indirect_jump(loc) { \
	c_mov_k(EAX, loc); \
	c_jmp_far(C->proc_indirect_jump); }
	
#define c_indirect_skipjump(loc) { \
	c_mov_k(EAX, loc); \
	c_jmp_far(C->proc_indirect_skipjump); }

void trace(context* C);

#define AV(x) (((x)>>4)&0x3f)
#define BV(x) (((x)>>10)&0x3f)
#define OP(x) ((x)&0xf)

#define EXIT_INTERP() do { \
	C->pc = pc; \
} while(0)

/* JIT life-cycle

Trace a block

1. Identify a set of instructions to compile (I-set).
2. Compile them into a contiguous block of code.
3. Store the entry-points to all instructions in the block.

A block can have direct-jumps within itself, but not to other blocks.
To go to another block, it must go via the codeloc table.

Block invalidation:
	A write has been done to an instruction that is part of a block.
1. Invalidate the block by setting codeloc[x] = 0 and block[x] = 0 for all x part of the block.
2. If the block being invalidated is the currently executed one,
	exit the block.
   Otherwise, continue as usual.

Register assignment:
DATA = esi
CODELOC = edi
PC = ebx
CONTEXT = ebp

update_c_state():
mov [CONTEXT->pc], PC

interp:


invalidate_instr(offset = eax):
mov [CODELOC + offset*4], 0
// TODO: Free machine-code range
update_c_state()
jmp interp

Write (dest = eax, value = ecx):
mov [DATA + dest*2], value
cmp [CODELOC + dest*4], 0
je ok
mov PC, <implicit PC>
call invalidate_instr(dest)
ok:
*/

void invalidate_block(context* C, int bindex) {
	block* bl = C->blocks + bindex;
	u16 iw;
	for(iw = bl->data_beg; iw != bl->data_end; ++iw) {
		C->blockmap[iw] = 0;
		C->codeloc[iw] = 0;
	}
	
	bl->data_beg = 0;
	bl->data_end = 0;
}

void invalidate_code_range(context* C, u8* beg, u8* end) {
	// Invalidate all blocks that have machine code in the given range
	int i;
	for(i = 1; i < 256; ++i) {
		if(C->blocks[i].data_beg != C->blocks[i].data_end
		&& !(C->blocks[i].code_beg >= end || C->blocks[i].code_end <= beg)) {
			invalidate_block(C, i);
		}
	}
}

void invalidate_data_range(context* C, u16 from, u16 to) {
	u16 iw;
	for(iw = from; iw != to; ++iw) {
		if(C->blockmap[iw] != 0) {
			invalidate_block(C, C->blockmap[iw]);
		}
	}
}

typedef struct label {
	u32* patchloc;

} label;

typedef struct skipslot {
	u32* codeloc;
	u16 pc; // The instruction to skip
} skipslot;



#define OP_IS_IF(x) ((x) >= OP_IFE)
#define INSTR_IS_IF(x) (((x)&0xc) == 0xc)
#define OP_SETS_O(x) ((x) >= OP_ADD && (x) <= OP_SHR && (x) != OP_MOD)
#define VAL_USES_SP(x) ((x) >= 0x18 && (x) <= 0x1b)

u16 val_size(u16 val) {
	return (val >= 0x10 && val <= 0x17) || (val >= 0x1e && val <= 0x1f);
}

u16 instr_size(u16 instr) {
	return 1 + val_size(AV(instr)) + val_size(BV(instr));
}

/*
0x10-0x17
0x1e, 0x1f

10???
1111?
abcde

a~b + abcd

       1????
except 11000..11101

(x > 16 && x < 24) || x > 29
*/

int requires_o(u16 instr) {
	return AV(instr) == VAL_O || BV(instr) == VAL_O || !OP_SETS_O(OP(instr));
}

// Machine values
typedef u32 mvalue;

char const* reg_names[16] = {
	"AX",
	"CX",
	"DX",
	"BX",
	"SP",
	"BP",
	"SI",
	"DI",
	"R8W",
	"R9W",
	"R10W",
	"R11W",
	"R12W",
	"R13W",
	"R14W",
	"R15W"
};

#define MV_IMM    (0) // o
#define MV_REG    (1) // Rr
#define MV_IMMIND (2) // [o]
#define MV_REGIND (3) // [Rr + o]

#define MV_IS_MEM(mv) ((mv)>=(MV_IMMIND<<24))
#define MV_IS_REG(mv) ((mv)&(1<<24))

#define MV(mode, reg, offset) (((mode)<<24)+((reg)<<16)+(offset))

#define MV_M(v) ((v)>>24)
#define MV_R(v) (((v)>>16)&0xff)
#define MV_O(v) ((v)&0xffff)

#define MV_O_IMM(v) (v)

void print_mv(mvalue mv) {
	switch(MV_M(mv)) {
		case MV_IMM: printf("%d", MV_O_IMM(mv)); break;
		case MV_REG: printf("%s", reg_names[MV_R(mv)]); break;
		case MV_REGIND: printf("[%s + %d]", reg_names[MV_R(mv)], MV_O(mv)); break;
		case MV_IMMIND: printf("[%d]", MV_O(mv)); break;
	}
}

void print_mvalues(mvalue mvalues[3]) {
	print_mv(mvalues[0]);
	printf(", ");
	print_mv(mvalues[1]);
	printf(" -> ");
	print_mv(mvalues[2]);
	printf("\n");
}

// TODO: We can delay a --SP

#define READ_VALUE(i, v) do { \
	mvalue mv, mvd; \
	if(v < 0x8)          { mvd = mv = MV(MV_REG, R_A + v, 0); } \
	else if(v < 0x10)  { mvd = mv = MV(MV_REGIND, R_A + v - 0x8, 0); } \
	else if(v < 0x18)  { \
		c_mov(r(i, R_A + v - 0x10)); \
		c_add16_k(r(i), C->data[pc]); \
		mvd = mv = MV(MV_REGIND, i, 0); \
		++pc; \
	} else if(v >= 0x20) { mvd = mv = MV(MV_IMM, 0, v - 0x20); } \
	else { \
		switch(v) { \
			case VAL_PC: { \
				mv = MV(MV_IMM, 0, pc); \
				mvd = MV(MV_REG, R_PC, 0); \
				break; \
			} \
			case VAL_SP: { \
				mvd = mv = MV(MV_REG, R_SP, 0); \
				break; \
			} \
			case VAL_O: { \
				mvd = mv = MV(MV_REG, R_O, 0); \
				break; \
			} \
			case VAL_LIT: { \
				mvd = mv = MV(MV_IMM, 0, C->data[pc]); \
				++pc; \
				break; \
			} \
			case VAL_PEEK: { \
				mvd = mv = MV(MV_REGIND, R_SP, 0); \
			} \
			case VAL_POP: { \
				/* VAL_PUSH is ok in vb */ \
				if(i == 1 || (vb != VAL_PEEK && vb != VAL_POP && vb != VAL_SP)) { \
					delayed_sp_dec = 1; \
					mvd = mv = MV(MV_REGIND, R_SP, 0); \
				} else { \
					c_mov16(m_sib(i, R_CONTEXT, R_SP, SS_2)); \
					c_mov16(r(2, R_SP)); \
					c_dec16(r(R_SP)); \
					mv = MV(MV_REG, i, 0); \
					mvd = MV(MV_REGIND, 2, 0); \
				} \
				break; \
			} \
			case VAL_PUSH: { \
				if(i == 1) { \
					/* TODO: Store away mvalues[0] and mvalues[2] if they are SP or [SP] */ \
				} \
				if(delayed_sp_dec) { \
					delayed_sp_dec = 0; /* Cancel */ \
				} else { \
					c_inc16(r(R_SP)); \
				} \
				mvd = mv = MV(MV_REGIND, R_SP, 0); \
				break; \
			} \
			case VAL_LITMEM: { \
				mvd = mv = MV(MV_IMMIND, 0, C->data[pc]); \
				++pc; \
				break; \
			} \
			default: \
				assert(!"Unimplemented read"); \
		} \
	} \
	mvalues[i] = mv; \
	if(i == 0) { \
		mvalues[2] = mvd; \
	} \
} while(0)

#define c_rex_modmv_(suff, mv_r, mvm) { \
	assert(MV_M(mvm) != MV_IMM); \
	if(MV_M(mvm) == MV_REG) { \
		c_rex_modr(mv_r, MV_R(mvm)); \
	} else	if(MV_M(mvm) == MV_REGIND) { \
		c_rex_modm_sib(mv_r, R_CONTEXT, MV_R(mvm), SS_2); \
	} else { \
		c_rex_modm_d32(mv_r, R_CONTEXT, MV_O(mvm)*2); \
	} \
}

#define c_modmv_(suff, mv_r, mvm) { \
	if(MV_M(mvm) == MV_REG) { \
		c_modr(mv_r, MV_R(mvm)); \
	} else	if(MV_M(mvm) == MV_REGIND) { \
		c_modm_sib(mv_r, R_CONTEXT, MV_R(mvm), SS_2); \
	} else { \
		c_modm_d32(mv_r, R_CONTEXT, MV_O(mvm)*2); \
	} \
}

#define c_rex_modmv(mv, mvm) c_rex_modmv_(, MV_R(mv), mvm)
#define c_rex_modexmv(mvm) c_rex_modmv_(ex, REXT, mvm)
#define c_modmv(mv, mvm) c_modmv_(, MV_R(mv), mvm)
#define c_modexmv(mvm) c_modmv_(ex, REXT, mvm)

#define c_mov16_k_adapt(v, k16) do { \
	if((k16) == 0 && MV_M(v) == MV_REG) { \
		c_xor(r(MV_R(v), MV_R(v))); \
	} else { \
		c_mov16_k(mv(v), (k16)); \
	} \
} while(0)

void trace(context* C) {
	u16 pc = C->pc;
	u16 iw, data_beg;
	u8* LOC;
	u8* code_beg;
	skipslot sslot;
	int has_skipslot = 0, require_prolog = 1;
	int i;
	
	int bindex = C->next_block++;
	block* bl = C->blocks + bindex;

	if(C->next_block == 0)
		C->next_block = 1;

	invalidate_block(C, bindex);

	LOC = C->code_cache_cur;
	code_beg = LOC;
	data_beg = pc;
	
	assert(C->codeloc[pc] == 0);
	
	printf("Start tracing at %d\n", pc);

	while(1) {
		u16 instr, op, next_instr;
		u32 va, vb;
		mvalue mvalues[3];
		u16 next_pc;
		int last_instr, need_o, delayed_sp_dec = 0;
		
		require_prolog = 1;
		
		instr = C->data[pc];
		next_pc = pc + instr_size(instr);
		next_instr = C->data[next_pc];
		
		op = OP(instr);
		va = AV(instr);
		vb = BV(instr);
		
		// Determine if this should be the last instruction in this block
		last_instr = (va == VAL_PC && !has_skipslot) || (LOC >= C->code_cache_end) || (C->codeloc[next_pc] != 0);
		need_o = last_instr || OP_IS_IF(op) || requires_o(next_instr);
		
		// TODO: If instr is a known jump into the same block (so that last_instr == 0), check whether the actual destination
		// requires_o.
		
		printf("OP: %d, need_o = %d, instr_size = %d\n", op, need_o, instr_size(instr));
		
		C->codeloc[pc] = LOC - C->code_cache;

		++pc;
		
		READ_VALUE(0, va);
		READ_VALUE(1, vb);
				
		if(mvalues[0] != mvalues[2] && MV_IS_MEM(mvalues[0]) && MV_IS_MEM(mvalues[2])) {
			// We need to move from some register to put the value
			// in place. Best to allocate mvalues[0] to a register.
			mvalue dest = MV(MV_REG, 0, 0);
			c_mov16(mv(dest, mvalues[0]));
			mvalues[0] = dest;
		} else if(MV_IS_MEM(mvalues[0]) && MV_IS_MEM(mvalues[1])) {
			// Move mvalues[1] to R1
			mvalue dest = MV(MV_REG, 1, 0);
			c_mov16(mv(dest, mvalues[1]));
			mvalues[1] = dest;
		}
		
		if(MV_M(mvalues[0]) == MV_IMM && MV_M(mvalues[1]) != MV_IMM) {
			if(MV_M(mvalues[2]) == MV_REG) {
				// Use same register as destination
				c_mov16_k_adapt(mvalues[2], MV_O_IMM(mvalues[0]));
				mvalues[0] = mvalues[2];
			} else {
				mvalue dest = MV(MV_REG, 2, 0);
				c_mov16_k_adapt(dest, MV_O_IMM(mvalues[0]));
				mvalues[0] = dest;
			}
		}
		
		print_mvalues(mvalues);
				
		// Do op
		switch(op) {
			/*
			case OP_SET: {
				// c_mov16(r(reg[0], reg[1]));
				break;
			}
			
			case OP_ADD: {
				if(need_o) c_xor16(r(R_O, R_O));
				// c_add16(r(reg[0], reg[1]));
				if(need_o) c_adc16(r(R_O, R_O));
				break;
			}*/
			
			case OP_SUB: {
				if(MV_M(mvalues[0]) == MV_IMM && MV_M(mvalues[1]) == MV_IMM) {
					mvalue res = (mvalues[0] - mvalues[1]) & 0xffff;
					if(need_o) {
						c_mov16_k_adapt(
							MV(MV_REG, R_O, 0),
							res > mvalues[0] ? (u16)-1 : 0);
					}
					mvalues[0] = res;
				} else {
					if(need_o) c_xor(r(R_O, R_O));

					if(MV_M(mvalues[1]) == MV_IMM)
						c_sub16_k(mv(mvalues[0]), MV_O_IMM(mvalues[1]));
					else if(MV_IS_MEM(mvalues[0]))
						c_sub16_r(mv(mvalues[1], mvalues[0]));
					else
						c_sub16(mv(mvalues[0], mvalues[1]));
					
					if(need_o) c_sbb16(r(R_O, R_O));
				}
				break;
			}
			
			default:
				printf("op: %d\n", op);
				assert(!"Unimplemented operator");
		}
		
		if(delayed_sp_dec) {
			c_dec16(r(R_SP));
		}
		
		// Direct jump if destination is known to be a valid
		// instruction in this block.
		
		if(va == VAL_PC
		&& MV_M(mvalues[0]) == MV_IMM
		&& C->codeloc[MV_O_IMM(mvalues[0])]
		&& MV_O_IMM(mvalues[0]) >= data_beg
		&& MV_O_IMM(mvalues[0]) < pc) {
			mvalues[2] = mvalues[0];
			// pc = MV_O_IMM(mvalues[0]);
			c_jmp(C->code_cache + C->codeloc[MV_O_IMM(mvalues[0])]);
			require_prolog = 0;
		}
		
		// TODO: We can use 32-bit move between registers
			
		if(mvalues[2] != mvalues[0]
		&& MV_M(mvalues[2]) != MV_IMM) {
			if(MV_M(mvalues[0]) == MV_IMM)
				c_mov16_k_adapt(mvalues[2], MV_O_IMM(mvalues[0]));
			else if(MV_IS_MEM(mvalues[2]))
				c_mov16_r(mv(mvalues[0], mvalues[2]));
			else
				c_mov16(mv(mvalues[2], mvalues[0]));
		}
		
		// TODO: Only write this if we did an actual indirect jump
		if(va == VAL_PC && MV_M(mvalues[2]) != MV_IMM) {
			c_jmp(C->proc_indirect_jump);
			require_prolog = 0;
		}
		
		// After instruction
		if(has_skipslot) {
			// Patch sslot.codeloc with offset to current
			*sslot.codeloc = (LOC - ((u8*)sslot.codeloc + 4));
			
			has_skipslot = 0;
		}

		if(OP_IS_IF(op)) {
			has_skipslot = 1;
			sslot.codeloc = (u32*)(LOC - 4); // TODO: This should be the address to the jump location
			sslot.pc = pc; // TODO: This should be the pc of the next instruction
		}
		
		if(last_instr) {
			break;
		}
	}
	
	if(require_prolog) {
		c_indirect_jump(pc);
	}
	
	if(has_skipslot) {
		// Handle trailing skipslot
		*sslot.codeloc = (LOC - ((u8*)sslot.codeloc + 4));
		c_indirect_skipjump(sslot.pc);
	}

	invalidate_data_range(C, data_beg, pc);
	invalidate_code_range(C, code_beg, LOC);
	
	for(iw = data_beg; iw != pc; ++iw) {
		C->blockmap[iw] = bindex;
	}

	bl->data_beg = data_beg;
	bl->data_end = pc;
	bl->code_beg = code_beg;
	bl->code_end = LOC;
	
	C->code_cache_cur = LOC;
	
	if(C->code_cache_cur >= C->code_cache_end) {
		C->code_cache_cur = C->code_cache + 1;
	}
	
	dump_asm(code_beg, LOC);
}

void dump_asm(u8* beg, u8* end) {
	ud_t ud_obj;
	
	ud_init(&ud_obj);
	ud_set_input_buffer(&ud_obj, (unsigned char*)beg, end - beg);
	ud_set_pc(&ud_obj, 0);
	ud_set_mode(&ud_obj, 64);
	ud_set_syntax(&ud_obj, UD_SYN_INTEL);
	
	while (ud_disassemble(&ud_obj)) {
		printf("%08x\t", (unsigned int)ud_insn_off(&ud_obj));
		printf("%s\n", ud_insn_asm(&ud_obj));
	}
}

int main(int argc, char* argv[])
{
	size_t sz;
	ud_t ud_obj;

	context C;
	context_init(&C);
	
	parse(&C,
		"SUB POP, PUSH\n"
		"SUB PC, PC\n"
	);
	
	trace(&C);
	
	//dump_asm(C.common_code, C.common_code_end);
	
	//((void(*)(context*))C.proc_indirect_jump_c)(&C);

	return 0;
}

