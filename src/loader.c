#include "loader.h"

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "3ds.h"
#include "filesystems.h"
#include "svc_types.h"

u32 load_elf(HLE3DS* s, char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        eprintf("no such file\n");
        return 0;
    }

    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof ehdr, 1, fp) < 1) {
        fclose(fp);
        return 0;
    }

    Elf32_Phdr* phdrs = calloc(ehdr.e_phnum, ehdr.e_phentsize);
    fseek(fp, ehdr.e_phoff, SEEK_SET);
    if (fread(phdrs, ehdr.e_phentsize, ehdr.e_phnum, fp) < ehdr.e_phnum) {
        fclose(fp);
        return 0;
    }
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        u32 perm = 0;
        if (phdrs[i].p_flags & PF_R) perm |= PERM_R;
        if (phdrs[i].p_flags & PF_W) perm |= PERM_W;
        if (phdrs[i].p_flags & PF_X) perm |= PERM_X;
        hle3ds_vmmap(s, phdrs[i].p_vaddr, phdrs[i].p_memsz, perm, MEMST_CODE,
                     false);
        void* segment = PTR(phdrs[i].p_vaddr);
        fseek(fp, phdrs[i].p_offset, SEEK_SET);
        if (fread(segment, 1, phdrs[i].p_filesz, fp) < phdrs[i].p_filesz) {
            fclose(fp);
            free(phdrs);
            return 0;
        }

        linfo("loaded elf segment at %08x", phdrs[i].p_vaddr);
    }
    free(phdrs);

    fclose(fp);

    s->gamecard.fp = NULL;

    return ehdr.e_entry;
}

u32 load_ncsd(HLE3DS* s, char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    NCSDHeader hdrncsd;
    fread(&hdrncsd, sizeof hdrncsd, 1, fp);

    u32 base = hdrncsd.part[0].offset * 0x200;
    u32 ncchbase = base;

    fseek(fp, base, SEEK_SET);

    NCCHHeader hdrncch;
    fread(&hdrncch, sizeof hdrncch, 1, fp);

    ExHeader exhdr;
    fread(&exhdr, sizeof exhdr, 1, fp);

    linfo("loading code from exefs");

    base += hdrncch.exefs.offset * 0x200;

    fseek(fp, base, SEEK_SET);

    ExeFSHeader hdrexefs;
    fread(&hdrexefs, sizeof hdrexefs, 1, fp);

    base += 0x200;

    u32 codeoffset = 0;
    u32 codesize = 0;
    for (int i = 0; i < 10; i++) {
        if (!strcmp(hdrexefs.file[i].name, ".code")) {
            codeoffset = hdrexefs.file[i].offset;
            codesize = hdrexefs.file[i].size;
        }
    }
    if (!codesize) return -1;

    u8* code = malloc(codesize);
    fseek(fp, base + codeoffset, SEEK_SET);
    fread(code, 1, codesize, fp);

    if (exhdr.sci.flags.compressed) {
        u8* buf = lzssrev_decompress(code, codesize, &codesize);
        free(code);
        code = buf;
    }

    hle3ds_vmmap(s, exhdr.sci.text.vaddr, exhdr.sci.text.pages * PAGE_SIZE,
                 PERM_RX, MEMST_CODE, false);
    void* text = PTR(exhdr.sci.text.vaddr);
    memcpy(text, code, exhdr.sci.text.size);
    mprotect(text, exhdr.sci.text.pages * PAGE_SIZE, PROT_READ);

    hle3ds_vmmap(s, exhdr.sci.rodata.vaddr, exhdr.sci.rodata.pages * PAGE_SIZE,
                 PERM_R, MEMST_CODE, false);
    void* rodata = PTR(exhdr.sci.rodata.vaddr);
    memcpy(rodata, code + exhdr.sci.text.pages * PAGE_SIZE,
           exhdr.sci.rodata.size);
    mprotect(rodata, exhdr.sci.rodata.pages * PAGE_SIZE, PROT_READ);

    hle3ds_vmmap(s, exhdr.sci.data.vaddr,
                 exhdr.sci.data.pages * PAGE_SIZE + exhdr.sci.bss, PERM_RW,
                 MEMST_CODE, false);
    void* data = PTR(exhdr.sci.data.vaddr);
    memcpy(data,
           code + exhdr.sci.text.pages * PAGE_SIZE +
               exhdr.sci.rodata.pages * PAGE_SIZE,
           exhdr.sci.data.size);

    free(code);

    s->gamecard.fp = fp;
    s->gamecard.exheader_off = ncchbase + 0x200;
    s->gamecard.exefs_off = ncchbase + hdrncch.exefs.offset * 0x200;
    s->gamecard.romfs_off = ncchbase + hdrncch.romfs.offset * 0x200 + 0x1000;

    return exhdr.sci.text.vaddr;
}

u8* lzssrev_decompress(u8* in, u32 src_size, u32* dst_size) {
    *dst_size = src_size + *(u32*) &in[src_size - 4];
    u8* out = malloc(*dst_size);
    memcpy(out, in, src_size);

    u8* src = out + src_size;
    u8* dst = src + *(u32*) (src - 4) - 1;
    u8* fin = src - (*(u32*) (src - 8) & MASK(24));
    src = src - src[-5] - 1;

    u8 flags;
    int count = 0;
    while (src > fin) {
        if (count == 0) {
            flags = *src--;
            count = 8;
        }
        if (flags & 0x80) {
            src--;
            int disp = *(u16*) src;
            src--;
            int len = disp >> 12;
            disp &= 0xfff;
            len += 3;
            disp += 3;
            for (int i = 0; i < len; i++) {
                *dst = dst[disp];
                dst--;
            }
        } else {
            *dst-- = *src--;
        }
        flags <<= 1;
        count--;
    }

    return out;
}