#pragma once

#include "q_shared.h"

#ifdef __cplusplus
#define Q3_NOEXCEPT_BOUNDARY(body) \
    try { body } catch (const std::exception &e) { Com_Printf("^1%s: %s\n", __func__, e.what()); } \
    catch (...) { Com_Printf("^1%s: unhandled C++ exception\n", __func__); }
#endif

#ifdef __cplusplus
extern "C" {
#endif

void Sys_SubsystemInit(void);
void Sys_SubsystemShutdown(void);
void Sys_SubsystemFrame(int msec);
qboolean Sys_ScriptExecute(const char* script);
void Sys_ScriptEvent(const char* event_name, const char* arg);

// HTTP FastDL wrappers
qboolean Sys_SanitizeDownloadFilename(const char *filename);
void Sys_StartHttpDownload(const char *url, const char *outputPath);
int Sys_GetHttpDownloadStatus(void);

// Cvar wrappers
struct cvar_s;
void Sys_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val);

// Math wrappers
float Sys_VectorNormalize(float *v);
void Sys_CrossProduct(const float *v1, const float *v2, float *cross);

// Logging wrappers
// Re-read com_logLevel and developer. Com_Init calls this after the command line has been
// applied, because Sys_SubsystemInit runs before the command line is parsed.
void Sys_LogApplyLevel(void);
void Sys_SetConsoleSink(void (*sink)(const char *msg));
void Sys_LogDebug(const char *msg);
void Sys_LogInfo(const char *msg);
void Sys_LogWarn(const char *msg);
void Sys_LogError(const char *msg);

#ifdef __cplusplus
}
#endif
