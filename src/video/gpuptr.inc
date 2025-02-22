#undef PTR
#ifdef FASTMEM
#define PTR(addr) ((void*) &gpu->mem[addr])
#else
#define PTR(addr) sw_pptr(gpu->mem, addr)
#endif