#include "font.h"

u8 shared_font_replacement[] = {
#embed "font.bcfnt"
};

void font_load(E3DS* s) {
    // try using a user provided font file
    FILE* fp = fopen("3ds/sys_files/font.bcfnt", "rb");
    void* custom_font = nullptr;
    void* font = shared_font_replacement;
    u32 fontsize = sizeof shared_font_replacement;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        fontsize = ftell(fp);
        rewind(fp);
        font = custom_font = malloc(fontsize);
        fread(custom_font, fontsize, 1, fp);
        fclose(fp);
    }

    s->services.apt.shared_font.size = 0x80 + fontsize;
    sharedmem_alloc(s, &s->services.apt.shared_font);
    // the shared font is treated as part of the linear heap
    // so its paddr and vaddr must be related appropriately
    s->services.apt.shared_font.mapaddr =
        s->services.apt.shared_font.paddr - FCRAM_PBASE + LINEAR_HEAP_BASE;
    u32* fontdest = PPTR(s->services.apt.shared_font.paddr);
    fontdest[0] = 2;        // 2 = font loaded
    fontdest[1] = 1;        // region
    fontdest[2] = fontsize; // size
    // font is at offset 0x80 of the memory block
    memcpy((void*) fontdest + 0x80, font, fontsize);
    // now we need to relocate the pointers in the font too (games will do it
    // themselves but homebrew needs it to be already done)
    font_relocate((void*) fontdest + 0x80,
                  s->services.apt.shared_font.mapaddr + 0x80);
    free(custom_font);
}

void font_relocate(void* buf, u32 vaddr) {
    CFNT_s* font = buf;

    font->signature[3] = 'U';

    TGLP_s* tglp = buf + font->finf.pTglp;
    font->finf.pTglp += vaddr;
    tglp->pSheetData += vaddr;

    CMAP_s* cmap = buf + font->finf.pCmap;
    font->finf.pCmap += vaddr;
    while (cmap->pNext) {
        CMAP_s* next = buf + cmap->pNext;
        cmap->pNext += vaddr;
        cmap = next;
    }

    CWDH_s* cwdh = buf + font->finf.pCwdh;
    font->finf.pCwdh += vaddr;
    while (cwdh->pNext) {
        CWDH_s* next = buf + cwdh->pNext;
        cwdh->pNext += vaddr;
        cwdh = next;
    }
}