#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/loader.h"

int main(int argc, char** argv) {
    if (argc < 2) return -1;
    char* filename = argv[1];

    char* ext = strrchr(filename, '.');
    if (!ext) return -1;

    char* outfile = malloc(strlen(filename) + 2);
    strcpy(outfile, filename);
    strcpy(outfile + (ext - filename), ".cxi");

    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    if (strcmp(ext, ".3ds")) {
        return -1;
    }

    NCSDHeader hdr;
    fread(&hdr, sizeof hdr, 1, fp);

    u32 base = hdr.part[0].offset * 0x200;
    u32 size = hdr.part[0].size * 0x200;

    fseek(fp, base, SEEK_SET);

    FILE* outfp = fopen(outfile, "wb");

    void* buf = malloc(size);
    fread(buf, 1, size, fp);
    fwrite(buf, 1, size, outfp);

    fclose(fp);
    fclose(outfp);

    return 0;
}