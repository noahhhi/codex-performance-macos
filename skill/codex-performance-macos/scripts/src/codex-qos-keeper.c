#include <arpa/inet.h>
#include <errno.h>
#include <libproc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "performance-protocol.h"

#define PRIO_DARWIN_ROLE 6
#define PRIO_DARWIN_ROLE_USER_INIT 0x7
#define RECONCILE_INTERVAL_MS 10000
#define EVENT_DEBOUNCE_MS 50

struct identity {
    pid_t pid;
    uint64_t start_seconds;
    uint64_t start_microseconds;
};

struct process_record {
    struct identity identity;
    pid_t parent_pid;
    int tracked;
};

struct identity_list {
    struct identity *items;
    size_t count;
    size_t capacity;
};

static int identity_equal(struct identity left, struct identity right) {
    return left.pid == right.pid && left.start_seconds == right.start_seconds &&
           left.start_microseconds == right.start_microseconds;
}

static int list_contains(const struct identity_list *list, struct identity value) {
    for (size_t index = 0; index < list->count; ++index) {
        if (identity_equal(list->items[index], value)) return 1;
    }
    return 0;
}

static int list_append(struct identity_list *list, struct identity value) {
    if (list_contains(list, value)) return 0;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        struct identity *items = realloc(list->items, capacity * sizeof(*items));
        if (!items) return -1;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = value;
    return 0;
}

static int load_identity(pid_t pid, uid_t target_uid, struct identity *identity,
                         pid_t *parent_pid) {
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
        info.pbi_uid != target_uid) return -1;
    identity->pid = pid;
    identity->start_seconds = info.pbi_start_tvsec;
    identity->start_microseconds = info.pbi_start_tvusec;
    if (parent_pid) *parent_pid = (pid_t)info.pbi_ppid;
    return 0;
}

static int process_index_for_pid(const struct process_record *records, size_t count,
                                 pid_t pid) {
    for (size_t index = 0; index < count; ++index) {
        if (records[index].identity.pid == pid) return (int)index;
    }
    return -1;
}

static void propagate_descendants(struct process_record *records, size_t count) {
    int changed;
    do {
        changed = 0;
        for (size_t index = 0; index < count; ++index) {
            if (records[index].tracked) continue;
            int parent_index = process_index_for_pid(records, count, records[index].parent_pid);
            if (parent_index >= 0 && records[(size_t)parent_index].tracked) {
                records[index].tracked = 1;
                changed = 1;
            }
        }
    } while (changed);
}

static int register_process_filter(int kqueue_fd, pid_t pid) {
    struct kevent change;
    EV_SET(&change, (uintptr_t)pid, EVFILT_PROC, EV_ADD | EV_CLEAR,
           NOTE_FORK | NOTE_EXEC | NOTE_EXIT, 0, NULL);
    if (kevent(kqueue_fd, &change, 1, NULL, 0, NULL) == 0) return 0;
    return errno == ESRCH ? 0 : -1;
}

static void configure_timer(int kqueue_fd, int enabled) {
    struct kevent change;
    EV_SET(&change, 1, EVFILT_TIMER, enabled ? EV_ADD | EV_ENABLE : EV_DELETE,
           0, RECONCILE_INTERVAL_MS, NULL);
    if (kevent(kqueue_fd, &change, 1, NULL, 0, NULL) != 0 &&
        !(errno == ENOENT && !enabled)) {
        fprintf(stderr, "keeper timer update failed: %s\n", strerror(errno));
    }
}

static void schedule_event_reconcile(int kqueue_fd) {
    struct kevent change;
    EV_SET(&change, 2, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT,
           0, EVENT_DEBOUNCE_MS, NULL);
    if (kevent(kqueue_fd, &change, 1, NULL, 0, NULL) != 0) {
        fprintf(stderr, "keeper event timer update failed: %s\n", strerror(errno));
    }
}

