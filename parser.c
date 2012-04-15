#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

enum {
	T_POP = VAL_POP,
	T_PEEK = VAL_PEEK,
	T_PUSH = VAL_PUSH,
	T_SP = VAL_SP,
	T_PC = VAL_PC,
	T_O = VAL_O,
	T_JSR,
	
	T_IDENT, T_INT,	T_COMMA, T_PLUS,
	T_LBRACKET, T_RBRACKET,
	T_COLON, T_NL,
	
	T_A, T_B, T_C,
	T_X, T_Y, T_Z,
	T_I, T_J,
	
	T_EOF
};

typedef struct parser {
	int token, line;
	char const* cur;
	
	char const* tok_data;
	char const* tok_data_end;
	u16 tok_int_data;
	
	u16* dest;
	u16* dest_end;
} parser;

#define KW4(a,b,c,d) (((a)<<24)+((b)<<16)+((c)<<8)+(d))
#define KW3(a,b,c) (((a)<<16)+((b)<<8)+(c))
#define KW2(a,b) (((a)<<8)+(b))

static void lex(parser* P) {
	char c;
repeat:
	c = *P->cur;
	if(!c) {
		P->token = T_EOF;
		return;
	}
	
	switch(c) {
		case '\0':
			P->token = T_EOF;
			return;
		case '[': ++P->cur; P->token = T_LBRACKET; break;
		case ']': ++P->cur; P->token = T_RBRACKET; break;
		case ':': ++P->cur; P->token = T_COLON; break;
		case ',': ++P->cur; P->token = T_COMMA; break;
		case '+': ++P->cur; P->token = T_PLUS; break;
		case '\n': ++P->cur; P->token = T_NL; ++P->line; break;
		
		case '\r': case ' ': case '\t':
			++P->cur;
			goto repeat;
		
		default: {
			if((c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')) {
				u32 kw = 0;
				P->tok_data = P->cur;
				
				do {
					kw = (kw << 8) + c;
					c = *++P->cur;
				} while((c >= 'a' && c <= 'z')
				|| (c >= 'A' && c <= 'Z'));
				
				P->tok_data_end = P->cur;
				P->token = T_IDENT;
				
				if((P->tok_data_end - P->tok_data) <= 4) {
					switch(kw) {
						case KW3('S','E','T'): P->token = OP_SET; break;
						case KW3('A','D','D'): P->token = OP_ADD; break;
						case KW3('S','U','B'): P->token = OP_SUB; break;
						case KW3('J','S','R'): P->token = T_JSR; break;
						case KW3('I','F','E'): P->token = OP_IFE; break;
						case KW3('I','F','G'): P->token = OP_IFG; break;
						case KW4('P','U','S','H'): P->token = T_PUSH; break;
						case KW4('P','E','E','K'): P->token = T_PEEK; break;
						case KW3('P','O','P'): P->token = T_POP; break;
						case KW2('P','C'): P->token = T_PC; break;
						case KW2('S','P'): P->token = T_SP; break;
						case 'O': P->token = T_O; break;
						
						case 'A': P->token = T_A; break;
						case 'B': P->token = T_B; break;
						case 'C': P->token = T_C; break;
						case 'X': P->token = T_X; break;
						case 'Y': P->token = T_Y; break;
						case 'Z': P->token = T_Z; break;
						case 'I': P->token = T_I; break;
						case 'J': P->token = T_J; break;
					}
				}
			} else if(c >= '0' && c <= '9') {
				u16 i = 0;
				P->tok_data = P->cur;
				
				while(c >= '0' && c <= '9') {
					i = (i * 10) + (c - '0');
					c = *++P->cur;
				}
				
				P->tok_data_end = P->cur;
				P->tok_int_data = i;
				P->token = T_INT;
			}
		}
	}
}

#define W16(w) { *P->dest++ = (w); }

static void parse_error(parser* P) {
	// TODO: Error
	printf("Error on line %d\n", P->line);
	abort();
}

static int test(parser* P, int tok) {
	if(P->token == tok) {
		lex(P);
		return 1;
	}
	
	return 0;
}

static void expect(parser* P, int tok) {
	if(P->token != tok) {
		parse_error(P);
		return;
	}
	
	lex(P);
}

static u16 r_operand(parser* P) {
	u16 v = (u16)P->token;
	switch(v) {
		case T_INT: {
			u16 i = (u16)P->tok_int_data;
			lex(P);
			if(i < 0x20) {
				return i + 0x20;
			} else {
				W16(i);
				return VAL_LIT;
			}
		}
		
		case T_POP: case T_PEEK: case T_PUSH:
		case T_SP: case T_PC: case T_O:
			lex(P);
			return v;
			
		case T_LBRACKET: {
			int used = 0;
			
			lex(P);
			v = 0x1e;
			
			do {
				if(!(used & 1) && P->token >= T_A && P->token <= T_J) {
					used |= 1;
					v = 0x8 + P->token - T_A;
					lex(P);
				} else if(!(used & 2) && P->token == T_INT) {
					used |= 2;
					W16((u16)P->tok_int_data);
					lex(P);
				} else {
					parse_error(P);
				}
			} while(test(P, T_PLUS));
			
			expect(P, T_RBRACKET);
			return (used == 3) ? v + 0x8 : v;
		}
		
		default: {
			if(v >= T_A && v <= T_J) {
				lex(P);
				return v - T_A;
			} else {
				printf("Invalid operand\n");
				parse_error(P);
				return 0;
			}
		}
	}
}

void r_file(parser* P) {
	while(P->token < 16 || P->token == T_NL || P->token == T_COLON || P->token == T_JSR) {
		if(P->token < 16) {
			// Instruction
			u16* first = P->dest++;
			u16 instr = P->token;
			
			lex(P);
			
			instr |= r_operand(P) << 4;
			expect(P, T_COMMA);
			instr |= r_operand(P) << 10;
			
			*first = instr;
		} else if(P->token == T_JSR) {
			u16* first = P->dest++;
			u16 instr = (OP_EXT_JSR << 4);
			
			lex(P);
			
			instr |= r_operand(P) << 10;
			
			*first = instr;
		} else if(P->token == T_NL) {
			// Ignore
			lex(P);
		} else {
			parse_error(P);
		}
	}
	
	expect(P, T_EOF);
}

void parse(context* C, char const* str) {
	parser P;
	
	P.line = 1;
	P.cur = str;
	P.dest = C->data;
	P.dest_end = C->data + 0x10000;
	
	lex(&P);
	
	r_file(&P);
}
