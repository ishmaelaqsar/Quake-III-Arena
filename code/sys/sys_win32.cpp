#ifdef _WIN32

#include "sys_local.h"
#include <windows.h>
#include <conio.h>
#include <string.h>

// No tty console on Windows, so NET_Sleep never waits on standard input.
qboolean stdin_active = qfalse;

volatile sig_atomic_t sys_quitRequested = 0;

extern "C" {

static char consoleInputBuffer[256];
static int consoleInputLen = 0;

void Sys_ConsoleInputInit(void) {
}

void Sys_ConsoleInputShutdown(void) {
}

char *Sys_ConsoleInput(void) {
    if (!com_dedicated || !com_dedicated->integer) {
        return NULL;
    }

    while (_kbhit()) {
        int c = _getch();
        if (c == '\r' || c == '\n') {
            consoleInputBuffer[consoleInputLen] = '\0';
            consoleInputLen = 0;
            putch('\n');
            return consoleInputBuffer;
        } else if (c == '\b') {
            if (consoleInputLen > 0) {
                consoleInputLen--;
                putch('\b');
                putch(' ');
                putch('\b');
            }
        } else if (c >= ' ' && consoleInputLen < (int)sizeof(consoleInputBuffer) - 1) {
            consoleInputBuffer[consoleInputLen++] = (char)c;
            putch(c);
        }
    }

    return NULL;
}

void Sys_PlatformInit(void) {
    SetConsoleOutputCP(CP_UTF8);
    timeBeginPeriod(1);
}

void Sys_PlatformExit(void) {
    timeEndPeriod(1);
}

static BOOL WINAPI Sys_ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        sys_quitRequested = 1;
        return TRUE;
    default:
        return FALSE;
    }
}

static LONG WINAPI Sys_UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo) {
    Sys_ReleaseDisplay();

    char message[256];
    DWORD code = pExceptionInfo && pExceptionInfo->ExceptionRecord ? pExceptionInfo->ExceptionRecord->ExceptionCode : 0;
    void *address = pExceptionInfo && pExceptionInfo->ExceptionRecord ? pExceptionInfo->ExceptionRecord->ExceptionAddress : NULL;
    snprintf(message, sizeof(message), "Quake III Arena crashed with exception code 0x%08lx at address %p.", (unsigned long)code, address);
    MessageBoxA(NULL, message, "Quake III Arena Crash", MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

void Sys_InitSignals(void) {
    SetUnhandledExceptionFilter(Sys_UnhandledExceptionFilter);
    SetConsoleCtrlHandler(Sys_ConsoleCtrlHandler, TRUE);
}

} // extern "C"

#endif // _WIN32
