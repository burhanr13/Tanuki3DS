#ifndef FS_H
#define FS_H

#include <dirent.h>
#include <stdio.h>

#include "../srv.h"

#define FS_FILE_MAX 32

enum {
    FSPATH_INVALID,
    FSPATH_EMPTY,
    FSPATH_BINARY,
    FSPATH_ASCII,
    FSPATH_UTF16
};

enum {
    FSERR_CREATE = 0xC82044BE,
    FSERR_OPEN = 0xC8804471,
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

    u32 priority;
} FSData;

DECL_PORT(fs);

DECL_PORT_ARG(fs_selfncch, base);

DECL_PORT_ARG(fs_file, fd);
DECL_PORT_ARG(fs_dir, fd);

u64 fs_open_archive(u32 id, u32 path_type, void* path);
KSession* fs_open_file(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                       u32 pathsize, u32 flags);
bool fs_create_file(u64 archive, u32 pathtype, void* rawpath, u32 pathsize,
                    u32 flags, u64 filesize);
KSession* fs_open_dir(E3DS* s, u64 archive, u32 pathtype, void* rawpath,
                      u32 pathsize);
bool fs_create_dir(u64 archive, u32 pathtype, void* rawpath, u32 pathsize);

#endif
