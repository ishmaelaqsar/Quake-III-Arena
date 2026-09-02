#ifdef _WIN32

#include "sys_local.h"
#include <SDL.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <string.h>

extern "C" {

#define MAX_FOUND_FILES 0x1000

static char cdPath[MAX_OSPATH];
static char installPath[MAX_OSPATH];
static char homePath[MAX_OSPATH];

void Sys_Mkdir(const char *path) {
    _mkdir(path);
}

char *Sys_Cwd(void) {
    static char cwd[MAX_OSPATH];
    if (_getcwd(cwd, sizeof(cwd) - 1)) {
        cwd[MAX_OSPATH - 1] = 0;
    } else {
        cwd[0] = 0;
    }
    return cwd;
}

void Sys_SetDefaultCDPath(const char *path) {
    Q_strncpyz(cdPath, path, sizeof(cdPath));
}

char *Sys_DefaultCDPath(void) {
    return cdPath;
}

void Sys_SetDefaultInstallPath(const char *path) {
    Q_strncpyz(installPath, path, sizeof(installPath));
}

char *Sys_DefaultInstallPath(void) {
    if (*installPath) {
        return installPath;
    }
    char *base = SDL_GetBasePath();
    if (base) {
        Q_strncpyz(installPath, base, sizeof(installPath));
        SDL_free(base);
        size_t len = strlen(installPath);
        while (len > 0 && (installPath[len - 1] == '/' || installPath[len - 1] == '\\')) {
            installPath[--len] = '\0';
        }
        return installPath;
    }
    return Sys_Cwd();
}

void Sys_SetDefaultHomePath(const char *path) {
    Q_strncpyz(homePath, path, sizeof(homePath));
}

char *Sys_DefaultHomePath(void) {
    if (*homePath) {
        return homePath;
    }
    char *pref = SDL_GetPrefPath("", "Quake3");
    if (pref) {
        Q_strncpyz(homePath, pref, sizeof(homePath));
        SDL_free(pref);
        size_t len = strlen(homePath);
        while (len > 0 && (homePath[len - 1] == '/' || homePath[len - 1] == '\\')) {
            homePath[--len] = '\0';
        }
        return homePath;
    }
    return Sys_DefaultInstallPath();
}

char *Sys_GetCurrentUser(void) {
    static char user[256];
    DWORD size = sizeof(user);
    if (GetUserNameA(user, &size)) {
        return user;
    }
    return (char *)"player";
}

char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs) {
    char search[MAX_OSPATH];
    char *list[MAX_FOUND_FILES];
    char **listCopy;
    WIN32_FIND_DATAA findData;
    HANDLE findHandle;
    int nfiles = 0;
    qboolean dironly = wantsubs;
    int i;

    if (!extension) {
        extension = "";
    }
    if (extension[0] == '/' && extension[1] == 0) {
        extension = "";
        dironly = qtrue;
    }

    Com_sprintf(search, sizeof(search), "%s/*", directory);
    findHandle = FindFirstFileA(search, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        *numfiles = 0;
        return NULL;
    }

    do {
        if (!strcmp(findData.cFileName, ".") || !strcmp(findData.cFileName, "..")) {
            continue;
        }

        qboolean isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? qtrue : qfalse;
        if ((dironly && !isDir) || (!dironly && isDir)) {
            continue;
        }

        if (*extension) {
            size_t nameLen = strlen(findData.cFileName);
            size_t extLen = strlen(extension);
            if (nameLen < extLen || Q_stricmp(findData.cFileName + nameLen - extLen, extension)) {
                continue;
            }
        }

        if (nfiles == MAX_FOUND_FILES - 1) {
            break;
        }
        list[nfiles] = CopyString(findData.cFileName);
        nfiles++;
    } while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);

    *numfiles = nfiles;
    if (!nfiles) {
        return NULL;
    }

    listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (i = 0; i < nfiles; i++) {
        listCopy[i] = list[i];
    }
    listCopy[i] = NULL;
    return listCopy;
}

void Sys_FreeFileList(char **list) {
    int i;
    if (!list) {
        return;
    }
    for (i = 0; list[i]; i++) {
        Z_Free(list[i]);
    }
    Z_Free(list);
}

} // extern "C"

#endif // _WIN32
