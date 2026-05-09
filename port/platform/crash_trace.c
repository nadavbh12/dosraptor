// port/platform/crash_trace.c
//
// Print a backtrace and short context dump on SIGSEGV/SIGBUS/SIGILL/SIGABRT
// before letting the default handler core-dump us. Without this, every
// crash in the legacy code path is silent — we have no idea WHERE the
// 1992 source went wrong, only that it did.

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern volatile int framecount;

static void on_crash(int sig, siginfo_t *info, void *uctx)
{
    (void)uctx;

    const char *signame = "?";
    switch (sig) {
        case SIGSEGV: signame = "SIGSEGV"; break;
        case SIGBUS:  signame = "SIGBUS";  break;
        case SIGILL:  signame = "SIGILL";  break;
        case SIGABRT: signame = "SIGABRT"; break;
        case SIGFPE:  signame = "SIGFPE";  break;
    }

    /* async-signal-safe: write() on stderr, no printf (which uses malloc) */
    char hdr[256];
    int n = snprintf(hdr, sizeof hdr,
        "\n\n=== CRASH %s addr=%p framecount=%d ===\n",
        signame, info ? info->si_addr : NULL, framecount);
    write(2, hdr, (size_t)n);

    void *frames[64];
    int nframes = backtrace(frames, 64);
    backtrace_symbols_fd(frames, nframes, 2);

    write(2, "=== end crash ===\n", 18);

    /* re-raise with default handler so we still core-dump for lldb */
    signal(sig, SIG_DFL);
    raise(sig);
}

void raptor_crash_trace_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
}
