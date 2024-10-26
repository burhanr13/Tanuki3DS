#ifndef FS_H
#define FS_H

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

typedef struct {
    FILE* files[FS_FILE_MAX];
    u32 priority;
} FSData;

DECL_PORT(fs);

DECL_PORT_ARG(fs_selfncch, base);

DECL_PORT_ARG(fs_file, fd);

u64 fs_open_archive(u32 id, u32 path_type, void* path);
KSession* fs_open_file(HLE3DS* s, u64 archive, u32 pathtype, void* rawpath,
                 u32 pathsize, u32 flags);

#endif
