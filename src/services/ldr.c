#include "ldr.h"

#include "../3ds.h"
#include "../memory.h"

DECL_PORT(ldr_ro) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            u32 crssrc = cmdbuf[1];
            u32 size = cmdbuf[2];
            u32 crsdst = cmdbuf[3];
            printfln("Initialize with crs=%08x dst=%08x sz=%x", crssrc, crsdst,
                     size);

            memory_virtmirror(s, crssrc, crsdst, size, PERM_R);

            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0002:
            linfo("LoadCRR");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0004: {
            u32 srcaddr = cmdbuf[1];
            u32 dstaddr = cmdbuf[2];
            u32 size = cmdbuf[3];
            u32 dataaddr = cmdbuf[4];
            u32 datasize = cmdbuf[6];
            u32 bssaddr = cmdbuf[7];
            u32 bsssize = cmdbuf[8];
            bool autolink = cmdbuf[9];
            ldebug("LoadCRO with src=%08x dst=%08x size=%x data=%08x,sz=%x "
                  "bss=%08x,sz=%x autolink=%d",
                  srcaddr, dstaddr, size, dataaddr, datasize, bssaddr, bsssize,
                  autolink);

            memory_virtmirror(s, srcaddr, dstaddr, size, PERM_RX);

            cro_relocate_hdr(s, dstaddr);
            
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        default:
            ldebug("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void cro_relocate_hdr(E3DS* s, u32 vaddr) {
#define REL(m) hdr->m.addr += vaddr
    CROHeader* hdr = PTR(vaddr);

    hdr->name_addr += vaddr;
    REL(code);
    REL(data);
    REL(modulename);
    REL(segmenttable);
    REL(exports.named_symbols);
    REL(exports.indexed_symbols);
    REL(exports.strings);
    REL(exports.nametree);
    REL(import_table);
    REL(external_patches);
    REL(imports.named_symbols);
    REL(imports.indexed_symbols);
    REL(imports.anon_symbols);
    REL(imports.strings);
    REL(static_anon_symbols);
    REL(internal_patches);
    REL(static_anon_patches);
#undef REL
}