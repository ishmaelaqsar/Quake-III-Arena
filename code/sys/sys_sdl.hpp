#pragma once

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "q_shared.h"
#include "qcommon.h"
#include "client.h"

// System and platform prototypes for SDL2 backend
void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );
void Sys_SendKeyEvents( void );

// GLimp
void GLimp_Init( void );
void GLimp_Shutdown( void );
void GLimp_EndFrame( void );
qboolean GLimp_SpawnRenderThread( void (*function)( void ) );
void *GLimp_RendererSleep( void );
void GLimp_FrontEndSleep( void );
void GLimp_WakeRenderer( void *data );
void GLimp_LogComment( char *comment );
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] );

// Sound DMA
qboolean SNDDMA_Init( void );
int SNDDMA_GetDMAPos( void );
void SNDDMA_Shutdown( void );
void SNDDMA_BeginPainting( void );
void SNDDMA_Submit( void );

// Input
void IN_Init( void );
void IN_Frame( void );
void IN_Shutdown( void );
void IN_JoyMove( void );
void IN_StartupJoystick( void );

#ifdef __cplusplus
}
#endif
