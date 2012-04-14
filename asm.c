#include "asm.h"

#if WIN32

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <windows.h>

#define MCPROT_RW       PAGE_READWRITE
#define MCPROT_RX       PAGE_EXECUTE_READ
#define MCPROT_RWX      PAGE_EXECUTE_READWRITE

void* mcode_alloc(size_t sz, int prot) {
	void* p = VirtualAlloc(NULL, sz,
  		MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN, MCPROT_RWX);
	return p;
}

#else

#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define MCPROT_RW       (PROT_READ|PROT_WRITE)
#define MCPROT_RX       (PROT_READ|PROT_EXEC)
#define MCPROT_RWX      (PROT_READ|PROT_WRITE|PROT_EXEC)

void* mcode_alloc(size_t sz, int prot) {
	void* p = mmap(NULL, sz, MCPROT_RWX, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	return p;
}

#endif
