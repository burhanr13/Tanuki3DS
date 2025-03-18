#include "font.h"

void font_relocate(void* buf, u32 vaddr) {
    CFNT_s* font = buf;

    font->signature[3] = 'U';

    TGLP_s* tglp = buf + font->finf.pTglp;
    font->finf.pTglp += vaddr;
    tglp->pSheetData += vaddr;

    CMAP_s* cmap = buf + font->finf.pCmap;
    font->finf.pCmap += vaddr;
    while (true) {
        if (!cmap->pNext) break;
        CMAP_s* next = buf + cmap->pNext;
        cmap->pNext += vaddr;
        cmap = next;
    }

    CWDH_s* cwdh = buf + font->finf.pCwdh;
    font->finf.pCwdh += vaddr;
    while (true) {
        if (!cwdh->pNext) break;
        CWDH_s* next = buf + cwdh->pNext;
        cwdh->pNext += vaddr;
        cwdh = next;
    }
}