#include "fs.h"

#include <stdio.h>

#include "../3ds.h"
#include "../srv.h"

DECL_PORT(fs) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0801:
            linfo("Initialize");
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        case 0x0802:
            linfo("OpenFile %s", PTR(cmd_params[9]));
            cmd_params[0] = MAKE_IPCHEADER(1, 2);
            cmd_params[1] = 0;
            cmd_params[2] = 0;
            lwarn("stubbed OpenFile");
            cmd_params[3] = 0x12345678;
            break;
        case 0x0803: {
            u32 archive = cmd_params[2];
            u32 archivepathtype = cmd_params[3];
            u32 filepathtype = cmd_params[5];
            char* archivepath = PTR(cmd_params[10]);
            char* filepath = PTR(cmd_params[12]);
            cmd_params[0] = MAKE_IPCHEADER(1, 2);
            cmd_params[1] = 0;
            switch (archive) {
                case 3:
                    if (filepathtype == 2) {
                        switch (filepath[0]) {
                            case 0: {
                                u32 h = handle_new(s);
                                KSession* ses =
                                    session_create(port_handle_fs_romfs);
                                HANDLE_SET(h, ses);
                                ses->hdr.refcount = 1;
                                cmd_params[3] = h;
                                linfo("opened romfs with handle %x", h);
                                break;
                            }
                            default:
                                lerror("unknown selfNCCH file");
                                cmd_params[1] = -1;
                        }
                    } else {
                        lerror("unknown selfNCCH file path type");
                        cmd_params[1] = -1;
                    }
                    break;
                default:
                    lerror("unknown archive %d", archive);
                    cmd_params[1] = -1;
            }
            break;
        }
        case 0x080c: {
            linfo("OpenArchive");
            cmd_params[0] = MAKE_IPCHEADER(3, 0);
            cmd_params[1] = 0;
            lwarn("stubbed OpenArchive");
            cmd_params[2] = 0x12345678;
            cmd_params[3] = 0x12345678;
            break;
        }
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = -1;
            break;
    }
}

DECL_PORT(fs_romfs) {
    u32* cmd_params = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0802: {
            u64 offset = cmd_params[1];
            offset |= (u64) cmd_params[2] << 32;
            u32 size = cmd_params[3];
            void* data = PTR(cmd_params[5]);

            linfo("reading at offset 0x%lx, size 0x%x", offset, size);

            cmd_params[0] = MAKE_IPCHEADER(2, 0);
            cmd_params[1] = 0;
            fseek(s->gamecard.fp, s->gamecard.romfs_off + 0x1000 + offset,
                  SEEK_SET);
            cmd_params[2] = fread(data, 1, size, s->gamecard.fp);
            break;
        }
        case 0x0808:
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x", cmd.command);
            cmd_params[0] = MAKE_IPCHEADER(1, 0);
            cmd_params[1] = -1;
            break;
    }
}