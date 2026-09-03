#include "sys_local.h"
#include "logger/logger.hpp"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

extern "C" {

void CL_Shutdown(void);

#define MAX_QUED_EVENTS 256
#define MASK_QUED_EVENTS (MAX_QUED_EVENTS - 1)

static sysEvent_t eventQue[MAX_QUED_EVENTS];
static int eventHead = 0;
static int eventTail = 0;
static byte sys_packetReceived[MAX_MSGLEN];

void Sys_In_Restart_f(void) {
    IN_Shutdown();
    IN_Init();
}

void Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr) {
    sysEvent_t *ev;

    ev = &eventQue[eventHead & MASK_QUED_EVENTS];

    if (eventHead - eventTail >= MAX_QUED_EVENTS) {
        Com_Printf("Sys_QueEvent: overflow\n");
        if (ev->evPtr) {
            Z_Free(ev->evPtr);
        }
        eventTail++;
    }

    eventHead++;

    if (time == 0) {
        time = Sys_Milliseconds();
    }

    ev->evTime = time;
    ev->evType = type;
    ev->evValue = value;
    ev->evValue2 = value2;
    ev->evPtrLength = ptrLength;
    ev->evPtr = ptr;
}

sysEvent_t Sys_GetEvent(void) {
    sysEvent_t ev;
    char *s;
    msg_t netmsg;
    netadr_t adr;

    if (eventHead > eventTail) {
        eventTail++;
        return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
    }

    Sys_SendKeyEvents();

    s = Sys_ConsoleInput();
    if (s) {
        int len = strlen(s) + 1;
        char *b = (char *)Z_Malloc(len);
        Q_strncpyz(b, s, len);
        Sys_QueEvent(0, SE_CONSOLE, 0, 0, len, b);
    }

    IN_Frame();

    MSG_Init(&netmsg, sys_packetReceived, sizeof(sys_packetReceived));
    if (Sys_GetPacket(&adr, &netmsg)) {
        int len = sizeof(netadr_t) + netmsg.cursize;
        netadr_t *buf = (netadr_t *)Z_Malloc(len);
        *buf = adr;
        memcpy(buf + 1, netmsg.data, netmsg.cursize);
        Sys_QueEvent(0, SE_PACKET, 0, 0, len, buf);
    }

    if (eventHead > eventTail) {
        eventTail++;
        return eventQue[(eventTail - 1) & MASK_QUED_EVENTS];
    }

    memset(&ev, 0, sizeof(ev));
    ev.evTime = Sys_Milliseconds();
    return ev;
}

int Sys_Milliseconds(void) {
    static Uint64 baseCounter = 0;
    static Uint64 frequency = 0;

    if (baseCounter == 0) {
        frequency = SDL_GetPerformanceFrequency();
        baseCounter = SDL_GetPerformanceCounter();
        return 0;
    }

    Uint64 current = SDL_GetPerformanceCounter();
    if (frequency == 0) {
        return 0;
    }
    return (int)((current - baseCounter) * 1000 / frequency);
}

void Sys_Sleep(int msec) {
    if (msec > 0) {
        SDL_Delay(msec);
    }
}

char *Sys_GetClipboardData(void) {
    char *sdlText = SDL_GetClipboardText();
    if (!sdlText) {
        return NULL;
    }
    int len = (int)strlen(sdlText) + 1;
    char *data = (char *)Z_Malloc(len);
    memcpy(data, sdlText, len);
    SDL_free(sdlText);
    return data;
}

void Sys_Print(const char *msg) {
    fputs(msg, stderr);
}

void Sys_Exit(int ex) {
    LOG_INFO("Sys_Exit: exiting with status ", ex);
    Sys_ConsoleInputShutdown();
    Sys_PlatformExit();
    SDL_Quit();
    exit(ex);
}

void Sys_Quit(void) {
    CL_Shutdown();
    Sys_Exit(0);
}

void QDECL Sys_Error(const char *error, ...) {
    va_list argptr;
    char string[1024];

    va_start(argptr, error);
    Q_vsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);

    Sys_ConsoleInputShutdown();
    Sys_ReleaseDisplay();
    fprintf(stderr, "Sys_Error: %s\n", string);
    Sys_Exit(1);
}

void Sys_Init(void) {
    LOG_DEBUG("Sys_Init: registering platform cvars and starting input");
    Cmd_AddCommand("in_restart", Sys_In_Restart_f);
    Cvar_Set("arch", OS_STRING "-" ARCH_STRING);
    Cvar_Set("username", Sys_GetCurrentUser());
    IN_Init();
}

void Sys_PrintBinVersion(const char *name) {
    printf("%s: %s (%s-%s)\n", name,
           Sys_IsDedicatedBuild() ? "Dedicated Server" : "Full Executable",
           OS_STRING, ARCH_STRING);
}

qboolean Sys_CheckCD(void) {
    return qtrue;
}

void Sys_SnapVector(float *v) {
    v[0] = rint(v[0]);
    v[1] = rint(v[1]);
    v[2] = rint(v[2]);
}

void Sys_BeginProfiling(void) {}
void Sys_EndProfiling(void) {}
void Sys_BeginStreamedFile(fileHandle_t f, int readahead) {}
void Sys_EndStreamedFile(fileHandle_t f) {}
int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f) {
    return FS_Read(buffer, size * count, f);
}
void Sys_StreamSeek(fileHandle_t f, int offset, int origin) {
    FS_Seek(f, offset, origin);
}
void Sys_ShowConsole(int level, qboolean quitOnClose) {}
void Sys_SetErrorText(const char *text) {}
int Sys_GetProcessorId(void) { return 0; }
qboolean Sys_LowPhysicalMemory(void) { return qfalse; }

} // extern "C"

enum SysArgAction {
    SYS_ARG_CONTINUE,
    SYS_ARG_EXIT
};

static SysArgAction Sys_ParseArgs(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("Usage: %s [options] [+set cvar value] [+exec config] ...\n\n", argv[0]);
            printf("Quake III Arena Modern Engine\n\n");
            printf("Options:\n");
            printf("  -h, --help     Show this help message and exit\n");
            printf("  -v, --version  Print version information and exit\n\n");
            printf("Commands and cvars can be passed on command line with '+' prefix:\n");
            printf("  +set <cvar> <value>   Set cvar value at startup\n");
            printf("  +exec <cfg>           Execute configuration file\n");
            printf("  +map <mapname>        Start map\n");
            return SYS_ARG_EXIT;
        }
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            Sys_PrintBinVersion(argv[0]);
            return SYS_ARG_EXIT;
        }
    }
    return SYS_ARG_CONTINUE;
}

int main(int argc, char **argv) {
    SDL_SetMainReady();

    if (Sys_ParseArgs(argc, argv) == SYS_ARG_EXIT) {
        return 0;
    }

    char commandLine[MAX_STRING_CHARS] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            Q_strcat(commandLine, sizeof(commandLine), " ");
        }
        Q_strcat(commandLine, sizeof(commandLine), argv[i]);
    }

    Sys_PlatformInit();
    Com_Init(commandLine);
    NET_Init();
    Sys_ConsoleInputInit();
    Sys_InitSignals();

    while (1) {
        Com_Frame();
    }

    return 0;
}
