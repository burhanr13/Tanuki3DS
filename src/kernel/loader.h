#ifndef LOADER_H
#define LOADER_H

#include "common.h"

// elf format information copied from linux <elf.h>

typedef u16 Elf32_Half;
typedef u32 Elf32_Word;
typedef u32 Elf32_Addr;
typedef u32 Elf32_Off;

#define EI_NIDENT 16
#define PT_LOAD 1
#define PF_X BIT(0)
#define PF_W BIT(1)
#define PF_R BIT(2)

typedef struct {
    unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
    Elf32_Half e_type;                /* Object file type */
    Elf32_Half e_machine;             /* Architecture */
    Elf32_Word e_version;             /* Object file version */
    Elf32_Addr e_entry;               /* Entry point virtual address */
    Elf32_Off e_phoff;                /* Program header table file offset */
    Elf32_Off e_shoff;                /* Section header table file offset */
    Elf32_Word e_flags;               /* Processor-specific flags */
    Elf32_Half e_ehsize;              /* ELF header size in bytes */
    Elf32_Half e_phentsize;           /* Program header table entry size */
    Elf32_Half e_phnum;               /* Program header table entry count */
    Elf32_Half e_shentsize;           /* Section header table entry size */
    Elf32_Half e_shnum;               /* Section header table entry count */
    Elf32_Half e_shstrndx;            /* Section header string table index */
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;   /* Segment type */
    Elf32_Off p_offset;  /* Segment file offset */
    Elf32_Addr p_vaddr;  /* Segment virtual address */
    Elf32_Addr p_paddr;  /* Segment physical address */
    Elf32_Word p_filesz; /* Segment size in file */
    Elf32_Word p_memsz;  /* Segment size in memory */
    Elf32_Word p_flags;  /* Segment flags */
    Elf32_Word p_align;  /* Segment alignment */
} Elf32_Phdr;

typedef struct {
    u8 signature[0x100];
    char magic[4]; // NCSD
    u32 size;
    u64 media_id;
    u8 part_type[8];
    u8 part_crypt[8];
    struct {
        u32 offset;
        u32 size;
    } part[8];
} NCSDHeader;

typedef struct {
    u8 signature[0x100];
    char magic[4]; // NCCH
    u32 size;
    u64 part_id;
    u8 _110[0x90];
    struct {
        u32 offset;
        u32 size;
        u32 hdrsize;
        u32 res;
    } exefs, romfs;
    u8 exefs_hash[0x20];
    u8 romfs_hash[0x20];
} NCCHHeader;

typedef struct {
    struct {
        char apptitle[8];
        u8 res_008[5];
        union {
            u8 b;
            struct {
                u8 compressed : 1;
                u8 sdapp : 1;
            };
        } flags;
        u16 remasterversion;
        union {
            struct {
                struct {
                    u32 vaddr;
                    u32 pages;
                    u32 size;
                    u32 _pad;
                } text, rodata, data;
            };
            struct {
                u32 pad1[3];
                u32 stacksz;
                u32 pad2[7];
                u32 bss;
            };
        };
    } sci;
} ExHeader;

typedef struct {
    struct {
        char name[8];
        u32 offset;
        u32 size;
    } file[10];
} ExeFSHeader;

typedef struct {
    char magic[4]; // 3DSX
    u16 hdrSz;
    u16 relHdrSz;
    u32 version;
    u32 flags;
    u32 codeSz;
    u32 rodataSz;
    u32 dataBssSz;
    u32 bssSz;
    u32 smdhOff;
    u32 smdhSz;
    u32 romfsOff;
} _3DSXHeader;

typedef struct {
    u32 numAbsolute;
    u32 numRelative;
} _3DSXRelHeader;

typedef struct {
    u16 skip;
    u16 patch;
} _3DSXRelocation;

typedef struct {
    char magic[4]; // SMDH
    u16 version;
    u16 res1;
    struct {
        u16 shortname[64];
        u16 longname[128];
        u16 publisher[64];
    } titles[16];
    struct {
        u32 ratings[4];
        u32 regionLock;
        u32 matchMakerIDs[3];
        u32 flags;
        u16 eula;
        u16 res;
        u32 animDefaultFrame;
        u32 cecID;
    } settings;
    u32 res2[2];
    u8 icon[];
} SMDHFile;

typedef struct _3DS E3DS;

typedef struct {
    FILE* fp;
    u32 exheader_off;
    u32 exefs_off;
    u32 smdh_off;
    u32 romfs_off;

    char name[128];
} RomImage;

u32 load_elf(E3DS* s, char* filename);
u32 load_3dsx(E3DS* s, char* filename);
u32 load_ncsd(E3DS* s, char* filename);
u32 load_ncch(E3DS* s, char* filename, u64 offset);

void parse_smdh(E3DS* s);

u8* lzssrev_decompress(u8* in, u32 src_size, u32* dst_size);

#endif
