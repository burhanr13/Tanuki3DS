#include "fs.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../3ds.h"
#include "../emulator_state.h"
#include "../filesystems.h"
#include "../srv.h"

#include "../sys_files/mii.app.romfs.h"

enum {
    SYSFILE_MIIDATA = 1,
};

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
        case 9: {
            char* basepath;
            asprintf(&basepath, "system/sdmc");
            return basepath;
        }
        default:
            lerror("invalid archive");
            return NULL;
    }
}

char* create_text_path(u64 archive, u32 pathtype, void* rawpath, u32 pathsize) {
    char* basepath = archive_basepath(archive);
    if (!basepath) return NULL;

    char* filepath = NULL;
    if (pathtype == FSPATH_ASCII) {
        asprintf(&filepath, "%s%s", basepath, rawpath);
    } else if (pathtype == FSPATH_UTF16) {
        u16* path16 = rawpath;
        u8 path[pathsize];
        for (int i = 0; i < pathsize / 2; i++) {
            path[i] = path16[i];
        }
        asprintf(&filepath, "%s%s", basepath, path);
    } else {
        lerror("unknown text file path type");
        return NULL;
    }
    free(basepath);
    return filepath;
}

DECL_PORT(fs) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0801:
            linfo("Initialize");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0802: {
            linfo("OpenFile");
            u64 archivehandle = cmdbuf[2] | (u64) cmdbuf[3] << 32;
            u32 pathtype = cmdbuf[4];
            u32 pathsize = cmdbuf[5];
            u32 flags = cmdbuf[6];
            void* path = PTR(cmdbuf[9]);

            cmdbuf[0] = IPCHDR(1, 2);

            u32 h = handle_new(s);
            if (!h) {
                cmdbuf[1] = -1;
                return;
            }
            KSession* ses =
                fs_open_file(s, archivehandle, pathtype, path, pathsize, flags);
            if (!ses) {
                cmdbuf[1] = FSERR_OPEN;
                return;
            }
            HANDLE_SET(h, ses);
            ses->hdr.refcount = 1;
            linfo("opened file with handle %x", h);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = h;
            break;
        }
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
            u32 h = handle_new(s);
            if (!h) {
                cmdbuf[1] = -1;
                return;
            }
            KSession* ses = fs_open_file(s, ahandle, filepathtype, filepath,
                                         filepathsize, flags);
            if (!ses) {
                cmdbuf[1] = FSERR_OPEN;
                return;
            }
            HANDLE_SET(h, ses);
            ses->hdr.refcount = 1;
            linfo("opened file with handle %x", h);
            cmdbuf[1] = 0;
            cmdbuf[3] = h;
            break;
        }
        case 0x0808: {
            linfo("CreateFile");
            u64 archivehandle = cmdbuf[2] | (u64) cmdbuf[3] << 32;
            u32 pathtype = cmdbuf[4];
            u32 pathsize = cmdbuf[5];
            u32 flags = cmdbuf[6];
            u64 filesize = cmdbuf[7] | (u64) cmdbuf[8] << 32;
            void* path = PTR(cmdbuf[10]);

            cmdbuf[0] = IPCHDR(1, 0);
            if (fs_create_file(archivehandle, pathtype, path, pathsize, flags,
                               filesize)) {
                cmdbuf[1] = 0;
            } else {
                cmdbuf[1] = FSERR_CREATE;
            }
            break;
        }
        case 0x0809: {
            linfo("CreateDirectory");
            u64 archivehandle = cmdbuf[2] | (u64) cmdbuf[3] << 32;
            u32 pathtype = cmdbuf[4];
            u32 pathsize = cmdbuf[5];
            void* path = PTR(cmdbuf[8]);

            cmdbuf[0] = IPCHDR(1, 0);
            if (fs_create_dir(archivehandle, pathtype, path, pathsize)) {
                cmdbuf[1] = 0;
            } else {
                cmdbuf[1] = FSERR_CREATE;
            }
            break;
        }
        case 0x080b: {
            linfo("OpenDirectory");
            u64 archivehandle = cmdbuf[1] | (u64) cmdbuf[2] << 32;
            u32 pathtype = cmdbuf[3];
            u32 pathsize = cmdbuf[4];
            void* path = PTR(cmdbuf[6]);

            cmdbuf[0] = IPCHDR(1, 2);

            u32 h = handle_new(s);
            if (!h) {
                cmdbuf[1] = -1;
                return;
            }
            KSession* ses =
                fs_open_dir(s, archivehandle, pathtype, path, pathsize);
            if (!ses) {
                cmdbuf[1] = FSERR_OPEN;
                return;
            }
            HANDLE_SET(h, ses);
            ses->hdr.refcount = 1;
            linfo("opened dir with handle %x", h);
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = h;
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
        case 0x0817:
            linfo("IsSdmcDetected");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            break;
        case 0x0818:
            linfo("IsSdmcWritable");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = true;
            break;
        case 0x845: {
            u32 archive = cmdbuf[1];
            u32 pathtype = cmdbuf[2];
            void* path = PTR(cmdbuf[5]);

            char* basepath =
                archive_basepath(fs_open_archive(archive, pathtype, path));

            linfo("GetFormatInfo for %s", basepath);

            cmdbuf[0] = IPCHDR(5, 0);
            if (!basepath) {
                lerror("unknown archive %x", archive);
                cmdbuf[1] = -1;
                return;
            }
            free(basepath);
            cmdbuf[2] = 0;
            cmdbuf[3] = 0;
            cmdbuf[4] = 0;
            cmdbuf[5] = 0;
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

DECL_PORT_ARG(fs_selfncch, base) {
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
            fseek(s->gamecard.fp, base + offset, SEEK_SET);

            cmdbuf[2] = fread(data, 1, size, s->gamecard.fp);
            break;
        }
        case 0x0808: {
            linfo("close");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x080c: {
            linfo("OpenLinkFile");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            u32 h = handle_new(s);
            if (!h) {
                cmdbuf[1] = -1;
                return;
            }
            KSession* ses = session_create_arg(port_handle_fs_selfncch, base);
            HANDLE_SET(h, ses);
            ses->hdr.refcount = 1;
            linfo("opened link file with handle %x", h);
            cmdbuf[3] = h;
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

DECL_PORT_ARG(fs_sysfile, file) {
    u32* cmdbuf = PTR(cmd_addr);

    void* srcdata = NULL;
    u64 srcsize = 0;
    switch (file) {
        case SYSFILE_MIIDATA:
            srcdata = MII_DATA;
            srcsize = MII_DATA_len;
            linfo("accessing mii data");
            break;
        default:
            lerror("unknown system file %x", file);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = -1;
            return;
    }

    switch (cmd.command) {
        case 0x0802: {
            u64 offset = cmdbuf[1];
            offset |= (u64) cmdbuf[2] << 32;
            u32 dstsize = cmdbuf[3];
            void* dstdata = PTR(cmdbuf[5]);

            linfo("reading at offset 0x%lx, size 0x%x", offset, dstsize);

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;

            if (offset > srcsize) {
                cmdbuf[2] = 0;
            } else {
                if (offset + dstsize > srcsize) {
                    dstsize = srcsize - offset;
                }
                cmdbuf[2] = dstsize;
                memcpy(dstdata, srcdata + offset, dstsize);
            }

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
        lerror("invalid fd");
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

DECL_PORT_ARG(fs_dir, fd) {
    u32* cmdbuf = PTR(cmd_addr);

    DIR* dp = s->services.fs.dirs[fd];

    if (!dp) {
        lerror("invalid fd");
        cmdbuf[0] = IPCHDR(1, 0);
        cmdbuf[1] = -1;
        return;
    }

    linfo("fd is %d", fd);

    switch (cmd.command) {
        case 0x0801: {
            u32 count = cmdbuf[1];
            FSDirent* ents = PTR(cmdbuf[3]);

            linfo("reading %d ents", count);

            struct dirent* ent;
            int i = 0;
            for (; i < count; i++) {
                ent = readdir(dp);
                if (!ent) break;

                int namelen = strlen(ent->d_name);
                if (namelen > 0x105) namelen = 0x105;
                for (int j = 0; j < namelen; j++) {
                    ents[i].name[j] = ent->d_name[j];
                }
                ents[i]._21a[0] = 1;

                struct stat st;
                fstatat(dirfd(dp), ent->d_name, &st, 0);

                ents[i].size = st.st_size;
            }

            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = i;
            break;
        }
        case 0x0802: {
            linfo("closing dir");
            closedir(dp);
            s->services.fs.dirs[fd] = NULL;
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
        case 6:
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
        case 9: {
            if (pathtype == FSPATH_EMPTY) {
                linfo("opening sd card");
                char* apath = archive_basepath(9);
                free(apath);
                return 9;
            } else {
                lwarn("unknown path type");
                return -1;
            }
        }
        case 0x2345678a:
            if (pathtype == FSPATH_BINARY) {
                u64* lowpath = path;
                switch (*lowpath) {
                    case 0x0004009b00010202:
                        linfo("opening mii data archive");
                        return (u64) 0x2345678a | (u64) SYSFILE_MIIDATA << 32;
                    default:
                        lwarn("unknown ncch archive %lx", *lowpath);
                        return -1;
                }
            } else {
                lwarn("unknown path type");
                return -1;
            }
        case 0x567890b4: {
            if (pathtype == FSPATH_BINARY) {
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
        default:
            lwarn("unknown archive %x", id);
            return -1;
    }
}

KSession* fs_open_file(HLE3DS* s, u64 archive, u32 pathtype, void* rawpath,
                       u32 pathsize, u32 flags) {
    switch (archive << 32 >> 32) {
        case 3: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                switch (path[0]) {
                    case 0: {
                        linfo("opening romfs");
                        return session_create_arg(port_handle_fs_selfncch,
                                                  s->gamecard.romfs_off);
                    }
                    case 2: {
                        char* filename = (char*) &path[1];
                        ExeFSHeader hdr;
                        fseek(s->gamecard.fp, s->gamecard.exefs_off, SEEK_SET);
                        fread(&hdr, sizeof hdr, 1, s->gamecard.fp);
                        u32 offset = 0;
                        for (int i = 0; i < 10; i++) {
                            if (!strcmp(hdr.file[i].name, filename)) {
                                offset = hdr.file[i].offset;
                            }
                        }
                        if (offset == 0) {
                            lerror("no such exefs file %s", filename);
                            return NULL;
                        }
                        linfo("opening exefs file %s", filename);
                        return session_create_arg(port_handle_fs_selfncch,
                                                  s->gamecard.exefs_off +
                                                      offset);
                    }
                    default:
                        lerror("unknown selfNCCH file");
                        return NULL;
                }
            } else {
                lerror("unknown selfNCCH file path type");
                return NULL;
            }
            break;
        }
        case 4:
        case 7:
        case 9: {

            int fd = -1;
            for (int i = 0; i < FS_FILE_MAX; i++) {
                if (s->services.fs.files[i] == NULL) {
                    fd = i;
                    break;
                }
            }
            if (fd == -1) {
                lerror("ran out of files");
                return NULL;
            }

            char* filepath =
                create_text_path(archive, pathtype, rawpath, pathsize);

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
                lwarn("file %s not found", filepath);
                free(filepath);
                return NULL;
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
                perror("fdopen");
                free(filepath);
                return NULL;
            }
            s->services.fs.files[fd] = fp;

            KSession* ses = session_create_arg(port_handle_fs_file, fd);
            linfo("opened file %s with fd %d", filepath, fd);

            free(filepath);

            return ses;
            break;
        }
        case 0x2345678a: {
            if (pathtype == FSPATH_BINARY) {
                u32* path = rawpath;
                if (path[0] == 0 && path[2] == 0) {
                    linfo("opening romfs");
                    return session_create_arg(port_handle_fs_sysfile,
                                              archive >> 32);
                } else {
                    lwarn("unknown path for archive 0x2345678a");
                    return 0;
                }
            } else {
                lerror("unknown selfNCCH(0x234578a) file path type");
                return NULL;
            }
            break;
        }
        default:
            lerror("unknown archive %llx", archive);
            return NULL;
    }
}

bool fs_create_file(u64 archive, u32 pathtype, void* rawpath, u32 pathsize,
                    u32 flags, u64 filesize) {
    switch (archive << 32 >> 32) {
        case 4:
        case 7:
        case 9: {
            char* filepath =
                create_text_path(archive, pathtype, rawpath, pathsize);

            linfo("creating file %s with size %x", filepath, filesize);

            int hostfd =
                open(filepath, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
            free(filepath);
            if (hostfd < 0) {
                linfo("cannot create file");
                return false;
            }
            ftruncate(hostfd, filesize);
            close(hostfd);

            return true;
        }
        default:
            lerror("unknown archive %llx", archive);
            return false;
    }
}

KSession* fs_open_dir(HLE3DS* s, u64 archive, u32 pathtype, void* rawpath,
                      u32 pathsize) {
    switch (archive << 32 >> 32) {
        case 4:
        case 7:
        case 9: {

            int fd = -1;
            for (int i = 0; i < FS_FILE_MAX; i++) {
                if (s->services.fs.dirs[i] == NULL) {
                    fd = i;
                    break;
                }
            }
            if (fd == -1) {
                lerror("ran out of dirs");
                return NULL;
            }

            char* filepath =
                create_text_path(archive, pathtype, rawpath, pathsize);

            DIR* dp = opendir(filepath);
            if (!dp) {
                linfo("failed to open directory %s", filepath);
                free(filepath);
                return NULL;
            }
            s->services.fs.dirs[fd] = dp;

            KSession* ses = session_create_arg(port_handle_fs_dir, fd);
            linfo("opened directory %s with fd %d", filepath, fd);

            free(filepath);

            return ses;
            break;
        }
        default:
            lerror("unknown archive %llx", archive);
            return NULL;
    }
}

bool fs_create_dir(u64 archive, u32 pathtype, void* rawpath, u32 pathsize) {
    switch (archive << 32 >> 32) {
        case 4:
        case 7:
        case 9: {
            char* filepath =
                create_text_path(archive, pathtype, rawpath, pathsize);

            linfo("creating directory %s", filepath);

            if (mkdir(filepath, S_IRWXU) < 0) {
                linfo("cannot create directory");
                free(filepath);
                return false;
            }
            free(filepath);
            return true;
        }
        default:
            lerror("unknown archive %llx", archive);
            return false;
    }
}
