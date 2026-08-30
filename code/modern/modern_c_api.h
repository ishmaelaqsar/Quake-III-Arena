#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Modern_Init(void);
void Modern_Frame(int msec);
void Modern_ScriptExecute(const char* script);
void Modern_ScriptEvent(const char* event_name, const char* arg);

// Filesystem wrappers
int Modern_ReadFile(const char *qpath, void **buffer);
void Modern_WriteFile(const char *qpath, const void *buffer, int size);

// Cvar wrappers
struct cvar_s;
void Modern_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val);

// Math wrappers
float Modern_VectorNormalize(float *v);
void Modern_CrossProduct(const float *v1, const float *v2, float *cross);

#ifdef __cplusplus
}
#endif
