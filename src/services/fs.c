#include "fs.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../3ds.h"
#include "../emulator_state.h"
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
            u32 pathsize = cmdbuf[5];
            u32 flags = cmdbuf[6];
            void* path = PTR(cmdbuf[9]);
            u32 handle =
                fs_open_file(s, archivehandle, pathtype, path, pathsize, flags);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = handle ? 0 : 0xC8804478;
            cmdbuf[3] = handle;
            break;
        case 0x0803: {
            linfo("OpenFileDirectly");
            u32 archive = cmdbuf[2];
            u32 archivepathtype = cmdbuf[3];
            u32 filepathtype = cmdbuf[5];
            u32 filepathsize = cmdbuf[6];
            u32 flags = cmdbuf[7];
            char* archivepath = PTR(cmdbuf[10]);
            char* filepath = PTR(cmdbuf[12]);
            cmdbuf[0] = IPCHDR(1, 2);
            u64 ahandle =
                fs_open_archive(archive, archivepathtype, archivepath);
            u32 fhandle = fs_open_file(s, ahandle, filepathtype, filepath,
                                       filepathsize, flags);
            cmdbuf[1] = fhandle ? 0 : -1;
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
            cmdbuf[1] = handle == -1 ? -1 : 0;
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
        case 0x0861: {
            linfo("InitializeWithSDKVersion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0862: {
            linfo("SetPriority");
            s->services.fs.priority = cmdbuf[1];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0863: {
            linfo("GetPriority");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.fs.priority;
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

DECL_PORT_ARG(fs_file, fd) {
    u32* cmdbuf = PTR(cmd_addr);

    FILE* fp = s->services.fs.files[fd];

    if (!fp) {
        cmdbuf[0] = IPCHDR(1, 0);
        cmdbuf[1] = -1;
        return;
    }

    linfo("fd is %d", fd);

    switch (cmd.command) {
        case 0x0802: {
            u64 offset = cmdbuf[1];
            offset |= (u64) cmdbuf[2] << 32;
            u32 size = cmdbuf[3];
            void* data = PTR(cmdbuf[5]);

            linfo("reading at offset 0x%lx, size 0x%x", offset, size);

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            fseek(fp, offset, SEEK_SET);
            cmdbuf[2] = fread(data, 1, size, fp);
            break;
        }
        case 0x0803: {
            u64 offset = cmdbuf[1];
            offset |= (u64) cmdbuf[2] << 32;
            u32 size = cmdbuf[3];
            void* data = PTR(cmdbuf[6]);

            linfo("writing at offset 0x%lx, size 0x%x", offset, size);

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            fseek(fp, offset, SEEK_SET);
            cmdbuf[2] = fwrite(data, 1, size, fp);
            break;
        }
        case 0x0804: {
            linfo("GetSize");
            fseek(fp, 0, SEEK_END);
            long len = ftell(fp);
            cmdbuf[0] = IPCHDR(3, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = len;
            cmdbuf[3] = len >> 32;
            break;
        }
        case 0x0808: {
            linfo("closing file");
            fclose(fp);
            s->services.fs.files[fd] = NULL;
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

char* archive_basepath(u64 archive) {
    switch (archive << 32 >> 32) {
        case 4: {
            char* basepath;
            asprintf(&basepath, "system/savedata/%s", ctremu.romfilenoext);
            return basepath;
        }
        case 7: {
            char* basepath;
            asprintf(&basepath, "system/extdata/%08x", archive >> 32);
            return basepath;
        }
    }
    return NULL;
}

u64 fs_open_archive(u32 id, u32 pathtype, void* path) {
    switch (id) {
        case 3:
            if (pathtype == FSPATH_EMPTY) {
                linfo("opening self ncch");
                return 3;
            } else {
                lwarn("unknown path type");
                return -1;
            }
            break;
        case 4: {
            if (pathtype == FSPATH_EMPTY) {
                linfo("opening save data");
                char* apath = archive_basepath(4);
                mkdir(apath, S_IRWXU);
                free(apath);
                return 4;
            } else {
                lwarn("unknown path type");
                return -1;
            }
        }
        case 7: {
            if (pathtype == FSPATH_BINARY) {
                u32* lowpath = path;
                linfo("opening extdata %08x", lowpath[1]);
                u64 aid = 7 | (u64) lowpath[1] << 32;
                char* apath = archive_basepath(aid);
                mkdir(apath, S_IRWXU);
                free(apath);
                return aid;
            } else {
                lerror("unknown file path type");
                return -1;
            }
            break;
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
                 u32 pathsize, u32 flags) {
    switch (archive << 32 >> 32) {
        case 3: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                switch (path[0]) {
                    case 0: {
                        return open_romfs(s);
                    }
                    default:
                        lerror("unknown selfNCCH file");
                        return 0;
                }
            } else {
                lerror("unknown selfNCCH file path type");
                return 0;
            }
            break;
        }
        case 4:
        case 7: {
            char* basepath = archive_basepath(archive);

            char* filepath = NULL;
            if (pathtype == FSPATH_ASCII) {
                linfo("archive %lx, opening file %s", archive, rawpath);
                asprintf(&filepath, "%s%s", basepath, rawpath);
            } else if (pathtype == FSPATH_UTF16) {
                u16* path16 = rawpath;
                u8 path[pathsize];
                for (int i = 0; i < pathsize / 2; i++) {
                    path[i] = path16[i];
                }
                linfo("archive %lx, opening file %s", archive, path);
                asprintf(&filepath, "%s%s", basepath, path);
            } else {
                lerror("unknown file path type");
                return 0;
            }
            int fd = -1;
            for (int i = 0; i < FS_FILE_MAX; i++) {
                if (s->services.fs.files[i] == NULL) {
                    fd = i;
                    break;
                }
            }
            if (fd == -1) {
                lerror("ran out of files");
                return 0;
            }

            int mode = 0;
            switch (flags & 3) {
                case 0b01:
                    mode = O_RDONLY;
                    break;
                case 0b10:
                    mode = O_WRONLY;
                    break;
                case 0b11:
                    mode = O_RDWR;
                    break;
            }
            if (flags & BIT(2)) mode |= O_CREAT;

            int hostfd = open(filepath, mode, S_IRUSR | S_IWUSR);
            if (hostfd < 0) {
                linfo("opening file which does not exist");
                return 0;
            }

            char* fopenmode = "r";
            switch (flags & 3) {
                case 0b01:
                    fopenmode = "r";
                    break;
                case 0b10:
                    fopenmode = "w";
                    break;
                case 0b11:
                    fopenmode = "r+";
                    break;
            }

            FILE* fp = fdopen(hostfd, fopenmode);
            if (!fp) {
                lerror("failed to open file %s", filepath);
                perror("fdopen");
                return 0;
            }
            s->services.fs.files[fd] = fp;

            u32 h = handle_new(s);
            KSession* ses = session_create_arg(port_handle_fs_file, fd);
            HANDLE_SET(h, ses);
            ses->hdr.refcount = 1;
            linfo("opened file %s with fd %d and handle %x", filepath, fd, h);

            free(filepath);
            free(basepath);

            return h;
            break;
        }
        case 0x2345678a: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                if (path[0] == 0 && path[2] == 0) {
                    return open_romfs(s);
                } else {
                    lwarn("unknown path for archive 0x2345678a");
                    return 0;
                }
            } else {
                lerror("unknown selfNCCH(0x234578a) file path type");
                return 0;
            }
            break;
        }
        default:
            lerror("unknown archive %llx", archive);
            return 0;
    }
}