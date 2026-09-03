#ifndef _WIN32

#include "sys_local.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#include <execinfo.h>

extern "C" {

volatile sig_atomic_t sys_quitRequested = 0;
static volatile sig_atomic_t crash_in_progress = 0;

static void Sys_CrashHandler(int sig, siginfo_t *info, void *ucontext) {
    (void)info;
    (void)ucontext;
    if (crash_in_progress) {
        raise(sig);
        return;
    }
    crash_in_progress = 1;

    const char banner[] = "\n*** FATAL SIGNAL RECEIVED: ";
    if (write(2, banner, sizeof(banner) - 1)) {}

    char sigbuf[16];
    int siglen = 0;
    int s = sig;
    if (s >= 10) {
        sigbuf[siglen++] = (char)('0' + (s / 10));
        s %= 10;
    }
    sigbuf[siglen++] = (char)('0' + s);
    sigbuf[siglen++] = '\n';
    if (write(2, sigbuf, siglen)) {}

    void *buf[64];
    int n = backtrace(buf, 64);
    backtrace_symbols_fd(buf, n, 2);

    Sys_ReleaseDisplay();
    raise(sig);
}

static void Sys_QuitSignalHandler(int sig) {
    (void)sig;
    sys_quitRequested = 1;
}

#define TTY_HISTORY 32

static qboolean ttycon_on = qfalse;
static int ttycon_hide = 0;
static struct termios tty_tc;
static cc_t tty_erase;
static cc_t tty_eof;
static field_t tty_con;
static field_t ttyEditLines[TTY_HISTORY];
static int hist_count = 0;
static int hist_current = -1;

cvar_t *ttycon = NULL;
qboolean stdin_active = qtrue;

void tty_FlushIn(void) {
    char key;
    while (read(0, &key, 1) != -1) {}
}

void tty_Back(void) {
    char key;
    key = '\b';
    if (write(1, &key, 1)) {}
    key = ' ';
    if (write(1, &key, 1)) {}
    key = '\b';
    if (write(1, &key, 1)) {}
}

void tty_Hide(void) {
    int i;
    if (!ttycon_on) return;
    if (ttycon_hide) {
        ttycon_hide++;
        return;
    }
    if (tty_con.cursor > 0) {
        for (i = 0; i < tty_con.cursor; i++) {
            tty_Back();
        }
    }
    ttycon_hide++;
}

void tty_Show(void) {
    int i;
    if (!ttycon_on) return;
    if (ttycon_hide <= 0) return;
    ttycon_hide--;
    if (ttycon_hide == 0) {
        if (tty_con.cursor) {
            for (i = 0; i < tty_con.cursor; i++) {
                if (write(1, tty_con.buffer + i, 1)) {}
            }
        }
    }
}

void Hist_Add(field_t *field) {
    int i;
    for (i = TTY_HISTORY - 1; i > 0; i--) {
        ttyEditLines[i] = ttyEditLines[i - 1];
    }
    ttyEditLines[0] = *field;
    if (hist_count < TTY_HISTORY) {
        hist_count++;
    }
    hist_current = -1;
}

field_t *Hist_Prev(void) {
    int hist_prev = hist_current + 1;
    if (hist_prev >= hist_count) {
        return NULL;
    }
    hist_current++;
    return &(ttyEditLines[hist_current]);
}

field_t *Hist_Next(void) {
    if (hist_current >= 0) {
        hist_current--;
    }
    if (hist_current == -1) {
        return NULL;
    }
    return &(ttyEditLines[hist_current]);
}

void Sys_ConsoleInputInit(void) {
    struct termios tc;

    ttycon = Cvar_Get("ttycon", "1", 0);
    if (ttycon && ttycon->value) {
        if (isatty(STDIN_FILENO) != 1) {
            Com_Printf("stdin is not a tty, tty console mode failed\n");
            Cvar_Set("ttycon", "0");
            ttycon_on = qfalse;
            return;
        }
        Com_Printf("Started tty console (use +set ttycon 0 to disable)\n");
        Field_Clear(&tty_con);
        tcgetattr(0, &tty_tc);
        tty_erase = tty_tc.c_cc[VERASE];
        tty_eof = tty_tc.c_cc[VEOF];
        tc = tty_tc;
        tc.c_lflag &= ~(ECHO | ICANON);
        tc.c_iflag &= ~(ISTRIP | INPCK);
        tc.c_cc[VMIN] = 1;
        tc.c_cc[VTIME] = 0;
        tcsetattr(0, TCSADRAIN, &tc);
        fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
        ttycon_on = qtrue;
    } else {
        ttycon_on = qfalse;
    }
}

void Sys_ConsoleInputShutdown(void) {
    if (ttycon_on) {
        Com_Printf("Shutdown tty console\n");
        tcsetattr(0, TCSADRAIN, &tty_tc);
        fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) & ~O_NONBLOCK);
        ttycon_on = qfalse;
    }
}

