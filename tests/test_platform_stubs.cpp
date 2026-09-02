#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"

int Sys_Milliseconds(void) {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

void Sys_Print(const char *msg) {
    std::fputs(msg, stdout);
}

void Sys_Error(const char *error, ...) {
    va_list argptr;
    char text[1024];

    va_start(argptr, error);
    std::vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    std::fprintf(stderr, "Sys_Error: %s\n", text);
    std::abort();
}

void Sys_Quit(void) {
    std::exit(0);
}

void Sys_SnapVector(float *v) {
    v[0] = std::rint(v[0]);
    v[1] = std::rint(v[1]);
    v[2] = std::rint(v[2]);
}

void Sys_ShowConsole(int level, qboolean quitOnClose) {
}

qboolean Sys_IsDedicatedBuild(void) {
    return qtrue;
}

void Sys_ReleaseDisplay(void) {
}

qboolean Sys_LowPhysicalMemory(void) {
    return qfalse;
}

void Sys_BeginProfiling(void) {
}

void Sys_EndProfiling(void) {
}

void Sys_Init(void) {
}

sysEvent_t Sys_GetEvent(void) {
    sysEvent_t ev;
    Com_Memset(&ev, 0, sizeof(ev));
    return ev;
}

void Sys_BeginStreamedFile(fileHandle_t f, int readAhead) {
}

void Sys_EndStreamedFile(fileHandle_t f) {
}

int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f) {
    return 0;
}

void Sys_StreamSeek(fileHandle_t f, int offset, int origin) {
}

qboolean Sys_CheckCD(void) {
    return qtrue;
}

qboolean stdin_active = qfalse;

void GLimp_LogComment(char *comment) {}
void GLimp_EndFrame(void) {}
void GLimp_RendererSleep(void) {}
qboolean GLimp_SpawnRenderThread(void (*function)(void)) { return qfalse; }
void GLimp_WakeRenderer(void *data) {}
void GLimp_FrontEndSleep(void) {}
void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256]) {}
void GLimp_Shutdown(void) {}
int GLimp_Init(void) { return 0; }

// Client dummies
cvar_t *cl_shownet = nullptr;

void CL_Shutdown(void) {}
void CL_Init(void) {}
void CL_MouseEvent(int dx, int dy, int time) {}
void Key_WriteBindings(fileHandle_t f) {}
void CL_Frame(int msec) {}
void CL_PacketEvent(netadr_t from, msg_t *msg) {}
void CL_CharEvent(int key) {}
void CL_Disconnect(qboolean showMainMenu) {}
void CL_MapLoading(void) {}
qboolean CL_GameCommand(void) { return qfalse; }
void CL_KeyEvent(int key, qboolean down, unsigned time) {}
qboolean UI_GameCommand(void) { return qfalse; }
void CL_ForwardCommandToServer(const char *string) {}
void CL_ConsolePrint(char *txt) {}
void CL_JoystickEvent(int axis, int value, int time) {}
void CL_InitKeyCommands(void) {}
void CL_CDDialog(void) {}
void CL_FlushMemory(void) {}
void CL_StartHunkUsers(void) {}
void CL_ShutdownAll(void) {}
qboolean CL_CDKeyValidate(const char *key, const char *checksum) { return qtrue; }
void CL_ShutdownCGame(void) {}
void CL_ShutdownUI(void) {}
void CIN_CloseAllVideos(void) {}
void S_ClearSoundBuffer(void) {}
qboolean UI_usesUniqueCDKey(void) { return qfalse; }

} // extern "C"
