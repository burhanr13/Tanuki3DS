#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESETPTR(p) ((p) = (void*) ((void*) (p) - (void*) addr))
#define GETPTR(p) ((void*) font + (u32) (p));

void font_reset(CFNT_s* font, void* addr) {
    font->signature &= 0x00ffffff;
    font->signature |= 'T' << 24;

    RESETPTR(font->finf.tglp);
    TGLP_s* tglp = GETPTR(font->finf.tglp);
    RESETPTR(tglp->sheetData);

    RESETPTR(font->finf.cmap);
    CMAP_s* cmap = GETPTR(font->finf.cmap);
    while (cmap->next) {
        RESETPTR(cmap->next);
        cmap = GETPTR(cmap->next);
    }

    RESETPTR(font->finf.cwdh);
    CWDH_s* cwdh = GETPTR(font->finf.cwdh);
    while (cwdh->next) {
        RESETPTR(cwdh->next);
        cwdh = GETPTR(cwdh->next);
    }
}

void dump_shared_font() {
    fontEnsureMapped();

    CFNT_s* font = fontGetSystemFont();
    u32* hdr = (u32*) ((void*) font - 0x80);
    u32 fontsz = hdr[2];

    CFNT_s* fntcpy = malloc(fontsz);
    memcpy(fntcpy, font, fontsz);

    font_reset(fntcpy, font);

    FILE* fp = fopen("font.bcfnt", "wb");
    fwrite(fntcpy, fontsz, 1, fp);
    fclose(fp);

    free(fntcpy);
}

void dump_mii_data() {
    FS_Path apath = {.type = PATH_BINARY,
                     .size = 16,
                     .data = &(u32[4]) {0x00010202, 0x0004009b, 0, 0x20008}};
    FS_Path fpath = {.type = PATH_BINARY, .size = 20, .data = &(u32[5]) {}};

    FS_Archive ar;
    if (FSUSER_OpenArchive(&ar, ARCHIVE_SAVEDATA_AND_CONTENT, apath) < 0) {
        printf("open archive failed\n");
        return;
    }

    printf("archive: %016llx\n", ar);

    Handle fh;
    if (FSUSER_OpenFile(&fh, ar, fpath, 0, 0) < 0) {
        printf("open file failed\n");
        return;
    }

    u64 size;
    if (FSFILE_GetSize(fh, &size) < 0) {
        printf("get size failed\n");
        return;
    }

    printf("size = %llu\n", size);

    void* buf = malloc(size);

    u32 bread;
    if (FSFILE_Read(fh, &bread, 0, buf, size) < 0) {
        printf("read failed\n");
        return;
    }
    if (bread != size) {
        printf("didnt read enough\n");
        return;
    }

    FILE* fp = fopen("mii.app.romfs", "wb");
    fwrite(buf, size, 1, fp);
    fclose(fp);

    free(buf);
}

int main(int argc, char* argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("dumping shared font...\n");
    dump_shared_font();
    printf("done");

    printf("dumping mii data...\n");
    dump_mii_data();
    printf("done");

    // Main loop
    while (aptMainLoop()) {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break; // break in order to return to hbmenu
    }

    gfxExit();
    return 0;
}
