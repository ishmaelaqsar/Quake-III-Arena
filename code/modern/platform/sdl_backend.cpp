#include "sdl_backend.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <vector>
#include <cstring>
#include <cmath>

extern "C" {
#include "tr_public.h"
#include "snd_local.h"

extern glconfig_t glConfig;
extern refimport_t ri;
// Global struct to satisfy linux_qgl.c dependencies
typedef struct {
    void* OpenGLLib;
    FILE* log_fp;
} glwstate_t;

glwstate_t glw_state;

qboolean QGL_Init( const char *dllname );

static SDL_Window*   s_window = nullptr;
static SDL_GLContext s_glContext = nullptr;
static SDL_AudioDeviceID s_audioDevice = 0;
static qboolean      s_mouseGrabbed = qfalse;

// Keycode translation map
static int TranslateSDLKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_TAB:        return K_TAB;
        case SDLK_RETURN:     return K_ENTER;
        case SDLK_ESCAPE:     return K_ESCAPE;
        case SDLK_SPACE:      return K_SPACE;
        case SDLK_BACKSPACE:  return K_BACKSPACE;
        case SDLK_CAPSLOCK:   return K_CAPSLOCK;
        case SDLK_PAUSE:      return K_PAUSE;
        case SDLK_UP:         return K_UPARROW;
        case SDLK_DOWN:       return K_DOWNARROW;
        case SDLK_LEFT:       return K_LEFTARROW;
        case SDLK_RIGHT:      return K_RIGHTARROW;
        case SDLK_LALT:
        case SDLK_RALT:       return K_ALT;
        case SDLK_LCTRL:
        case SDLK_RCTRL:      return K_CTRL;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:     return K_SHIFT;
        case SDLK_INSERT:     return K_INS;
        case SDLK_DELETE:     return K_DEL;
        case SDLK_PAGEDOWN:   return K_PGDN;
        case SDLK_PAGEUP:     return K_PGUP;
        case SDLK_HOME:       return K_HOME;
        case SDLK_END:        return K_END;
        case SDLK_F1:         return K_F1;
        case SDLK_F2:         return K_F2;
        case SDLK_F3:         return K_F3;
        case SDLK_F4:         return K_F4;
        case SDLK_F5:         return K_F5;
        case SDLK_F6:         return K_F6;
        case SDLK_F7:         return K_F7;
        case SDLK_F8:         return K_F8;
        case SDLK_F9:         return K_F9;
        case SDLK_F10:        return K_F10;
        case SDLK_F11:        return K_F11;
        case SDLK_F12:        return K_F12;
        case SDLK_KP_0:       return K_KP_INS;
        case SDLK_KP_1:       return K_KP_END;
        case SDLK_KP_2:       return K_KP_DOWNARROW;
        case SDLK_KP_3:       return K_KP_PGDN;
        case SDLK_KP_4:       return K_KP_LEFTARROW;
        case SDLK_KP_5:       return K_KP_5;
        case SDLK_KP_6:       return K_KP_RIGHTARROW;
        case SDLK_KP_7:       return K_KP_HOME;
        case SDLK_KP_8:       return K_KP_UPARROW;
        case SDLK_KP_9:       return K_KP_PGUP;
        case SDLK_KP_ENTER:   return K_KP_ENTER;
        case SDLK_KP_PERIOD:  return K_KP_DEL;
        case SDLK_KP_DIVIDE:  return K_KP_SLASH;
        case SDLK_KP_MINUS:   return K_KP_MINUS;
        case SDLK_KP_PLUS:    return K_KP_PLUS;
        case SDLK_KP_MULTIPLY:return K_KP_STAR;
        case SDLK_KP_EQUALS:  return K_KP_EQUALS;
        default:
            if (key >= 32 && key <= 126) {
                return (key >= 'A' && key <= 'Z') ? (key + 32) : key;
            }
            return 0;
    }
}

// ---------------------------------------------------------------------------
// OpenGL / Video Implementation
// ---------------------------------------------------------------------------

