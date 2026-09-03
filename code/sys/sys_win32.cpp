#ifdef _WIN32

#include "sys_local.h"
#include <windows.h>
#include <conio.h>
#include <string.h>

// No tty console on Windows, so NET_Sleep never waits on standard input.
qboolean stdin_active = qfalse;

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

static LONG WINAPI Sys_UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo) {
    return EXCEPTION_CONTINUE_SEARCH;
}

void Sys_InitSignals(void) {
    SetUnhandledExceptionFilter(Sys_UnhandledExceptionFilter);
}

} // extern "C"

#endif // _WIN32
