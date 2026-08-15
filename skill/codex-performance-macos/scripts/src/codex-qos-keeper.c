#include <errno.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <unistd.h>

#define PRIO_DARWIN_ROLE 6
#define PRIO_DARWIN_ROLE_USER_INIT 0x7

static const char *allowed_paths[] = {
    "/Applications/ChatGPT.app/Contents/Resources/codex",
    "/Applications/ChatGPT.app/Contents/Resources/codex-code-mode-host",
    "/Applications/ChatGPT.app/Contents/Resources/cua_node/bin/node",
    "/Applications/ChatGPT.app/Contents/Resources/cua_node/bin/node_repl",
};

static int path_allowed(const char *path) {
    for (size_t index = 0; index < sizeof(allowed_paths) / sizeof(allowed_paths[0]); ++index) {
        if (strcmp(path, allowed_paths[index]) == 0) return 1;
    }
    return 0;
}

static void apply_roles(uid_t target_uid, int verbose) {
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (bytes <= 0) return;
    pid_t *pids = calloc(1, (size_t)bytes);
    if (!pids) return;
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, bytes);
    if (bytes <= 0) { free(pids); return; }

    int count = bytes / (int)sizeof(pid_t);
    for (int index = 0; index < count; ++index) {
        pid_t pid = pids[index];
        if (pid <= 1) continue;
        struct proc_bsdinfo info;
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
            info.pbi_uid != target_uid) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, path, sizeof(path)) <= 0 || !path_allowed(path)) continue;

        errno = 0;
        int role = getpriority(PRIO_DARWIN_ROLE, (id_t)pid);
        if (errno == 0 && role == PRIO_DARWIN_ROLE_USER_INIT) {
            if (verbose) printf("pid=%d role=user-initiated unchanged path=%s\n", pid, path);
            continue;
        }
        if (setpriority(PRIO_DARWIN_ROLE, (id_t)pid, PRIO_DARWIN_ROLE_USER_INIT) == 0) {
            if (verbose) printf("pid=%d role=user-initiated applied path=%s\n", pid, path);
        } else if (verbose && errno != ENOTSUP && errno != ESRCH) {
            fprintf(stderr, "pid=%d role update failed: %s path=%s\n", pid, strerror(errno), path);
        }
    }
    free(pids);
}

int main(int argc, char *argv[]) {
    uid_t target_uid = 0;
    int uid_set = 0;
    int once = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[index], "--uid") == 0 && index + 1 < argc) {
            target_uid = (uid_t)strtoul(argv[++index], NULL, 10);
            uid_set = 1;
        } else {
            fprintf(stderr, "usage: codex-qos-keeper [--uid uid] [--once]\n");
            return 64;
        }
    }
    if (geteuid() != 0) {
        fprintf(stderr, "codex-qos-keeper must run as root\n");
        return 77;
    }
    if (!uid_set || target_uid == 0) {
        fprintf(stderr, "codex-qos-keeper requires a non-root --uid\n");
        return 64;
    }
    do {
        apply_roles(target_uid, once);
        if (!once) sleep(2);
    } while (!once);
    return 0;
}
