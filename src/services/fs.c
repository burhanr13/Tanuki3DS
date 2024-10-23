#include "fs.h"

#include <stdio.h>

#include "../3ds.h"
#include "../srv.h"

DECL_PORT(fs) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0801:
            linfo("Initialize");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0802:
            linfo("OpenFile");
            u64 archivehandle = cmdbuf[2] | (u64) cmdbuf[3] << 32;
            u32 pathtype = cmdbuf[4];
            u32 flags = cmdbuf[6];
            void* path = PTR(cmdbuf[9]);
            u32 handle = fs_open_file(s, archivehandle, pathtype, path, flags);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = handle;
            break;
        case 0x0803: {
            linfo("OpenFileDirectly");
            u32 archive = cmdbuf[2];
            u32 archivepathtype = cmdbuf[3];
            u32 filepathtype = cmdbuf[5];
            u32 flags = cmdbuf[7];
            char* archivepath = PTR(cmdbuf[10]);
            char* filepath = PTR(cmdbuf[12]);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            u64 ahandle =
                fs_open_archive(archive, archivepathtype, archivepath);
            u32 fhandle =
                fs_open_file(s, ahandle, filepathtype, filepath, flags);
            cmdbuf[3] = fhandle;
            break;
        }
        case 0x080c: {
            linfo("OpenArchive");
            u32 archiveid = cmdbuf[1];
            u32 pathtype = cmdbuf[2];
            void* path = PTR(cmdbuf[5]);
            u64 handle = fs_open_archive(archiveid, pathtype, path);
            cmdbuf[0] = IPCHDR(3, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = handle;
            cmdbuf[3] = handle >> 32;
            break;
        }
        case 0x080e: {
            linfo("CloseArchive");
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

DECL_PORT(fs_romfs) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0802: {
            u64 offset = cmdbuf[1];
            offset |= (u64) cmdbuf[2] << 32;
            u32 size = cmdbuf[3];
            void* data = PTR(cmdbuf[5]);

            linfo("reading at offset 0x%lx, size 0x%x", offset, size);

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            fseek(s->gamecard.fp, s->gamecard.romfs_off + 0x1000 + offset,
                  SEEK_SET);
            cmdbuf[2] = fread(data, 1, size, s->gamecard.fp);
            break;
        }
        case 0x0808: {
            linfo("close");
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

u64 fs_open_archive(u32 id, u32 pathtype, void* path) {
    switch (id) {
        case 3:
            switch (pathtype) {
                case FSPATH_EMPTY:
                    linfo("opening self ncch");
                    return 3;
                default:
                    lwarn("unknown path type");
                    return -1;
            }
        case 0x2345678a:
            return 0x2345678a;
        default:
            lwarn("unknown archive %x", id);
            return -1;
    }
}

u32 open_romfs(HLE3DS* s) {
    u32 h = handle_new(s);
    KSession* ses = session_create(port_handle_fs_romfs);
    HANDLE_SET(h, ses);
    ses->hdr.refcount = 1;
    linfo("opened romfs with handle %x", h);
    return h;
}

u32 fs_open_file(HLE3DS* s, u64 archive, u32 pathtype, void* rawpath,
                 u32 flags) {
    switch (archive) {
        case 3: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                switch (path[0]) {
                    case 0: {
                        return open_romfs(s);
                    }
                    default:
                        lerror("unknown selfNCCH file");
                        return -1;
                }
            } else {
                lerror("unknown selfNCCH file path type");
                return -1;
            }
            break;
        }
        case 0x2345678a: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                if (path[0] == 0 && path[2] == 0) {
                    return open_romfs(s);
                } else {
                    lwarn("unknown path for archive 0x2345678a");
                    return -1;
                }
            } else {
                lerror("unknown selfNCCH(0x234578a) file path type");
                return -1;
            }
            break;
        }
        default:
            lerror("unknown archive %x", archive);
            return -1;
    }
}