static void remove_dead_roots(struct identity_list *roots,
                              const struct process_record *records, size_t count) {
    size_t output = 0;
    for (size_t index = 0; index < roots->count; ++index) {
        int record_index = process_index_for_pid(records, count, roots->items[index].pid);
        if (record_index >= 0 &&
            identity_equal(records[(size_t)record_index].identity, roots->items[index])) {
            roots->items[output++] = roots->items[index];
        }
    }
    roots->count = output;
}

static int reconcile(uid_t target_uid, int kqueue_fd, struct identity_list *roots,
                     struct identity_list *tracked, int verbose) {
    int bytes = proc_listpids(PROC_UID_ONLY, (uint32_t)target_uid, NULL, 0);
    if (bytes <= 0) return -1;
    pid_t *pids = calloc(1, (size_t)bytes);
    if (!pids) return -1;
    bytes = proc_listpids(PROC_UID_ONLY, (uint32_t)target_uid, pids, bytes);
    if (bytes <= 0) {
        free(pids);
        return -1;
    }

    size_t maximum = (size_t)bytes / sizeof(pid_t);
    struct process_record *records = calloc(maximum, sizeof(*records));
    if (!records) {
        free(pids);
        return -1;
    }
    size_t count = 0;
    for (size_t index = 0; index < maximum; ++index) {
        if (pids[index] <= 1) continue;
        struct identity identity;
        pid_t parent_pid;
        if (load_identity(pids[index], target_uid, &identity, &parent_pid) != 0) continue;
        records[count].identity = identity;
        records[count].parent_pid = parent_pid;
        records[count].tracked = list_contains(roots, identity) || list_contains(tracked, identity);
        ++count;
    }
    free(pids);

    remove_dead_roots(roots, records, count);
    propagate_descendants(records, count);

    struct identity_list next = {0};
    for (size_t index = 0; index < count; ++index) {
        if (!records[index].tracked) continue;
        if (list_append(&next, records[index].identity) != 0) {
            free(next.items);
            free(records);
            return -1;
        }
        (void)register_process_filter(kqueue_fd, records[index].identity.pid);
        errno = 0;
        int role = getpriority(PRIO_DARWIN_ROLE, (id_t)records[index].identity.pid);
        if (errno == 0 && role == PRIO_DARWIN_ROLE_USER_INIT) continue;
        if (setpriority(PRIO_DARWIN_ROLE, (id_t)records[index].identity.pid,
                        PRIO_DARWIN_ROLE_USER_INIT) == 0) {
            if (verbose) printf("pid=%d role=user-initiated applied\n",
                                records[index].identity.pid);
        } else if (verbose && errno != ESRCH && errno != ENOTSUP) {
            fprintf(stderr, "pid=%d role update failed: %s\n",
                    records[index].identity.pid, strerror(errno));
        }
    }
    free(records);
    free(tracked->items);
    *tracked = next;
    configure_timer(kqueue_fd, tracked->count > 0);
    return 0;
}

static int make_server_socket(const char *path, uid_t target_uid) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) >=
        (int)sizeof(address.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    unlink(path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chown(path, target_uid, (gid_t)-1) != 0 || chmod(path, 0600) != 0 ||
        listen(fd, 16) != 0) {
        int saved = errno;
        close(fd);
        unlink(path);
        errno = saved;
        return -1;
    }
    return fd;
}

static int read_request(int client_fd, uid_t target_uid, struct identity *identity) {
    uid_t peer_uid;
    gid_t peer_gid;
    if (getpeereid(client_fd, &peer_uid, &peer_gid) != 0 || peer_uid != target_uid) return -1;
    (void)peer_gid;
    struct performance_register_request request;
    ssize_t received;
    do {
        received = recv(client_fd, &request, sizeof(request), MSG_WAITALL);
    } while (received < 0 && errno == EINTR);
    if (received != (ssize_t)sizeof(request) ||
        ntohl(request.magic) != PERFORMANCE_REGISTER_MAGIC ||
        ntohl(request.version) != PERFORMANCE_REGISTER_VERSION) return -1;
    pid_t pid = (pid_t)(int32_t)ntohl((uint32_t)request.pid);
    if (pid <= 1 || load_identity(pid, target_uid, identity, NULL) != 0) return -1;
    return 0;
}

