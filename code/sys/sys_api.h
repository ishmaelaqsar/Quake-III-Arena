#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Sys_SubsystemInit(void);
void Sys_SubsystemFrame(int msec);
void Sys_ScriptExecute(const char* script);
void Sys_ScriptEvent(const char* event_name, const char* arg);

// Filesystem wrappers
int Sys_VFS_ReadFile(const char *qpath, void **buffer);
void Sys_VFS_WriteFile(const char *qpath, const void *buffer, int size);

// Cvar wrappers
struct cvar_s;
void Sys_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val);

// Math wrappers
float Sys_VectorNormalize(float *v);
void Sys_CrossProduct(const float *v1, const float *v2, float *cross);

// Logging wrappers
void Sys_SetConsoleSink(void (*sink)(const char *msg));
void Sys_LogDebug(const char *msg);
void Sys_LogInfo(const char *msg);
void Sys_LogWarn(const char *msg);
void Sys_LogError(const char *msg);

// Compatibility aliases
#define Modern_Init Sys_SubsystemInit
#define Modern_Frame Sys_SubsystemFrame
#define Modern_ScriptExecute Sys_ScriptExecute
#define Modern_ScriptEvent Sys_ScriptEvent
#define Modern_ReadFile Sys_VFS_ReadFile
#define Modern_WriteFile Sys_VFS_WriteFile
#define Modern_Cvar_NotifyChange Sys_Cvar_NotifyChange
#define Modern_VectorNormalize Sys_VectorNormalize
#define Modern_CrossProduct Sys_CrossProduct
#define Modern_SetConsoleSink Sys_SetConsoleSink
#define Modern_LogDebug Sys_LogDebug
#define Modern_LogInfo Sys_LogInfo
#define Modern_LogWarn Sys_LogWarn
#define Modern_LogError Sys_LogError

#ifdef __cplusplus
}
#endif
