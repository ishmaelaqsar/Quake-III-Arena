#pragma once

#ifndef SYS_LOCAL_H
#define SYS_LOCAL_H

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef __cplusplus
extern "C" {
#endif

void Sys_PlatformInit(void);
void Sys_PlatformExit(void);
void Sys_InitSignals(void);

// True while the tty console is usable, so that NET_Sleep can also wait on standard input.
// Defined per platform: sys_unix.cpp starts it true, sys_win32.cpp leaves it false.
extern qboolean stdin_active;

void Sys_ConsoleInputInit(void);
void Sys_ConsoleInputShutdown(void);
char *Sys_ConsoleInput(void);

void Sys_QueEvent(int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr);
void Sys_SendKeyEvents(void);
qboolean Sys_GetPacket(netadr_t *net_from, msg_t *net_message);

void IN_Init(void);
void IN_Frame(void);
void IN_Shutdown(void);
void IN_JoyMove(void);
void IN_StartupJoystick(void);

char **Sys_ListFilteredFiles(const char *basedir, const char *subdirs, const char *filter, int *numfiles);

#ifdef __cplusplus
}
#endif

#endif // SYS_LOCAL_H
