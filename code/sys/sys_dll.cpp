#include "sys_local.h"
#include <SDL.h>
#include <string.h>

extern "C" {

char *Sys_DefaultInstallPath(void);

void Sys_ModuleFileName(const char *name, char *buf, int bufSize) {
    Com_sprintf(buf, bufSize, "%s" ARCH_STRING DLL_EXT, name);
}

void *Sys_LoadDll(const char *name, char *fqpath,
                  vmMainFunc_t *entryPoint,
                  intptr_t (QDECL *systemcalls)(intptr_t, ...)) {
    void *libHandle = NULL;
    void (*dllEntry)(intptr_t (QDECL *syscallptr)(intptr_t, ...));
    char fname[MAX_OSPATH];
    char *basepath;
    char *homepath;
    char *pwdpath;
    char *installpath;
    char *gamedir;
    char *fn = NULL;

    if (fqpath) {
        *fqpath = 0;
    }

    if (!name) {
        return NULL;
    }

    Sys_ModuleFileName(name, fname, sizeof(fname));

    pwdpath = Sys_Cwd();
    homepath = Cvar_VariableString("fs_homepath");
    basepath = Cvar_VariableString("fs_basepath");
    installpath = Sys_DefaultInstallPath();
    gamedir = Cvar_VariableString("fs_game");

    const char *searchPaths[] = { pwdpath, homepath, basepath, installpath };
    for (size_t i = 0; i < sizeof(searchPaths) / sizeof(searchPaths[0]); i++) {
        if (!searchPaths[i] || !searchPaths[i][0]) {
            continue;
        }
        fn = FS_BuildOSPath(searchPaths[i], gamedir, fname);
        libHandle = SDL_LoadObject(fn);
        if (libHandle) {
            break;
        }
        if (gamedir && gamedir[0] && Q_stricmp(gamedir, "baseq3")) {
            fn = FS_BuildOSPath(searchPaths[i], "baseq3", fname);
            libHandle = SDL_LoadObject(fn);
            if (libHandle) {
                break;
            }
        }
    }

    if (!libHandle) {
        Com_Printf("Sys_LoadDll(%s) failed completely: %s\n", name, SDL_GetError());
        return NULL;
    }

    dllEntry = (void (*)(intptr_t (QDECL *)(intptr_t, ...)))SDL_LoadFunction(libHandle, "dllEntry");
    *entryPoint = (vmMainFunc_t)SDL_LoadFunction(libHandle, "vmMain");

    if (!*entryPoint || !dllEntry) {
        Com_Printf("Sys_LoadDll(%s) failed finding vmMain/dllEntry: %s\n", name, SDL_GetError());
        SDL_UnloadObject(libHandle);
        return NULL;
    }

    if (fqpath && fn) {
        Q_strncpyz(fqpath, fn, MAX_QPATH);
    }

    dllEntry(systemcalls);
    return libHandle;
}

void Sys_UnloadDll(void *dllHandle) {
    if (!dllHandle) {
        return;
    }
    SDL_UnloadObject(dllHandle);
}

} // extern "C"
