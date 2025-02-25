SECT("General")
BOOL("VerboseLog", g_infologs)
CMT("0=timed wait, 1=vsync, 2=audio sync")
INT("FrameSyncMode", ctremu.syncmode)

SECT("Video")
INT("VideoScale", ctremu.videoscale)
INT("SwVshNumThreads", ctremu.vshthreads)
BOOL("ShaderJIT", ctremu.shaderjit)
BOOL("HwVertexShaders", ctremu.hwvshaders)
BOOL("Ubershader", ctremu.ubershader)

SECT("Audio")
BOOL("StartMuted", ctremu.mute)