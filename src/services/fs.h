#ifndef FS_H
#define FS_H

#include <dirent.h>
#include <stdio.h>

#include "srv.h"

#define FS_FILE_MAX 32

enum {
    ARCHIVE_SELFNCCH = 3,
    ARCHIVE_SAVEDATA = 4,
    ARCHIVE_EXTSAVEDATA = 6,
    ARCHIVE_SHAREDEXTDATA = 7,
    ARCHIVE_SYSTEMSAVEDATA = 8,
    ARCHIVE_SDMC = 9,
};

enum {
    FSPATH_INVALID,
    FSPATH_EMPTY,
    FSPATH_BINARY,
    FSPATH_ASCII,
    FSPATH_UTF16
};

// games actually care about which filesystem errors we return
enum {
    FSERR_CREATE = 0xc82044Be,
    FSERR_OPEN = 0xc8804478,
    FSERR_ARCHIVE = 0xc8f04560,
};

typedef struct {
    u16 name[0x106];
    u8 shortname[10];
    u8 shortext[4];
    u8 _21a[2];
    u8 isdir;
    u8 ishidden;
    u8 isarchive;
    u8 isreadonly;
    u64 size;
} FSDirent;

typedef struct {
    FILE* files[FS_FILE_MAX];
    DIR* dirs[FS_FILE_MAX];
#ifdef _WIN32
    char* dirpaths[FS_FILE_MAX];
#endif

    u32 priority;
} FSData;

DECL_PORT(fs);

DECL_PORT_ARG(fs_selfncch, base);

DECL_PORT_ARG(fs_file, fd);
DECL_PORT_ARG(fs_dir, fd);

u64 fs_open_archive(E3DS* s, u32 id, u32 path_type, void* path);
KSession* fs_open_file(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                       u32 pathsize, u32 flags);
bool fs_create_file(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                    u32 pathsize, u32 flags, u64 filesize);
bool fs_delete_file(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                    u32 pathsize);
KSession* fs_open_dir(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                      u32 pathsize);
bool fs_create_dir(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                   u32 pathsize);

void fs_close_all_files(E3DS* s);

#endif
