#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/loader.h"

// i hate linkers
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

int main(int argc, char** argv) {
    if (argc < 2) return -1;
    char* filename = argv[1];

    char* ext = strrchr(filename, '.');
    if (!ext) return -1;

    char* outfile = malloc(strlen(filename) + 2);
    strcpy(outfile, filename);
    strcpy(outfile + (ext - filename), ".code");

    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    if (strcmp(ext, ".3ds") && strcmp(ext, ".cxi")) {
        return -1;
    }

    u32 base = 0;
    if (!strcmp(ext, ".3ds")) {
        NCSDHeader hdr;
        fread(&hdr, sizeof hdr, 1, fp);

        base = hdr.part[0].offset * 0x200;

        fseek(fp, base, SEEK_SET);
    }

    NCCHHeader hdr2;
    fread(&hdr2, sizeof hdr2, 1, fp);

    ExHeader exhdr;
    fread(&exhdr, sizeof exhdr, 1, fp);

    printf("%-12s%-12s%-12s%-12s\n", "SECTION", "VADDR", "MEMSZ", "FILESZ");
    printf("%-12s%-12x%-12x%-12x\n", ".text", exhdr.sci.text.vaddr,
           exhdr.sci.text.pages << 12, exhdr.sci.text.size);
    printf("%-12s%-12x%-12x%-12x\n", ".rodata", exhdr.sci.rodata.vaddr,
           exhdr.sci.rodata.pages << 12, exhdr.sci.rodata.size);
    printf("%-12s%-12x%-12x%-12x\n", ".data", exhdr.sci.data.vaddr,
           exhdr.sci.data.pages << 12, exhdr.sci.data.size);
    printf("STACK SIZE %x\n", exhdr.sci.stacksz);
    printf("BSS SIZE %x\n", exhdr.sci.bss);

    base += hdr2.exefs.offset * 0x200;

    fseek(fp, base, SEEK_SET);

    ExeFSHeader hdr3;
    fread(&hdr3, sizeof hdr3, 1, fp);

    base += 0x200;

    u32 codeoffset = 0;
    u32 codesize = 0;
    for (int i = 0; i < 10; i++) {
        if (!strcmp(hdr3.file[i].name, ".code")) {
            codeoffset = hdr3.file[i].offset;
            codesize = hdr3.file[i].size;
        }
    }
    if (!codesize) return -1;

    u8* buf = malloc(codesize);
    fseek(fp, base + codeoffset, SEEK_SET);
    fread(buf, 1, codesize, fp);

    u8* code = lzssrev_decompress(buf, codesize, &codesize);

    FILE* outfp = fopen(outfile, "wb");

    fwrite(code, 1, codesize, outfp);

    fclose(fp);
    fclose(outfp);

    return 0;
}