void GLimp_Init( void ) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        ri.Error(ERR_FATAL, "GLimp_Init: Unable to initialize SDL video: %s\n", SDL_GetError());
        return;
    }

    cvar_t* r_mode = Cvar_Get("r_mode", "3", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t* r_fullscreen = Cvar_Get("r_fullscreen", "0", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t* r_depthbits = Cvar_Get("r_depthbits", "24", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t* r_stencilbits = Cvar_Get("r_stencilbits", "8", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t* r_swapInterval = Cvar_Get("r_swapInterval", "0", CVAR_ARCHIVE);

    int width = 1024;
    int height = 768;

    if (r_mode && r_mode->integer == -1) {
        cvar_t* w = Cvar_Get("r_customwidth", "1024", CVAR_ARCHIVE);
        cvar_t* h = Cvar_Get("r_customheight", "768", CVAR_ARCHIVE);
        width = w->integer;
        height = h->integer;
    } else if (r_mode && r_mode->integer >= 0) {
        switch (r_mode->integer) {
            case 0: width = 320; height = 240; break;
            case 1: width = 400; height = 300; break;
            case 2: width = 512; height = 384; break;
            case 3: width = 640; height = 480; break;
            case 4: width = 800; height = 600; break;
            case 5: width = 960; height = 720; break;
            case 6: width = 1024; height = 768; break;
            case 7: width = 1152; height = 864; break;
            case 8: width = 1280; height = 1024; break;
            case 9: width = 1600; height = 1200; break;
            case 10: width = 2048; height = 1536; break;
            case 11: width = 856; height = 480; break;
            default: width = 1280; height = 720; break;
        }
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, (r_depthbits && r_depthbits->integer) ? r_depthbits->integer : 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, (r_stencilbits && r_stencilbits->integer) ? r_stencilbits->integer : 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (r_fullscreen && r_fullscreen->integer) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    s_window = SDL_CreateWindow("Quake III Arena (C++17)",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, flags);
    if (!s_window) {
        ri.Error(ERR_FATAL, "GLimp_Init: Failed to create window: %s\n", SDL_GetError());
        return;
    }

    s_glContext = SDL_GL_CreateContext(s_window);
    if (!s_glContext) {
        ri.Error(ERR_FATAL, "GLimp_Init: Failed to create GL context: %s\n", SDL_GetError());
        return;
    }

    if (r_swapInterval) {
        SDL_GL_SetSwapInterval(r_swapInterval->integer);
    }

    glConfig.vidWidth = width;
    glConfig.vidHeight = height;
    glConfig.windowAspect = (float)width / (float)height;
    glConfig.colorBits = 32;
    glConfig.depthBits = 24;
    glConfig.stencilBits = 8;
    glConfig.deviceSupportsGamma = qtrue;

    // Direct QGL function bindings via SDL_GL_GetProcAddress
    QGL_Init("libGL.so.1");
}

void GLimp_Shutdown( void ) {
    if (s_glContext) {
        SDL_GL_DeleteContext(s_glContext);
        s_glContext = nullptr;
    }
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void GLimp_EndFrame( void ) {
    if (s_window) {
        SDL_GL_SwapWindow(s_window);
    }
}

qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {
    return qfalse; // SMP single thread default
}

void *GLimp_RendererSleep( void ) { return nullptr; }
void GLimp_FrontEndSleep( void ) {}
void GLimp_WakeRenderer( void *data ) {}
void GLimp_LogComment( char *comment ) {}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
    if (s_window) {
        Uint16 r[256], g[256], b[256];
        for (int i = 0; i < 256; ++i) {
            r[i] = (Uint16)red[i] << 8;
            g[i] = (Uint16)green[i] << 8;
            b[i] = (Uint16)blue[i] << 8;
        }
        SDL_SetWindowGammaRamp(s_window, r, g, b);
    }
}

// ---------------------------------------------------------------------------
// Audio Implementation (SNDDMA)
// ---------------------------------------------------------------------------

static Uint32 s_dmaPos = 0;

static void AudioCallback(void* userdata, Uint8* stream, int len) {
    if (!dma.buffer) {
        std::memset(stream, 0, len);
        return;
    }

    int bytesPerSample = (dma.samplebits / 8) * dma.channels;
    int totalBytes = dma.samples * bytesPerSample;
    int currentOffset = (s_dmaPos * bytesPerSample) % totalBytes;

    if (currentOffset + len <= totalBytes) {
        std::memcpy(stream, dma.buffer + currentOffset, len);
    } else {
        int firstPart = totalBytes - currentOffset;
        std::memcpy(stream, dma.buffer + currentOffset, firstPart);
        std::memcpy(stream + firstPart, dma.buffer, len - firstPart);
    }

    s_dmaPos = (s_dmaPos + (len / bytesPerSample)) % dma.samples;
}

qboolean SNDDMA_Init( void ) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        Com_Printf("SNDDMA_Init: SDL Audio init failed: %s\n", SDL_GetError());
        return qfalse;
    }

    SDL_AudioSpec desired, obtained;
    std::memset(&desired, 0, sizeof(desired));
    desired.freq = (s_khz && s_khz->integer) ? s_khz->integer * 1000 : 22050;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = AudioCallback;

    s_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (s_audioDevice == 0) {
        Com_Printf("SNDDMA_Init: Failed to open audio device: %s\n", SDL_GetError());
        return qfalse;
    }

    dma.channels = obtained.channels;
    dma.samplebits = 16;
    dma.speed = obtained.freq;
    dma.samples = obtained.samples * 4;
    dma.submission_chunk = 1;
    dma.buffer = (byte*)calloc(dma.samples * (dma.samplebits / 8) * dma.channels, 1);

    SDL_PauseAudioDevice(s_audioDevice, 0);
    return qtrue;
}

int SNDDMA_GetDMAPos( void ) {
    return s_dmaPos;
}

void SNDDMA_Shutdown( void ) {
    if (s_audioDevice) {
        SDL_CloseAudioDevice(s_audioDevice);
        s_audioDevice = 0;
    }
    if (dma.buffer) {
        free(dma.buffer);
        dma.buffer = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SNDDMA_BeginPainting( void ) {}
void SNDDMA_Submit( void ) {}

// ---------------------------------------------------------------------------
// Input Implementation
// ---------------------------------------------------------------------------

void IN_Init( void ) {
    if (s_window) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        s_mouseGrabbed = qtrue;
    }
}

void IN_Shutdown( void ) {
    if (s_mouseGrabbed) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        s_mouseGrabbed = qfalse;
    }
}

void IN_Frame( void ) {}
void IN_JoyMove( void ) {}
void IN_StartupJoystick( void ) {}

void Sys_SendKeyEvents( void ) {
    SDL_Event event;
    int time = Sys_Milliseconds();

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int key = TranslateSDLKey(event.key.keysym.sym);
                if (key) {
                    Sys_QueEvent(time, SE_KEY, key, (event.type == SDL_KEYDOWN) ? qtrue : qfalse, 0, nullptr);
                }
                break;
            }
            case SDL_TEXTINPUT: {
                for (const char* p = event.text.text; *p; ++p) {
                    Sys_QueEvent(time, SE_CHAR, static_cast<unsigned char>(*p), 0, 0, nullptr);
                }
                break;
            }
            case SDL_MOUSEMOTION: {
                Sys_QueEvent(time, SE_MOUSE, event.motion.xrel, event.motion.yrel, 0, nullptr);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int btn = K_MOUSE1;
                if (event.button.button == SDL_BUTTON_RIGHT)  btn = K_MOUSE2;
                if (event.button.button == SDL_BUTTON_MIDDLE) btn = K_MOUSE3;
                if (event.button.button == SDL_BUTTON_X1)     btn = K_MOUSE4;
                if (event.button.button == SDL_BUTTON_X2)     btn = K_MOUSE5;

                Sys_QueEvent(time, SE_KEY, btn, (event.type == SDL_MOUSEBUTTONDOWN) ? qtrue : qfalse, 0, nullptr);
                break;
            }
            case SDL_QUIT: {
                Sys_QueEvent(time, SE_KEY, K_ESCAPE, qtrue, 0, nullptr);
                break;
            }
        }
    }
}

void Snd_Memset (void* dest, const int val, const size_t count)
{
	std::memset(dest, val, count);
}

} // extern "C"
