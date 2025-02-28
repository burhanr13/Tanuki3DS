#include "loader.h"

#include <stdio.h>
#include <stdlib.h>

#include "3ds.h"

#include "svc_types.h"

u32 load_elf(E3DS* s, char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        lerror("no such file");
        return -1;
    }

    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof ehdr, 1, fp) < 1) {
        fclose(fp);
        return -1;
    }

    Elf32_Phdr* phdrs = calloc(ehdr.e_phnum, ehdr.e_phentsize);
    fseek(fp, ehdr.e_phoff, SEEK_SET);
    if (fread(phdrs, ehdr.e_phentsize, ehdr.e_phnum, fp) < ehdr.e_phnum) {
        fclose(fp);
        return -1;
    }
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        u32 perm = 0;
        if (phdrs[i].p_flags & PF_R) perm |= PERM_R;
        if (phdrs[i].p_flags & PF_W) perm |= PERM_W;
        if (phdrs[i].p_flags & PF_X) perm |= PERM_X;
        memory_virtalloc(s, phdrs[i].p_vaddr, phdrs[i].p_memsz, perm,
                         MEMST_CODE);
        void* segment = PTR(phdrs[i].p_vaddr);
        fseek(fp, phdrs[i].p_offset, SEEK_SET);
        if (fread(segment, 1, phdrs[i].p_filesz, fp) < phdrs[i].p_filesz) {
            fclose(fp);
            free(phdrs);
            return -1;
        }

        linfo("loaded elf segment at %08x", phdrs[i].p_vaddr);
    }
    free(phdrs);

    fclose(fp);

    s->romimage.fp = nullptr;

    memory_virtalloc(s, STACK_BASE - BIT(14), BIT(14), PERM_RW, MEMST_PRIVATE);

    return ehdr.e_entry;
}

// 3dsx file format info from citra
u32 load_3dsx(E3DS* s, char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        lerror("no such file");
        return -1;
    }

    _3DSXHeader hdr;
    fread(&hdr, 1, sizeof(_3DSXHeader), fp);

    fseek(fp, hdr.hdrSz, SEEK_SET);
    _3DSXRelHeader relhdr[3];
    fread(relhdr, hdr.relHdrSz, 3, fp);

    // the start address here is arbitrary
    u32 start_addr = 0x10'0000;

    u32 segfilesz[3] = {hdr.codeSz, hdr.rodataSz, hdr.dataBssSz - hdr.bssSz};
    u32 segmemsz[3] = {
        (hdr.codeSz + 0xfff) & ~0xfff,
        (hdr.rodataSz + 0xfff) & ~0xfff,
        (hdr.dataBssSz + 0xfff) & ~0xfff,
    };
    u32 segstarts[3] = {start_addr, start_addr + segmemsz[0],
                        start_addr + segmemsz[0] + segmemsz[1]};

    memory_virtalloc(s, segstarts[0], segmemsz[0], PERM_RX, MEMST_CODE);
    memory_virtalloc(s, segstarts[1], segmemsz[1], PERM_R, MEMST_CODE);
    memory_virtalloc(s, segstarts[2], segmemsz[2], PERM_RW, MEMST_CODE);

    for (int i = 0; i < 3; i++) {
        fread(PTR(segstarts[i]), 1, segfilesz[i], fp);
    }

    for (int seg = 0; seg < 3; seg++) {
        u32* pos = PTR(segstarts[seg]);
        for (int r = 0; r < relhdr[seg].numAbsolute; r++) {
            _3DSXRelocation rel;
            fread(&rel, sizeof rel, 1, fp);
            pos += rel.skip;
            for (int p = 0; p < rel.patch; p++) {
                *pos++ += start_addr;
            }
        }
        pos = PTR(segstarts[seg]);
        for (int r = 0; r < relhdr[seg].numRelative; r++) {
            _3DSXRelocation rel;
            fread(&rel, sizeof rel, 1, fp);
            pos += rel.skip;
            for (int p = 0; p < rel.patch; p++) {
                *pos -= (void*) pos - PTR(segstarts[0]);
                pos++;
            }
        }
    }

    s->romimage.fp = fp;
    s->romimage.romfs_off = hdr.romfsOff;

    memory_virtalloc(s, STACK_BASE - BIT(14), BIT(14), PERM_RW, MEMST_PRIVATE);

    return start_addr;
}

u32 load_ncsd(E3DS* s, char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    NCSDHeader hdrncsd;
    fread(&hdrncsd, sizeof hdrncsd, 1, fp);

    u32 ncchbase = hdrncsd.part[0].offset * 0x200;
    fclose(fp);

    return load_ncch(s, filename, ncchbase);
}

u32 load_ncch(E3DS* s, char* filename, u64 offset) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    u64 base = offset;
    u64 ncchbase = base;

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

    memory_virtalloc(s, exhdr.sci.text.vaddr, exhdr.sci.text.pages * PAGE_SIZE,
                     PERM_RX, MEMST_CODE);
    void* text = PTR(exhdr.sci.text.vaddr);
    memcpy(text, code, exhdr.sci.text.size);

    memory_virtalloc(s, exhdr.sci.rodata.vaddr,
                     exhdr.sci.rodata.pages * PAGE_SIZE, PERM_R, MEMST_CODE);
    void* rodata = PTR(exhdr.sci.rodata.vaddr);
    memcpy(rodata, code + exhdr.sci.text.pages * PAGE_SIZE,
           exhdr.sci.rodata.size);

    memory_virtalloc(s, exhdr.sci.data.vaddr,
                     exhdr.sci.data.pages * PAGE_SIZE + exhdr.sci.bss, PERM_RW,
                     MEMST_CODE);
    void* data = PTR(exhdr.sci.data.vaddr);
    memcpy(data,
           code + exhdr.sci.text.pages * PAGE_SIZE +
               exhdr.sci.rodata.pages * PAGE_SIZE,
           exhdr.sci.data.size);

    free(code);

    s->romimage.fp = fp;
    s->romimage.exheader_off = ncchbase + 0x200;
    s->romimage.exefs_off = ncchbase + hdrncch.exefs.offset * 0x200;
    s->romimage.romfs_off = ncchbase + hdrncch.romfs.offset * 0x200 + 0x1000;

    memory_virtalloc(s, STACK_BASE - exhdr.sci.stacksz, exhdr.sci.stacksz,
                     PERM_RW, MEMST_PRIVATE);

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