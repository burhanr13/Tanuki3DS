#undef PTR
#ifdef FASTMEM
#define PTR(addr) ((void*) &dsp->mem[addr])
#else
#define PTR(addr) sw_pptr(dsp->mem, addr)
#endif