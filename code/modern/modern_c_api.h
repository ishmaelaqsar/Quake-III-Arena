#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Modern_Init(void);
void Modern_Frame(int msec);
void Modern_ScriptExecute(const char* script);
void Modern_ScriptEvent(const char* event_name, const char* arg);

#ifdef __cplusplus
}
#endif
