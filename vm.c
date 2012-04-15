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
void invalidate_block(context* C, int bindex);
void trace(context* C);

u8* load_context_state(u8* LOC) {
	c_movzx_w(m_si_d32(R_SP, R_CONTEXT, SS_1, offsetof(context, sp)));
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
	// c_xor(r(EAX, EAX));
	c_xor(r(ECX, ECX));
	c_xor(r(EDX, EDX));

	c_movzx_w(m_si_d32(R_PC, R_CONTEXT, SS_1, offsetof(context, pc)));

	return LOC;
}


u8* save_context_state(u8* LOC) {
	c_mov16_r(m_si_d32(R_SP, R_CONTEXT, SS_1, offsetof(context, sp)));
	c_mov16_r(m_si_d32(R_O, R_CONTEXT, SS_1, offsetof(context, o)));
	
	c_mov16_r(m_d32(R_A, R_CONTEXT, offsetof(context, regs) + 0));
	c_mov16_r(m_d32(R_B, R_CONTEXT, offsetof(context, regs) + 2));
	c_mov16_r(m_d32(R_C, R_CONTEXT, offsetof(context, regs) + 4));
	c_mov16_r(m_d32(R_X, R_CONTEXT, offsetof(context, regs) + 6));
	c_mov16_r(m_d32(R_Y, R_CONTEXT, offsetof(context, regs) + 8));
	c_mov16_r(m_d32(R_Z, R_CONTEXT, offsetof(context, regs) + 10));
	c_mov16_r(m_d32(R_I, R_CONTEXT, offsetof(context, regs) + 12));
	c_mov16_r(m_d32(R_J, R_CONTEXT, offsetof(context, regs) + 14));
	
	return LOC;
}

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

		// TODO: Skip the instruction at EAX
		
		C->proc_indirect_jump = LOC;
		c_movzx_w(m_sib_d32(EAX, R_CONTEXT, R_PC, SS_2, offsetof(context, codeloc)));
		c_test(r(EAX, EAX));
		c_jz(0);
		L = LOC;
			c_add(mq_d32(RAX, R_CONTEXT, offsetof(context, code_cache)));
			c_jmp_ind(r(RAX));
		c_label8(L);

		LOC = save_context_state(LOC);

		c_push_q(R_CONTEXT);
		c_callrel(trace);
		c_wide(); c_add_k8(r(RSP), 8);

		LOC = load_context_state(LOC);

		c_jmp(C->proc_indirect_jump);
		
		C->proc_indirect_jump_c = LOC;
		
		c_mov(mq_d8(RAX, RSP, 8));
		c_push_q(RBP);
		c_push_q(RBX);
		c_push_q(RSI);
		c_push_q(RDI);
		c_push_q(R12);
		c_push_q(R13);
		c_push_q(R14);
		c_push_q(R15);
		
		c_mov(rq(R_CONTEXT, RAX));

		LOC = load_context_state(LOC);
		
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

		C->proc_invalidate = LOC;

		LOC = save_context_state(LOC);

		// Invalidate block number CL
		c_movzx_b(r(ECX, CL));
		c_push_q(RCX);
		c_push_q(R_CONTEXT);
		c_callrel(invalidate_block);
		c_add_k8(rq(RSP), 16);

		LOC = load_context_state(LOC);
		c_jmp(C->proc_indirect_jump);

		C->common_code_end = LOC;
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

// TODO: Need to keep track of the interval of blocks in use,
// invalidating from the front when code is overwritten.