char *Sys_ConsoleInput(void) {
    static char text[256];
    int i;
    int avail;
    char key;
    field_t *history;

    if (ttycon && ttycon->value && ttycon_on) {
        avail = read(0, &key, 1);
        if (avail != -1) {
            if ((key == (char)tty_erase) || (key == 127) || (key == 8)) {
                if (tty_con.cursor > 0) {
                    tty_con.cursor--;
                    tty_con.buffer[tty_con.cursor] = '\0';
                    tty_Back();
                }
                return NULL;
            }
            if (key && key < ' ') {
                if (key == '\n') {
                    Hist_Add(&tty_con);
                    Q_strncpyz(text, tty_con.buffer, sizeof(text));
                    Field_Clear(&tty_con);
                    key = '\n';
                    if (write(1, &key, 1)) {}
                    return text;
                }
                if (key == '\t') {
                    tty_Hide();
                    Field_CompleteCommand(&tty_con);
                    tty_con.cursor = strlen(tty_con.buffer);
                    if (tty_con.cursor > 0) {
                        if (tty_con.buffer[0] == '\\') {
                            for (i = 0; i <= tty_con.cursor; i++) {
                                tty_con.buffer[i] = tty_con.buffer[i + 1];
                            }
                            tty_con.cursor--;
                        }
                    }
                    tty_Show();
                    return NULL;
                }
                avail = read(0, &key, 1);
                if (avail != -1) {
                    if (key == '[' || key == 'O') {
                        avail = read(0, &key, 1);
                        if (avail != -1) {
                            switch (key) {
                            case 'A':
                                history = Hist_Prev();
                                if (history) {
                                    tty_Hide();
                                    tty_con = *history;
                                    tty_Show();
                                }
                                tty_FlushIn();
                                return NULL;
                            case 'B':
                                history = Hist_Next();
                                tty_Hide();
                                if (history) {
                                    tty_con = *history;
                                } else {
                                    Field_Clear(&tty_con);
                                }
                                tty_Show();
                                tty_FlushIn();
                                return NULL;
                            case 'C':
                            case 'D':
                                return NULL;
                            }
                        }
                    }
                }
                tty_FlushIn();
                return NULL;
            }
            tty_con.buffer[tty_con.cursor] = key;
            tty_con.cursor++;
            if (write(1, &key, 1)) {}
        }
        return NULL;
    } else {
        int len;
        fd_set fdset;
        struct timeval timeout;

        if (!com_dedicated || !com_dedicated->integer) {
            return NULL;
        }

        if (!stdin_active) {
            return NULL;
        }

        FD_ZERO(&fdset);
        FD_SET(0, &fdset);
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        if (select(1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset)) {
            return NULL;
        }

        len = read(0, text, sizeof(text) - 1);
        if (len <= 0) {
            stdin_active = qfalse;
            return NULL;
        }
        text[len] = 0;
        if (text[len - 1] == '\n' || text[len - 1] == '\r') {
            text[len - 1] = 0;
        }
        return text;
    }
}

void Sys_PlatformInit(void) {
    if (seteuid(getuid())) {}
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sigaction(SIGTTIN, &sa_ign, NULL);
    sigaction(SIGTTOU, &sa_ign, NULL);
    sigaction(SIGPIPE, &sa_ign, NULL);
}

void Sys_PlatformExit(void) {
}

void Sys_InitSignals(void) {
    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_sigaction = Sys_CrashHandler;
    sa_crash.sa_flags = SA_RESETHAND | SA_NODEFER | SA_SIGINFO;
    sigemptyset(&sa_crash.sa_mask);

    sigaction(SIGSEGV, &sa_crash, NULL);
    sigaction(SIGBUS, &sa_crash, NULL);
    sigaction(SIGILL, &sa_crash, NULL);
    sigaction(SIGFPE, &sa_crash, NULL);
    sigaction(SIGABRT, &sa_crash, NULL);
    sigaction(SIGTRAP, &sa_crash, NULL);

    struct sigaction sa_quit;
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = Sys_QuitSignalHandler;
    sigemptyset(&sa_quit.sa_mask);

    sigaction(SIGINT, &sa_quit, NULL);
    sigaction(SIGTERM, &sa_quit, NULL);
    sigaction(SIGHUP, &sa_quit, NULL);
}

} // extern "C"

#endif // !_WIN32
