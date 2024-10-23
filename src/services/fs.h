#ifndef FS_H
#define FS_H

#include "../srv.h"
#include "../svc_types.h"

enum {
    FSPATH_INVALID,
    FSPATH_EMPTY,
    FSPATH_BINARY,
    FSPATH_ASCII,
    FSPATH_UTF16
};

DECL_PORT(fs);

DECL_PORT(fs_romfs);

u64 fs_open_archive(u32 id, u32 path_type, void* path);
u32 fs_open_file(HLE3DS* s, u64 archive, u32 pathtype, void* rawpath,
                 u32 flags);

#endif