void invalidate_old_blocks(context* C, u8* beg, u8* end) {
	// Invalidate all blocks that have machine code in the given range
	int i;
	while(C->num_used_blocks > 0) {
		int i = 1 + C->first_used_block;
		if(!(C->blocks[i].code_beg >= end || C->blocks[i].code_end <= beg)) {
			invalidate_block(C, i);
			C->first_used_block = (C->first_used_block + 1) % 255;
			--C->num_used_blocks;
		} else {
			break;
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
	return 1 + (OP(instr) == OP_EXT ? 0 : val_size(AV(instr))) + val_size(BV(instr));
}

/* val_size:
0x10-0x17
0x1e, 0x1f

10???
1111?
abcde

a~b + abcd

       1????
except 11000..11101
*/

int requires_o(u16 instr) {
	return (OP(instr) != OP_EXT && AV(instr) == VAL_O)
	     || BV(instr) == VAL_O
	     || !OP_SETS_O(OP(instr));
}

// Machine values
typedef u32 mvalue;

char const* reg_names[16] = {
	"AX", "CX", "DX", "BX",
	"SP", "BP", "SI", "DI",
	"R8W", "R9W", "R10W", "R11W",
	"R12W", "R13W",	"R14W",	"R15W"
};

#define MV_IMM    (0) // o
#define MV_REG    (1) // Rr
#define MV_IMMIND (2) // [o]
#define MV_REGIND (3) // [Rr]

#define MV_IS_MEM(mv) ((mv)>=(MV_IMMIND<<24))
#define MV_IS_REG(mv) ((mv)&(1<<24))

#define MV(mode, reg, offset) (((mode)<<24)+((reg)<<16)+(offset))

#define MV2_IS_IMM(mv1, mv2) (((mv1)|(mv2))<(MV_REG<<24))

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

void print_mvalues(mvalue mvalue0, mvalue mvalue1, mvalue mvalue2) {
	print_mv(mvalue0);
	printf(", ");
	print_mv(mvalue1);
	printf(" -> ");
	print_mv(mvalue2);
	printf("\n");
}

#define READ_VALUE(i, v) do { \
	mvalue mv, mvd; \
	if(v < 0x8)        { mvd = mv = MV(MV_REG, R_A + v, 0); } \
	else if(v < 0x10)  { mvd = mv = MV(MV_REGIND, R_A + v - 0x8, 0); } \
	else if(v < 0x18)  { \
		c_mov(r(i, R_A + v - 0x10)); \
		c_add16_k(r(i), C->data[pc]); \
		mvd = mv = MV(MV_REGIND, i, 0); \
		++pc; \
	} else if(v >= 0x20) { \
		mvd = mv = MV(MV_IMM, 0, v - 0x20); \
	} \
	else { \
		switch(v) { \
			case VAL_PC: { \
				mv = MV(MV_IMM, 0, pc); \
				mvd = MV(MV_REG, R_PC, 0); \
				break; \
			} \
			case VAL_SP: { \
				mvd = MV(MV_REG, R_SP, 0); \
				if(i == 0 && vb == VAL_PUSH) { \
					c_mov(r(EDX, R_SP)); \
					mv = MV(MV_REG, DX, 0); \
				} else { \
					mv = mvd; \
				} \
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
				if(i == 0 && vb == VAL_PUSH) { \
					c_mov(r(EDX, R_SP)); \
					mvd = mv = MV(MV_REGIND, RDX, 0); \
				} else { \
					mvd = mv = MV(MV_REGIND, R_SP, 0); \
				} \
				break; \
			} \
			case VAL_POP: { \
				/* VAL_PUSH is ok in vb */ \
				if(i == 1 || (vb != VAL_PEEK && vb != VAL_POP && vb != VAL_SP)) { \
					delayed_sp_dec = 1; \
					mvd = mv = MV(MV_REGIND, R_SP, 0); \
				} else { \
					c_mov(r(EDX, R_SP)); \
					c_dec16(r(R_SP)); \
					mv = mvd = MV(MV_REGIND, RDX, 0); \
				} \
				break; \
			} \
			case VAL_PUSH: { \
				if(delayed_sp_dec) { \
					delayed_sp_dec = 0; /* Cancel */ \
				} else { \
					c_inc16(r(R_SP)); \
				} \
				if(i == 0 && vb == VAL_PUSH) { \
					c_mov(r(EDX, R_SP)); \
					mvd = mv = MV(MV_REGIND, RDX, 0); \
				} else { \
					mvd = mv = MV(MV_REGIND, R_SP, 0); \
				} \
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
	mvalue##i = mv; \
	if(i == 0) { \
		mvalue2 = mvd; \
	} \
} while(0)

#define c_rex_modmv_(mv_r, mvm) { \
	assert(MV_M(mvm) != MV_IMM); \
	if(MV_M(mvm) == MV_REG) { \
		c_rex_modr(mv_r, MV_R(mvm)); \
	} else	if(MV_M(mvm) == MV_REGIND) { \
		c_rex_modm_sib(mv_r, R_CONTEXT, MV_R(mvm), SS_2); \
	} else { \
		c_rex_modm_d32(mv_r, R_CONTEXT, MV_O(mvm)*2); \
	} \
}

#define c_modmv_(mv_r, mvm) { \
	if(MV_M(mvm) == MV_REG) { \
		c_modr(mv_r, MV_R(mvm)); \
	} else	if(MV_M(mvm) == MV_REGIND) { \
		c_modm_sib(mv_r, R_CONTEXT, MV_R(mvm), SS_2); \
	} else { \
		c_modm_d32(mv_r, R_CONTEXT, MV_O(mvm)*2); \
	} \
}

#define c_rex_modmv(mv, mvm) c_rex_modmv_(MV_R(mv), mvm)
#define c_rex_modexmv(mvm) c_rex_modmv_(REXT, mvm)
#define c_modmv(mv, mvm) c_modmv_(MV_R(mv), mvm)
#define c_modexmv(mvm) c_modmv_(REXT, mvm)

#define c_mov16_k_adapt(v, k16) do { \
	if((k16) == 0 && MV_M(v) == MV_REG) { \
		c_xor(r(MV_R(v), MV_R(v))); \
	} else { \
		c_mov16_k(mv(v), (k16)); \
	} \
} while(0)

static u8* memory_guard(context* C, u8* LOC, i32 pc, mvalue mv) {
	if(MV_IS_MEM(mv)) {
		u8* L;
		if(MV_M(mv) == MV_REGIND) {
			c_mov8(m_sib_d32(CL,
				R_CONTEXT, MV_R(mv), SS_1, offsetof(context, blockmap)));
		} else {	

			c_mov8(m_d32(CL,
				R_CONTEXT, MV_O(mv) + offsetof(context, blockmap)));
		}
		c_test8(r(CL, CL));
		if(pc >= 0) c_mov_k(EAX, pc);
		c_jcnd_far(JCND_JNZ, C->proc_invalidate);
	}

	return LOC;
}

void trace(context* C) {
	u16 pc = C->pc;
	u16 iw, data_beg;
	u8* LOC;
	u8* code_beg;
	skipslot sslot;
	int has_skipslot = 0;
	
	int bindex = 1 + (C->first_used_block + C->num_used_blocks) % 255;
	block* bl = C->blocks + bindex;

	if(C->num_used_blocks == 255) {
		invalidate_block(C, bindex);
		--C->num_used_blocks;
	}

	LOC = C->code_cache_cur;
	code_beg = LOC;
	data_beg = pc;
	
	assert(C->codeloc[pc] == 0);
	
	printf("Start tracing at %d\n", pc);

	while(1) {
		u16 instr, op, next_instr;
		u32 va, vb;
		mvalue mvalue0, mvalue1, mvalue2;
		u16 next_pc;
		u8* skipjump;
		int last_instr, need_o, delayed_sp_dec = 0;
		
		instr = C->data[pc];
		next_pc = pc + instr_size(instr);
		next_instr = C->data[next_pc];
		
		op = OP(instr);
		va = AV(instr);
		vb = BV(instr);
		
		// TODO: If instr is a known jump into the same block (so that last_instr == 0), check whether the actual destination
		// requires_o.
		
		printf("<%d> OP: %d, instr_size = %d\n", pc, op, instr_size(instr));
		
		C->codeloc[pc] = LOC - C->code_cache;

		++pc;
		
		if(op != OP_EXT) {
			READ_VALUE(0, va);
		} else {
			mvalue0 = mvalue2 = MV(MV_IMM, 0, 0);
		}
		READ_VALUE(1, vb);

		if(op == OP_EXT) {
			// Ignore mvalue0 and mvalue2		
		} else if(op == OP_SET) {
			// Ignore mvalue0
			if(MV_IS_MEM(mvalue2) && MV_IS_MEM(mvalue1)) {
				// Move mvalue1 to R1
				mvalue dest = MV(MV_REG, CX, 0);
				c_mov16(mv(dest, mvalue1));
				mvalue1 = dest;
			}
		} else {
			if(MV_IS_MEM(mvalue0) && MV_IS_MEM(mvalue1)) {
				// Move mvalue1 to R1
				mvalue dest = MV(MV_REG, CX, 0);
				c_mov16(mv(dest, mvalue1));
				mvalue1 = dest;
			}
			
			if(MV_M(mvalue0) == MV_IMM && MV_M(mvalue1) != MV_IMM) {
				if(MV_M(mvalue2) == MV_REG) {
					// Use same register as destination
					c_mov16_k_adapt(mvalue2, MV_O_IMM(mvalue0));
					mvalue0 = mvalue2;
				} else {
					mvalue dest = MV(MV_REG, AX, 0);
					c_mov16_k_adapt(dest, MV_O_IMM(mvalue0));
					mvalue0 = dest;
				}
			}
		}
		
		// Determine if this should be the last instruction in this block
		/*
		last_instr = (mvalue2 == MV(MV_REG, R_PC, 0) && !has_skipslot)
		        || (LOC >= C->code_cache_end)
			|| (C->codeloc[next_pc] != 0);*/
		
		last_instr = C->data[next_pc] == 0;
		need_o = (mvalue2 == MV(MV_REG, R_PC, 0)) || requires_o(C->data[next_pc]);

		print_mvalues(mvalue0, mvalue1, mvalue2);
				
		// Do op
		switch(op) {
			case OP_EXT: {
				if(va == OP_EXT_JSR) {
					mvalue mv = MV(MV_REGIND, R_SP, 0);
					c_dec16(r(R_SP));
					c_mov16_k_adapt(mv, pc);
					mvalue0 = mvalue1;
					mvalue2 = MV(MV_REG, R_PC, 0);
				}
				break;
			}
			case OP_SET: {
				mvalue0 = mvalue1;
				break; // Let store logic do the work
			}
			
			case OP_ADD: {
				if(MV2_IS_IMM(mvalue0, mvalue1)) {
					mvalue res = (mvalue0 + mvalue1) & 0xffff;
					if(need_o) {
						c_mov16_k_adapt(
							MV(MV_REG, R_O, 0),
							res > mvalue0 ? 1 : 0);
					}
					mvalue0 = res;
				} else {
					if(need_o) c_xor(r(R_O, R_O));
					
					if(MV_M(mvalue1) == MV_IMM)
						c_add16_k(mv(mvalue0), MV_O_IMM(mvalue1));
					else if(MV_IS_MEM(mvalue0))
						c_add16_r(mv(mvalue1, mvalue0));
					else
						c_add16(mv(mvalue0, mvalue1));
					
					if(need_o) c_adc(r(R_O, R_O));
				}
				break;
			}
			
			case OP_SUB: {
				if(MV2_IS_IMM(mvalue0, mvalue1)) {
					mvalue res = (mvalue0 - mvalue1) & 0xffff;
					if(need_o) {
						c_mov16_k_adapt(
							MV(MV_REG, R_O, 0),
							res > mvalue0 ? (u16)-1 : 0);
					}
					mvalue0 = res;
				} else {
					if(MV_M(mvalue1) == MV_IMM)
						c_sub16_k(mv(mvalue0), MV_O_IMM(mvalue1));
					else if(MV_IS_MEM(mvalue0))
						c_sub16_r(mv(mvalue1, mvalue0));
					else
						c_sub16(mv(mvalue0, mvalue1));
					
					if(need_o) c_sbb(r(R_O, R_O)); // Top half of R_O doesn't matter
				}
				break;
			}

			case OP_IFG: {
				// TODO: When both are MV_IMM
				if(MV_M(mvalue1) == MV_IMM)
					c_cmp16_k(mv(mvalue0), MV_O_IMM(mvalue1));
				else if(MV_IS_MEM(mvalue0))
					c_cmp16_r(mv(mvalue1, mvalue0));
				else
					c_cmp16(mv(mvalue0, mvalue1));

				c_jcnd_far(JCND_JBE, 0);
				skipjump = LOC;
				break;
			}

			case OP_IFE: {
				if(MV_IS_MEM(mvalue0))
					c_cmp16_r(mv(mvalue1, mvalue0));
				else
					c_cmp16(mv(mvalue0, mvalue1));
				c_jcnd_far(JCND_JNZ, 0);
				skipjump = LOC;
				break;
			}
			
			default:
				printf("op: %d\n", op);
				assert(!"Unimplemented operator");
		}
		
		if(delayed_sp_dec) {
			c_dec16(r(R_SP));
		}
		
		if(op == OP_EXT && va == OP_EXT_JSR) {
			LOC = memory_guard(C, LOC, pc, MV(MV_REGIND, R_SP, 0));
		}
		
		// Direct jump if destination is known to be a valid
		// instruction in this block.

		if(mvalue2 == MV(MV_REG, R_PC, 0)
		&& MV_M(mvalue0) == MV_IMM
		&& C->codeloc[MV_O_IMM(mvalue0)]
		&& MV_O_IMM(mvalue0) >= data_beg
		&& MV_O_IMM(mvalue0) < pc) {
			mvalue2 = mvalue0;
			c_jmp(C->code_cache + C->codeloc[MV_O_IMM(mvalue2)]);
		}
		
		// TODO: We can use 32-bit move between registers
	
		if(!OP_IS_IF(op)) {
			if(mvalue2 != mvalue0
			&& MV_M(mvalue2) != MV_IMM) {
				if(MV_M(mvalue0) == MV_IMM)
					c_mov16_k_adapt(mvalue2, MV_O_IMM(mvalue0));
				else if(MV_IS_MEM(mvalue2))
					c_mov16_r(mv(mvalue0, mvalue2));
				else
					c_mov16(mv(mvalue2, mvalue0));
			}

			LOC = memory_guard(C, LOC, pc, mvalue2);
		}
		
		if(mvalue2 == MV(MV_REG, R_PC, 0)) {
			c_jmp(C->proc_indirect_jump);
		}

		// After instruction
		if(has_skipslot) {
			// Patch sslot.codeloc with offset to current
			*sslot.codeloc = (LOC - ((u8*)sslot.codeloc + 4));
			
			has_skipslot = 0;
		}

		if(OP_IS_IF(op)) {
			has_skipslot = 1;
			sslot.codeloc = (u32*)(skipjump - 4); // TODO: This should be the address to the jump location
			sslot.pc = pc; // TODO: This should be the pc of the next instruction
		}
		
		if(last_instr) {
			c_indirect_jump(pc);
			break;
		}
	}
	
	if(has_skipslot) {
		// Handle trailing skipslot
		*sslot.codeloc = (LOC - ((u8*)sslot.codeloc + 4));
		c_indirect_skipjump(sslot.pc);
	}

	invalidate_data_range(C, data_beg, pc);
	invalidate_old_blocks(C, code_beg, LOC);
	
	for(iw = data_beg; iw != pc; ++iw) {
		C->blockmap[iw] = bindex;
	}

	++C->num_used_blocks;

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
	
	while(ud_disassemble(&ud_obj)) {
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
		"SET PUSH, 15\n"
		"JSR 3\n"
		"SUB PC, 1\n"
		
		"SET A, PEEK\n"
		"IFG A, 1\n"
		"ADD PC, 2\n"
			"SET A, 1\n"
			"SET PC, POP\n"
		// else
			"SUB A, 2\n"
			"SET PUSH, A\n"
			"ADD A, 1\n"
			"SET PUSH, A\n"
			"JSR 3\n"
			"SET B, A\n"
			"JSR 3\n"
			"ADD A, B\n"
			"SET PC, POP\n"
	);
	
	trace(&C);
	
	//printf("Common:\n");
	
	//dump_asm(C.common_code, C.common_code_end);
	
	//((void(*)(context*))C.proc_indirect_jump_c)(&C);

	return 0;
}

