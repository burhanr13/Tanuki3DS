#include "ldr.h"

#include "../3ds.h"
#include "../arm/jit/jit.h"
#include "../memory.h"

DECL_PORT(ldr_ro) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            // if this game uses cros it doesn't get to optimize literals
            // anymore
            g_jit_opt_literals = false;

            u32 crssrc = cmdbuf[1];
            u32 size = cmdbuf[2];
            u32 crsdst = cmdbuf[3];
            linfo("Initialize with crs=%08x dst=%08x sz=%x", crssrc, crsdst,
                  size);

            memory_virtmirror(s, crssrc, crsdst, size, PERM_R);
            s->services.ldr.crs_addr = crsdst;
            cro_relocate(s, crsdst);

            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0002:
            linfo("LoadCRR");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0004:
        case 0x0009: // same as 4?
        {
            u32 srcaddr = cmdbuf[1];
            u32 dstaddr = cmdbuf[2];
            u32 size = cmdbuf[3];
            u32 dataaddr = cmdbuf[4];
            u32 datasize = cmdbuf[6];
            u32 bssaddr = cmdbuf[7];
            u32 bsssize = cmdbuf[8];
            bool autolink = cmdbuf[9];
            linfo("LoadCRO with src=%08x dst=%08x size=%x data=%08x,sz=%x "
                  "bss=%08x,sz=%x autolink=%d",
                  srcaddr, dstaddr, size, dataaddr, datasize, bssaddr, bsssize,
                  autolink);

            memory_virtmirror(s, srcaddr, dstaddr, size, PERM_RX);

            ldr_load_cro(s, dstaddr, dataaddr, bssaddr, autolink);

            jit_invalidate_range(&s->cpu, dstaddr, size);

            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0005: {
            u32 addr = cmdbuf[1];
            linfo("UnloadCRO at %08x", addr);
            ldr_unload_cro(s, addr);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void cro_relocate(E3DS* s, u32 vaddr) {
    CROHeader* hdr = PTR(vaddr);

    hdr->name_addr += vaddr;
#define REL(m) hdr->m.addr += vaddr
    REL(code);
    REL(data);
    REL(modulename);
    REL(segmenttable);
    REL(exports.named_symbols);
    REL(exports.indexed_symbols);
    REL(exports.strings);
    REL(exports.nametree);
    REL(import_modules);
    REL(external_patches);
    REL(imports.named_symbols);
    REL(imports.indexed_symbols);
    REL(imports.anon_symbols);
    REL(imports.strings);
    REL(static_anon_symbols);
    REL(internal_patches);
    REL(static_anon_patches);
#undef REL

    CRONamedExport* namedexps = PTR(hdr->exports.named_symbols.addr);
    for (int i = 0; i < hdr->exports.named_symbols.size; i++) {
        namedexps[i].name_addr += vaddr;
    }
    CRONamedImport* namedimps = PTR(hdr->imports.named_symbols.addr);
    for (int i = 0; i < hdr->imports.named_symbols.size; i++) {
        namedimps[i].name_addr += vaddr;
        namedimps[i].patches_addr += vaddr;
    }
    CROIndexedImport* indeximps = PTR(hdr->imports.indexed_symbols.addr);
    for (int i = 0; i < hdr->imports.indexed_symbols.size; i++) {
        indeximps[i].patches_addr += vaddr;
    }
    CROAnonImport* anonimps = PTR(hdr->imports.anon_symbols.addr);
    for (int i = 0; i < hdr->imports.anon_symbols.size; i++) {
        anonimps[i].patches_addr += vaddr;
    }
    CROImportModule* mods = PTR(hdr->import_modules.addr);
    for (int i = 0; i < hdr->import_modules.size; i++) {
        mods[i].name_addr += vaddr;
        mods[i].anon.addr += vaddr;
        mods[i].indexed.addr += vaddr;
    }
}

#define SEGTAGADDR(segs, loc) (segs[loc.id].addr + loc.offset)

#define PATCH(rel, sym, segs)                                                  \
    ({                                                                         \
        u32 addr = SEGTAGADDR(segs, rel.loc);                                  \
        linfo("patching %08x with symbol at %08x type=%x addend=%x", addr,     \
              sym, rel.type, rel.addend);                                      \
        switch (rel.type) {                                                    \
            case 0:                                                            \
                break;                                                         \
            case 2:                                                            \
                *(u32*) PTR(addr) = sym + rel.addend;                          \
                break;                                                         \
            case 3:                                                            \
                *(u32*) PTR(addr) = sym + rel.addend - addr;                   \
                break;                                                         \
            default:                                                           \
                lerror("unknown patch type %d", rel.type);                     \
                break;                                                         \
        }                                                                      \
    })

#define IMPORTPATCHLIST(rels, symloc, srcsegs, dstsegs)                        \
    ({                                                                         \
        int j = 0;                                                             \
        do {                                                                   \
            PATCH(rels[j], SEGTAGADDR(srcsegs, symloc), dstsegs);              \
            rels[j].loaded = 1;                                                \
        } while (!rels[j++].end);                                              \
    })

void import_symbols(E3DS* s, u32 srcaddr, u32 dstaddr,
                    CROImportModule* imports) {
    CROHeader* src = PTR(srcaddr);
    CROHeader* dst = PTR(dstaddr);

    linfo("importing symbols from %s to %s", PTR(src->name_addr),
          PTR(dst->name_addr));

    CROSegment* srcsegs = PTR(src->segmenttable.addr);
    CROSegment* dstsegs = PTR(dst->segmenttable.addr);

    CROIndexedExport* indexedexp = PTR(src->exports.indexed_symbols.addr);
    CROIndexedImport* indexed = PTR(imports->indexed.addr);
    for (int i = 0; i < imports->indexed.size; i++) {
        SegmentTag loc = indexedexp[indexed[i].index].loc;
        CROImportPatch* rels = PTR(indexed[i].patches_addr);
        IMPORTPATCHLIST(rels, loc, srcsegs, dstsegs);
    }

    CROAnonImport* anon = PTR(imports->anon.addr);
    for (int i = 0; i < imports->anon.size; i++) {
        SegmentTag loc = anon[i].loc;
        CROImportPatch* rels = PTR(anon[i].patches_addr);
        IMPORTPATCHLIST(rels, loc, srcsegs, dstsegs);
    }
}

SegmentTag search_named_symbol(E3DS* s, u32 croaddr, char* name) {
    CROHeader* hdr = PTR(croaddr);

    CRONamedExport* syms = PTR(hdr->exports.named_symbols.addr);
    for (int i = 0; i < hdr->exports.named_symbols.size; i++) {
        if (!strcmp(PTR(syms[i].name_addr), name)) {
            return syms[i].loc;
        }
    }
    return (SegmentTag) {.raw = -1};
}

void import_named_symbols(E3DS* s, u32 srcaddr, u32 dstaddr) {
    CROHeader* src = PTR(srcaddr);
    CROHeader* dst = PTR(dstaddr);

    linfo("importing named symbols from %s to %s", PTR(src->name_addr),
          PTR(dst->name_addr));

    CROSegment* srcsegs = PTR(src->segmenttable.addr);
    CROSegment* dstsegs = PTR(dst->segmenttable.addr);

    CRONamedImport* named = PTR(dst->imports.named_symbols.addr);
    for (int i = 0; i < dst->imports.named_symbols.size; i++) {
        char* name = PTR(named[i].name_addr);
        SegmentTag loc;
        if (strcmp(name, "__aeabi_atexit")) {
            loc = search_named_symbol(s, srcaddr, name);
        } else {
            loc = search_named_symbol(s, srcaddr, "nnroAeabiAtexit_");
        }
        if (loc.raw == -1) continue;

        linfo("symbol %s", name);
        CROImportPatch* rels = PTR(named[i].patches_addr);
        IMPORTPATCHLIST(rels, loc, srcsegs, dstsegs);
    }
}

void ldr_load_cro(E3DS* s, u32 vaddr, u32 data, u32 bss, bool autolink) {
    CROHeader* crs = PTR(s->services.ldr.crs_addr);

    CROHeader* hdr = PTR(vaddr);

    cro_relocate(s, vaddr);

    char* name = PTR(hdr->name_addr);
    linfo("loading cro %s", name);

    CROSegment* segs = PTR(hdr->segmenttable.addr);
    // when patching symbols in data it needs to be done in
    // the cro itself not in the external data buffer
    CROSegment patchsegs[4];

    for (int i = 0; i < 4; i++) {
        if (segs[i].id != i) {
            break;
        }

        segs[i].addr += vaddr;
        patchsegs[i] = segs[i];

        // set data and bss to point to user buffers
        if (segs[i].id == CROSEG_BSS) {
            segs[i].addr = bss;
        } else if (segs[i].id == CROSEG_DATA) {
            segs[i].addr = data;
        }

        linfo("segment %d (addr=%08x,size=%x)", segs[i].id, segs[i].addr,
              segs[i].size);
    }

    // relocations
    linfo("applying internal relocations");
    CROInternalPatch* rels = PTR(hdr->internal_patches.addr);
    for (int i = 0; i < hdr->internal_patches.size; i++) {
        u32 base = segs[rels[i].segid].addr;
        PATCH(rels[i], base, patchsegs);
    }

    // exports
    u32 cur = s->services.ldr.crs_addr;
    while (cur) {
        CROHeader* curhdr = PTR(cur);

        CROImportModule* mod = nullptr;
        CROImportModule* modtab = PTR(curhdr->import_modules.addr);
        for (int i = 0; i < curhdr->import_modules.size; i++) {
            if (!strcmp(name, PTR(modtab[i].name_addr))) {
                mod = &modtab[i];
                break;
            }
        }
        if (!mod) {
            cur = curhdr->next;
            continue;
        }

        import_symbols(s, vaddr, cur, mod);

        cur = curhdr->next;
    }

    // imports
    CROImportModule* modtab = PTR(hdr->import_modules.addr);
    for (int i = 0; i < hdr->import_modules.size; i++) {
        char* importname = PTR(modtab[i].name_addr);
        u32 cur = s->services.ldr.crs_addr;
        while (cur) {
            CROHeader* curhdr = PTR(cur);
            if (!strcmp(importname, PTR(curhdr->name_addr))) {
                break;
            }

            cur = curhdr->next;
        }
        if (!cur) continue;

        import_symbols(s, cur, vaddr, &modtab[i]);
    }

    // named symbols (this is going to be really inefficient)
    cur = s->services.ldr.crs_addr;
    while (cur) {
        CROHeader* curhdr = PTR(cur);

        import_named_symbols(s, cur, vaddr);
        import_named_symbols(s, vaddr, cur);

        cur = curhdr->next;
    }

    // insert into the link list
    // crs->next is the auto linked modules list
    // and crs->prev is the manual linked modules list
    // crs->*->prev is the tail pointer of that list
    // the tail has next = null
    // otherwise next/prev are like a normal DLL
    u32* llhd = autolink ? &crs->next : &crs->prev;
    if (*llhd) {
        CROHeader* hd = PTR(*llhd);
        CROHeader* tl = PTR(hd->prev);
        tl->next = vaddr;
        hdr->prev = hd->prev;
        hd->prev = vaddr;
    } else {
        *llhd = vaddr;
        hdr->prev = vaddr;
    }
}

void ldr_unload_cro(E3DS* s, u32 vaddr) {
    CROHeader* crs = PTR(s->services.ldr.crs_addr);

    CROHeader* hdr = PTR(vaddr);

    linfo("unloading cro %s", PTR(hdr->name_addr));

    // remove from link list
    // see the diagram in the cro doc (this is so dumb)
    if (hdr->next) {
        CROHeader* nxt = PTR(hdr->next);
        nxt->prev = hdr->prev;
    } else {
        u32 cur = vaddr;
        // find the head of the list which contains the tail pointer
        while (cur != crs->next && cur != crs->prev) {
            CROHeader* curhdr = PTR(cur);
            cur = curhdr->prev;
        }
        CROHeader* hd = PTR(cur);
        hd->prev = hdr->prev;
    }
    if (crs->next == vaddr) {
        crs->next = hdr->next;
    } else if (crs->prev == vaddr) {
        crs->prev = hdr->next;
    } else {
        CROHeader* prv = PTR(hdr->prev);
        prv->next = hdr->next;
    }

    hdr->prev = 0;
    hdr->next = 0;

    CROSegment* segs = PTR(hdr->segmenttable.addr);

    u32 onunresolved = SEGTAGADDR(segs, hdr->onunresolved);

    // reset patches
    CROImportPatch* extrels = PTR(hdr->external_patches.addr);
    for (int i = 0; i < hdr->external_patches.size; i++) {
        *(u32*) PTR(SEGTAGADDR(segs, extrels[i].loc)) = 0;
        extrels[i].loaded = 0;
    }
    CROInternalPatch* intrels = PTR(hdr->internal_patches.addr);
    for (int i = 0; i < hdr->internal_patches.size; i++) {
        *(u32*) PTR(SEGTAGADDR(segs, intrels[i].loc)) = 0;
    }

    // reset segment table
    for (int i = 0; i < 4; i++) {
        if (segs[i].id != i) {
            break;
        }
        if (segs[i].id == CROSEG_BSS) {
            segs[i].addr = 0;
        } else if (segs[i].id == CROSEG_DATA) {
            segs[i].addr = hdr->data.addr - vaddr;
        } else {
            segs[i].addr -= vaddr;
        }
    }

    // undo rebasing
    CRONamedExport* namedexps = PTR(hdr->exports.named_symbols.addr);
    for (int i = 0; i < hdr->exports.named_symbols.size; i++) {
        namedexps[i].name_addr -= vaddr;
    }
    CRONamedImport* namedimps = PTR(hdr->imports.named_symbols.addr);
    for (int i = 0; i < hdr->imports.named_symbols.size; i++) {
        namedimps[i].name_addr -= vaddr;
        namedimps[i].patches_addr -= vaddr;
    }
    CROIndexedImport* indeximps = PTR(hdr->imports.indexed_symbols.addr);
    for (int i = 0; i < hdr->imports.indexed_symbols.size; i++) {
        indeximps[i].patches_addr -= vaddr;
    }
    CROAnonImport* anonimps = PTR(hdr->imports.anon_symbols.addr);
    for (int i = 0; i < hdr->imports.anon_symbols.size; i++) {
        anonimps[i].patches_addr -= vaddr;
    }
    CROImportModule* mods = PTR(hdr->import_modules.addr);
    for (int i = 0; i < hdr->import_modules.size; i++) {
        mods[i].name_addr -= vaddr;
        mods[i].anon.addr -= vaddr;
        mods[i].indexed.addr -= vaddr;
    }

    hdr->name_addr -= vaddr;
#define REL(m) hdr->m.addr -= vaddr
    REL(code);
    REL(data);
    REL(modulename);
    REL(segmenttable);
    REL(exports.named_symbols);
    REL(exports.indexed_symbols);
    REL(exports.strings);
    REL(exports.nametree);
    REL(import_modules);
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