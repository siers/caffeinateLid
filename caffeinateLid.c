/*
 * caffeinateLid: keep the Mac awake with the lid closed until interrupted.
 *
 * Sets `pmset -a disablesleep 1`, waits for SIGINT/SIGTERM/SIGHUP/SIGQUIT,
 * then restores the previous value. Designed for `sudo timeout 60 caffeinateLid`.
 *
 * The restore is done by a watchdog child in its own session that ignores all
 * signals and acts only when the main process's pipe closes, so it also runs
 * if the main process is SIGKILLed (e.g. `timeout -k`).
 *
 * CAFFEINATELID_DRY_RUN=<file>: don't call pmset or require root; keep the
 * simulated SleepDisabled value (0/1) in <file> instead (still via fork/exec).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PMSET "/usr/bin/pmset"

static const char *dry_run_file;

static void noop(int sig) { (void)sig; }

/* Runs argv[0] with argv; if out is non-NULL, the child's stdout is read into it. Returns the exit status or -1. */
static int run(char *const argv[], char *out, size_t out_size) {
    int fds[2];
    if (pipe(fds) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        if (out) dup2(fds[1], STDOUT_FILENO);
        else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) dup2(devnull, STDOUT_FILENO);
        }
        close(fds[0]);
        close(fds[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    size_t len = 0;
    ssize_t n;
    char discard[256];
    for (;;) {
        char *dst = out && len + 1 < out_size ? out + len : discard;
        size_t cap = out && len + 1 < out_size ? out_size - 1 - len : sizeof discard;
        n = read(fds[0], dst, cap);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (dst != discard) len += (size_t)n;
    }
    if (out) out[len] = '\0';
    close(fds[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Dry run swaps pmset for /bin/sh one-liners on a state file, keeping the same fork/exec/wait path. */
static int set_state(int value) {
    char *v = value ? "1" : "0";
    if (dry_run_file) {
        char *argv[] = {"/bin/sh", "-c", "printf '%s\\n' \"$1\" > \"$2\"", "sh", v, (char *)dry_run_file, NULL};
        return run(argv, NULL, 0) == 0 ? 0 : -1;
    }
    char *argv[] = {PMSET, "-a", "disablesleep", v, NULL};
    return run(argv, NULL, 0) == 0 ? 0 : -1;
}

/* Returns 0 or 1, or -1 if the state could not be read. */
static int get_state(void) {
    char out[8192];
    int rc;
    if (dry_run_file) {
        char *argv[] = {"/bin/sh", "-c", "printf ' SleepDisabled\\t\\t%s\\n' \"$(cat \"$1\" 2>/dev/null || echo 0)\"", "sh", (char *)dry_run_file, NULL};
        rc = run(argv, out, sizeof out);
    } else {
        char *argv[] = {PMSET, "-g", NULL};
        rc = run(argv, out, sizeof out);
    }
    if (rc != 0) return -1;
    char *p = strstr(out, "SleepDisabled");
    if (!p) return -1;
    return atoi(p + strlen("SleepDisabled")) ? 1 : 0;
}

/* Waits for the main process to go away, then restores the saved state. */
static void watchdog(int read_fd, int previous) {
    setsid();
    /* SIGCHLD stays default: ignoring it auto-reaps children and breaks waitpid on pmset. */
    for (int s = 1; s < NSIG; s++)
        if (s != SIGKILL && s != SIGSTOP && s != SIGCHLD) signal(s, SIG_IGN);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);

    char buf;
    ssize_t n;
    do n = read(read_fd, &buf, 1);
    while (n > 0 || (n < 0 && errno == EINTR));

    for (int attempt = 0; attempt < 5; attempt++) {
        if (set_state(previous) == 0) _exit(0);
        sleep(1);
    }
    fprintf(stderr, "caffeinateLid: FAILED to restore; run: sudo pmset -a disablesleep %d\n", previous);
    _exit(1);
}

int main(int argc, char **argv) {
    (void)argv;
    /* Block termination signals first so an early interrupt can't skip the restore. */
    sigset_t stop;
    sigemptyset(&stop);
    sigaddset(&stop, SIGINT);
    sigaddset(&stop, SIGTERM);
    sigaddset(&stop, SIGHUP);
    sigaddset(&stop, SIGQUIT);
    sigprocmask(SIG_BLOCK, &stop, NULL);
    /* A real handler, not SIG_IGN: signals inherited as ignored (nohup, `&`) would never reach sigwait. */
    struct sigaction sa = {.sa_handler = noop};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (argc > 1) {
        fprintf(stderr, "usage: sudo caffeinateLid   (runs until interrupted; e.g. sudo timeout 60 caffeinateLid)\n");
        return 2;
    }

    const char *dr = getenv("CAFFEINATELID_DRY_RUN");
    if (dr && *dr) dry_run_file = dr;

    if (!dry_run_file && geteuid() != 0) {
        fprintf(stderr, "caffeinateLid: must run as root (sudo caffeinateLid)\n");
        return 1;
    }

    int previous = get_state();
    if (previous < 0) {
        fprintf(stderr, "caffeinateLid: could not read SleepDisabled from pmset -g\n");
        return 1;
    }

    int fds[2];
    if (pipe(fds) < 0) { perror("caffeinateLid: pipe"); return 1; }
    pid_t wd = fork();
    if (wd < 0) { perror("caffeinateLid: fork"); return 1; }
    if (wd == 0) {
        close(fds[1]);
        watchdog(fds[0], previous);
    }
    close(fds[0]);

    int rc = 0;
    if (set_state(1) != 0) {
        fprintf(stderr, "caffeinateLid: pmset -a disablesleep 1 failed\n");
        rc = 1;
    } else {
        fprintf(stderr, "caffeinateLid: sleep disabled (lid close won't sleep); interrupt to restore\n");
        int sig;
        sigwait(&stop, &sig);
    }

    close(fds[1]); /* wakes the watchdog, which restores the previous state */
    int status = 0;
    while (waitpid(wd, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    fprintf(stderr, "caffeinateLid: restored disablesleep %d\n", previous);
    return rc;
}
