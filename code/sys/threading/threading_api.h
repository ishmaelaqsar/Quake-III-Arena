#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Sys_AssertMainThread(const char *file, int line);
void Sys_PostToMainThread(void (*fn)(void *), void *ctx);
void Sys_MainThreadQueueDrain(int max_ms);

int Sys_JobSubmit(void (*fn)(void *), void *ctx, void (*done)(void *), int priority);
void Sys_JobWait(int handle);
void Sys_JobCancel(int handle);

#if !defined(NDEBUG) || defined(Q3_SANITIZE)
#define Q3_ASSERT_MAIN_THREAD() Sys_AssertMainThread(__FILE__, __LINE__)
#else
#define Q3_ASSERT_MAIN_THREAD() ((void)0)
#endif

#ifdef __cplusplus
}
#endif