static int run_self_test(void) {
    struct process_record records[] = {
        {{100, 1, 0}, 1, 1},
        {{101, 2, 0}, 100, 0},
        {{102, 3, 0}, 101, 0},
        {{200, 4, 0}, 1, 0},
    };
    propagate_descendants(records, sizeof(records) / sizeof(records[0]));
    if (!records[0].tracked || !records[1].tracked || !records[2].tracked ||
        records[3].tracked) return 1;
    struct identity_list list = {0};
    if (list_append(&list, records[0].identity) != 0 ||
        list_append(&list, records[0].identity) != 0 || list.count != 1) {
        free(list.items);
        return 1;
    }
    free(list.items);
    puts("keeper lineage self-test passed");
    return 0;
}

int main(int argc, char *argv[]) {
    uid_t target_uid = 0;
    int uid_set = 0;
    int verbose = 0;
    const char *socket_path = NULL;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--self-test") == 0) return run_self_test();
        if (strcmp(argv[index], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[index], "--uid") == 0 && index + 1 < argc) {
            target_uid = (uid_t)strtoul(argv[++index], NULL, 10);
            uid_set = 1;
        } else if (strcmp(argv[index], "--socket") == 0 && index + 1 < argc) {
            socket_path = argv[++index];
        } else {
            fprintf(stderr, "usage: codex-qos-keeper --uid uid --socket path [--verbose]\n");
            return 64;
        }
    }
    if (geteuid() != 0) {
        fprintf(stderr, "codex-qos-keeper must run as root\n");
        return 77;
    }
    if (!uid_set || target_uid == 0 || !socket_path || socket_path[0] != '/') {
        fprintf(stderr, "codex-qos-keeper requires non-root --uid and absolute --socket\n");
        return 64;
    }

    signal(SIGPIPE, SIG_IGN);
    int server_fd = make_server_socket(socket_path, target_uid);
    if (server_fd < 0) {
        fprintf(stderr, "keeper socket failed: %s\n", strerror(errno));
        return 71;
    }
    int kqueue_fd = kqueue();
    if (kqueue_fd < 0) return 71;
    struct kevent socket_event;
    EV_SET(&socket_event, (uintptr_t)server_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(kqueue_fd, &socket_event, 1, NULL, 0, NULL) != 0) return 71;

    struct identity_list roots = {0};
    struct identity_list tracked = {0};
    for (;;) {
        struct kevent events[32];
        int count;
        do {
            count = kevent(kqueue_fd, NULL, 0, events, 32, NULL);
        } while (count < 0 && errno == EINTR);
        if (count < 0) break;
        int needs_reconcile = 0;
        for (int index = 0; index < count; ++index) {
            if (events[index].filter == EVFILT_READ &&
                events[index].ident == (uintptr_t)server_fd) {
                int pending = (int)events[index].data;
                do {
                    int client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    struct identity identity;
                    if (read_request(client_fd, target_uid, &identity) == 0 &&
                        list_append(&roots, identity) == 0) needs_reconcile = 1;
                    close(client_fd);
                } while (--pending > 0);
            } else if (events[index].filter == EVFILT_PROC) {
                schedule_event_reconcile(kqueue_fd);
            } else if (events[index].filter == EVFILT_TIMER) {
                needs_reconcile = 1;
            }
        }
        if (needs_reconcile) (void)reconcile(target_uid, kqueue_fd, &roots, &tracked, verbose);
    }

    free(roots.items);
    free(tracked.items);
    close(kqueue_fd);
    close(server_fd);
    unlink(socket_path);
    return 71;
}
