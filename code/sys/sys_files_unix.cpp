#include "sys_local.h"
#include <SDL.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>

extern "C" {

#define MAX_FOUND_FILES 0x1000

static char cdPath[MAX_OSPATH];
static char installPath[MAX_OSPATH];
static char homePath[MAX_OSPATH];

void Sys_Mkdir(const char *path) {
    mkdir(path, 0755);
}

char *Sys_Cwd(void) {
    static char cwd[MAX_OSPATH];
    if (getcwd(cwd, sizeof(cwd) - 1)) {
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
    char *p;

    if (*homePath) {
        return homePath;
    }

    if ((p = getenv("HOME")) != NULL) {
        Q_strncpyz(homePath, p, sizeof(homePath));
#ifdef __APPLE__
        Q_strcat(homePath, sizeof(homePath), "/Library/Application Support/Quake3");
#else
        Q_strcat(homePath, sizeof(homePath), "/.q3a");
#endif
        if (mkdir(homePath, 0700)) {
            if (errno != EEXIST) {
                Com_Printf("Sys_DefaultHomePath: Unable to create \"%s\": %s\n", homePath, strerror(errno));
                return Sys_DefaultInstallPath();
            }
        }
        return homePath;
    }
    return Sys_DefaultInstallPath();
}

char *Sys_GetCurrentUser(void) {
    struct passwd *p;

    if ((p = getpwuid(getuid())) == NULL) {
        return (char *)"player";
    }
    return p->pw_name;
}

static void Sys_ListFilteredFilesInternal(const char *basedir, char *subdirs, char *filter, char **list, int *numfiles) {
    char search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
    DIR *fdir;
    struct dirent *d;
    struct stat st;

    if (*numfiles >= MAX_FOUND_FILES - 1) {
        return;
    }

    if (strlen(subdirs)) {
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    } else {
        Com_sprintf(search, sizeof(search), "%s", basedir);
    }

    if ((fdir = opendir(search)) == NULL) {
        return;
    }

    while ((d = readdir(fdir)) != NULL) {
        Com_sprintf(search, sizeof(search), "%s/%s/%s", basedir, subdirs, d->d_name);
        if (stat(search, &st) == -1) {
            continue;
        }

        if (st.st_mode & S_IFDIR) {
            if (Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) {
                if (strlen(subdirs)) {
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
                } else {
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
                }
                Sys_ListFilteredFilesInternal(basedir, newsubdirs, filter, list, numfiles);
            }
        }
        if (*numfiles >= MAX_FOUND_FILES - 1) {
            break;
        }
        Com_sprintf(search, sizeof(search), "%s/%s", subdirs, d->d_name);
        if (!Com_FilterPath(filter, search, qfalse)) {
            continue;
        }
        list[*numfiles] = CopyString(search);
        (*numfiles)++;
    }

    closedir(fdir);
}

char **Sys_ListFilteredFiles(const char *basedir, const char *subdirs, const char *filter, int *numfiles) {
    char *list[MAX_FOUND_FILES];
    char **listCopy;
    char subdirsBuf[MAX_OSPATH];
    char filterBuf[MAX_OSPATH];
    int nfiles = 0;
    int i;

    Q_strncpyz(subdirsBuf, subdirs ? subdirs : "", sizeof(subdirsBuf));
    Q_strncpyz(filterBuf, filter ? filter : "*", sizeof(filterBuf));

    Sys_ListFilteredFilesInternal(basedir, subdirsBuf, filterBuf, list, &nfiles);

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

char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs) {
    char search[MAX_OSPATH];
    int nfiles;
    char **listCopy;
    char *list[MAX_FOUND_FILES];
    struct dirent *d;
    DIR *fdir;
    qboolean dironly = wantsubs;
    struct stat st;
    int i;

    if (filter) {
        nfiles = 0;
        char subdirs[MAX_OSPATH] = "";
        char filt[MAX_OSPATH];
        Q_strncpyz(filt, filter, sizeof(filt));
        Sys_ListFilteredFilesInternal(directory, subdirs, filt, list, &nfiles);

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

    if (!extension) {
        extension = "";
    }

    if (extension[0] == '/' && extension[1] == 0) {
        extension = "";
        dironly = qtrue;
    }

    nfiles = 0;
    if ((fdir = opendir(directory)) == NULL) {
        *numfiles = 0;
        return NULL;
    }

    while ((d = readdir(fdir)) != NULL) {
        Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
        if (stat(search, &st) == -1) {
            continue;
        }
        if ((dironly && !(st.st_mode & S_IFDIR)) ||
            (!dironly && (st.st_mode & S_IFDIR))) {
            continue;
        }

        if (*extension) {
            if (strlen(d->d_name) < strlen(extension) ||
                Q_stricmp(d->d_name + strlen(d->d_name) - strlen(extension), extension)) {
                continue;
            }
        }

        if (nfiles == MAX_FOUND_FILES - 1) {
            break;
        }
        list[nfiles] = CopyString(d->d_name);
        nfiles++;
    }

    closedir(fdir);

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
