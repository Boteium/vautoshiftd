#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hotplug.h"
#include "parser.h"

#define INSTANCE_LOCK_PATH "/run/vautoshiftd.lock"

enum pending_state {
    PENDING_IDLE = 0,
    PENDING_WAITING,
    PENDING_SHIFTED_TAP_SENT,
    PENDING_PASSTHROUGH_HOLD
};

struct pending_key {
    enum pending_state state;
    uint16_t code;
    uint64_t down_time_ms;
};

struct runtime_state {
    int uinput_fd;
    int autoshift_timeout_ms;
    int modifier_count;
    struct pending_key pending;
};

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int signo) {
    (void)signo;
    keep_running = 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int acquire_instance_lock(void) {
    int fd;
    char pid_buf[32];
    int len;

    fd = open(INSTANCE_LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to open lock file %s: %s\n",
                INSTANCE_LOCK_PATH, strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            fprintf(stderr, "Another vautoshiftd instance is already running.\n");
        } else {
            fprintf(stderr, "Failed to lock %s: %s\n",
                    INSTANCE_LOCK_PATH, strerror(errno));
        }
        close(fd);
        return -1;
    }

    if (ftruncate(fd, 0) < 0) {
        fprintf(stderr, "Failed to truncate %s: %s\n",
                INSTANCE_LOCK_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    len = snprintf(pid_buf, sizeof(pid_buf), "%ld\n", (long)getpid());
    if (write(fd, pid_buf, (size_t)len) < 0) {
        fprintf(stderr, "Failed to write %s: %s\n",
                INSTANCE_LOCK_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static int open_uinput_keyboard(void) {
    struct uinput_user_dev dev;
    int fd;
    int code;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/uinput");
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        perror("ioctl UI_SET_EVBIT");
        close(fd);
        return -1;
    }

    for (code = 0; code <= KEY_MAX; code++) {
        if (ioctl(fd, UI_SET_KEYBIT, code) < 0) {
            perror("ioctl UI_SET_KEYBIT");
            close(fd);
            return -1;
        }
    }

    memset(&dev, 0, sizeof(dev));
    snprintf(dev.name, sizeof(dev.name), "%s", VIRTUAL_KEYBOARD_NAME);
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x0FAC;
    dev.id.product = 0x0BEE;
    dev.id.version = 1;

    if (write(fd, &dev, sizeof(dev)) < 0) {
        perror("write uinput_user_dev");
        close(fd);
        return -1;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("ioctl UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    usleep(100000);
    return fd;
}

static int emit_key(int uinput_fd, uint16_t code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;

    if (write(uinput_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;

    if (write(uinput_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        return -1;
    }

    return 0;
}

static bool is_modifier(uint16_t code) {
    switch (code) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            return true;
        default:
            return false;
    }
}

static bool is_autoshift_candidate(uint16_t code) {
    switch (code) {
        case KEY_A:
        case KEY_B:
        case KEY_C:
        case KEY_D:
        case KEY_E:
        case KEY_F:
        case KEY_G:
        case KEY_H:
        case KEY_I:
        case KEY_J:
        case KEY_K:
        case KEY_L:
        case KEY_M:
        case KEY_N:
        case KEY_O:
        case KEY_P:
        case KEY_Q:
        case KEY_R:
        case KEY_S:
        case KEY_T:
        case KEY_U:
        case KEY_V:
        case KEY_W:
        case KEY_X:
        case KEY_Y:
        case KEY_Z:
        case KEY_GRAVE:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
        case KEY_0:
        case KEY_MINUS:
        case KEY_EQUAL:
        case KEY_BACKSLASH:
        case KEY_RIGHTBRACE:
        case KEY_LEFTBRACE:
        case KEY_SEMICOLON:
        case KEY_APOSTROPHE:
        case KEY_SLASH:
        case KEY_DOT:
        case KEY_COMMA:
            return true;
        default:
            return false;
    }
}

static void pending_start(struct runtime_state *st, uint16_t code) {
    st->pending.state = PENDING_WAITING;
    st->pending.code = code;
    st->pending.down_time_ms = now_ms();
}

static int emit_pending_tap(struct runtime_state *st) {
    if (emit_key(st->uinput_fd, st->pending.code, 1) < 0 ||
        emit_key(st->uinput_fd, st->pending.code, 0) < 0) {
        return -1;
    }
    st->pending.state = PENDING_IDLE;
    return 0;
}

static int emit_pending_passthrough_hold(struct runtime_state *st) {
    if (emit_key(st->uinput_fd, st->pending.code, 1) < 0) {
        return -1;
    }
    st->pending.state = PENDING_PASSTHROUGH_HOLD;
    return 0;
}

static int emit_pending_shifted_tap(struct runtime_state *st) {
    if (emit_key(st->uinput_fd, KEY_LEFTSHIFT, 1) < 0 ||
        emit_key(st->uinput_fd, st->pending.code, 1) < 0 ||
        emit_key(st->uinput_fd, st->pending.code, 0) < 0 ||
        emit_key(st->uinput_fd, KEY_LEFTSHIFT, 0) < 0) {
        return -1;
    }
    st->pending.state = PENDING_SHIFTED_TAP_SENT;
    return 0;
}

static int maybe_fire_pending_timeout(struct runtime_state *st) {
    uint64_t elapsed;

    if (st->pending.state != PENDING_WAITING) {
        return 0;
    }

    if (st->modifier_count > 0) {
        return emit_pending_passthrough_hold(st);
    }

    elapsed = now_ms() - st->pending.down_time_ms;
    if ((int)elapsed >= st->autoshift_timeout_ms) {
        return emit_pending_shifted_tap(st);
    }

    return 0;
}

static int pending_timeout_remaining(const struct runtime_state *st) {
    int elapsed;

    if (st->pending.state != PENDING_WAITING) {
        return -1;
    }

    elapsed = (int)(now_ms() - st->pending.down_time_ms);
    if (elapsed >= st->autoshift_timeout_ms) {
        return 0;
    }

    return st->autoshift_timeout_ms - elapsed;
}

static bool pending_needs_resolution(const struct runtime_state *st) {
    return st->pending.state == PENDING_WAITING;
}

static int resolve_pending_before_event(struct runtime_state *st,
                                        bool incoming_is_modifier,
                                        int incoming_value) {
    if (!pending_needs_resolution(st)) {
        return 0;
    }

    if (maybe_fire_pending_timeout(st) < 0) {
        return -1;
    }
    if (!pending_needs_resolution(st)) {
        return 0;
    }

    if (incoming_is_modifier && incoming_value == 1) {
        return emit_pending_passthrough_hold(st);
    }

    return emit_pending_tap(st);
}

static int finish_pending_on_release(struct runtime_state *st, uint16_t code, int value) {
    if (st->pending.state == PENDING_IDLE || code != st->pending.code || value != 0) {
        return 0;
    }

    if (st->pending.state == PENDING_PASSTHROUGH_HOLD) {
        if (emit_key(st->uinput_fd, code, 0) < 0) {
            return -1;
        }
    } else if (st->pending.state == PENDING_WAITING) {
        if (emit_pending_tap(st) < 0) {
            return -1;
        }
        return 1;
    }

    st->pending.state = PENDING_IDLE;
    return 1;
}

static void update_modifier_state(struct runtime_state *st, uint16_t code, int value) {
    if (!is_modifier(code)) {
        return;
    }

    if (value == 1) {
        st->modifier_count++;
    } else if (value == 0 && st->modifier_count > 0) {
        st->modifier_count--;
    }
}

static int handle_key_event(struct runtime_state *st, uint16_t code, int value) {
    bool mod = is_modifier(code);
    int modifiers_before = st->modifier_count;
    int pending_release_rv;

    if (value == 2) {
        return 0;
    }

    pending_release_rv = finish_pending_on_release(st, code, value);
    if (pending_release_rv < 0) {
        return -1;
    }
    if (pending_release_rv > 0) {
        return 0;
    }

    if (resolve_pending_before_event(st, mod, value) < 0) {
        return -1;
    }

    if (!mod &&
        value == 1 &&
        is_autoshift_candidate(code) &&
        modifiers_before == 0) {
        pending_start(st, code);
        return 0;
    }

    update_modifier_state(st, code, value);

    if (emit_key(st->uinput_fd, code, value) < 0) {
        return -1;
    }

    return 0;
}

static void decrement_active_count(int *active_count) {
    if (*active_count > 0) {
        (*active_count)--;
    }
}

static void log_and_disconnect_keyboard(struct selected_keyboard *kbs,
                                        struct pollfd *pfds,
                                        int index,
                                        int *active_count,
                                        const char *reason) {
    fprintf(stderr, "%s: %s (%s)\n", reason, kbs[index].info.path, kbs[index].info.name);
    disconnect_keyboard(kbs, pfds, index);
    decrement_active_count(active_count);
}


static int open_uevent_monitor(void) {
    struct sockaddr_nl addr;
    int fd;

    fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        perror("socket NETLINK_KOBJECT_UEVENT");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = (uint32_t)getpid();
    addr.nl_groups = 1;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind NETLINK_KOBJECT_UEVENT");
        close(fd);
        return -1;
    }

    return fd;
}

static bool uevent_is_input_hotplug(const char *buf, ssize_t len) {
    const char *p = buf;
    const char *end = buf + len;
    bool action_match = false;
    bool subsystem_match = false;
    bool devname_match = false;
    bool devpath_match = false;

    while (p < end && *p) {
        size_t left = (size_t)(end - p);
        size_t n = strnlen(p, left);
        const char *entry = p;

        if (n == left) {
            break;
        }

        if (starts_with(entry, "ACTION=")) {
            const char *action = entry + strlen("ACTION=");
            if (strcmp(action, "add") == 0 ||
                strcmp(action, "remove") == 0 ||
                strcmp(action, "change") == 0 ||
                strcmp(action, "bind") == 0 ||
                strcmp(action, "unbind") == 0) {
                action_match = true;
            }
        } else if (strcmp(entry, "SUBSYSTEM=input") == 0) {
            subsystem_match = true;
        } else if (starts_with(entry, "DEVNAME=input/event")) {
            devname_match = true;
        } else if (starts_with(entry, "DEVPATH=") &&
                   strstr(entry + strlen("DEVPATH="), "/event")) {
            devpath_match = true;
        }

        p += n + 1;
    }

    return action_match && subsystem_match && (devname_match || devpath_match);
}

static int run_loop(struct options *opts,
                    struct selected_keyboard *kbs,
                    int n,
                    struct runtime_state *st) {
    struct pollfd pfds[MAX_KEYBOARDS + 1];
    int uevent_fd = -1;
    int poll_count;
    bool waiting_logged = false;
    int active_count = 0;
    int i;

    if (!opts->disable_hotplug) {
        uevent_fd = open_uevent_monitor();
        if (uevent_fd < 0) {
            return -1;
        }
    }

    for (i = 0; i < n; i++) {
        pfds[i].fd = kbs[i].fd;
        pfds[i].events = (kbs[i].fd >= 0) ? POLLIN : 0;
        pfds[i].revents = 0;
    }
    if (uevent_fd >= 0) {
        pfds[n].fd = uevent_fd;
        pfds[n].events = POLLIN;
        pfds[n].revents = 0;
        poll_count = n + 1;
        (void)refresh_keyboards(opts, kbs, pfds, n);
    } else {
        poll_count = n;
    }
    active_count = active_keyboard_count(kbs, n);
    if (active_count == 0) {
        if (opts->disable_hotplug) {
            fprintf(stderr, "No matching keyboards are active and hotplug is disabled.\n");
        } else {
            fprintf(stderr, "Waiting for matching keyboard hotplug...\n");
        }
        waiting_logged = true;
    }

    while (keep_running) {
        int timeout_ms = pending_timeout_remaining(st);
        int poll_rv = poll(pfds, (nfds_t)poll_count, timeout_ms);

        if (poll_rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            if (uevent_fd >= 0) {
                close(uevent_fd);
            }
            return -1;
        }

        if (poll_rv == 0) {
            if (maybe_fire_pending_timeout(st) < 0) {
                perror("uinput write");
                if (uevent_fd >= 0) {
                    close(uevent_fd);
                }
                return -1;
            }
        }

        if (uevent_fd >= 0 && (pfds[n].revents & POLLIN)) {
            char uevent_buf[4096];
            bool hotplug_event = false;
            while (1) {
                ssize_t uevent_len = recv(uevent_fd, uevent_buf, sizeof(uevent_buf), 0);
                if (uevent_len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    perror("recv NETLINK_KOBJECT_UEVENT");
                    if (uevent_fd >= 0) {
                        close(uevent_fd);
                    }
                    return -1;
                }
                if (uevent_len == 0) {
                    break;
                }
                if (uevent_is_input_hotplug(uevent_buf, uevent_len)) {
                    hotplug_event = true;
                }
            }
            if (hotplug_event) {
                (void)refresh_keyboards(opts, kbs, pfds, n);
                active_count = active_keyboard_count(kbs, n);
            }
        }

        for (i = 0; i < n; i++) {
            if (pfds[i].fd < 0) {
                continue;
            }

            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                log_and_disconnect_keyboard(kbs, pfds, i, &active_count, "Keyboard disconnected");
                continue;
            }

            if (!(pfds[i].revents & POLLIN)) {
                continue;
            }

            while (1) {
                struct input_event ev;
                ssize_t r = read(kbs[i].fd, &ev, sizeof(ev));

                if (r < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == ENODEV || errno == EIO) {
                        log_and_disconnect_keyboard(kbs, pfds, i, &active_count,
                                                    "Keyboard disconnected during read");
                        break;
                    }
                    fprintf(stderr, "Read error from %s: %s\n",
                            kbs[i].info.path, strerror(errno));
                    if (uevent_fd >= 0) {
                        close(uevent_fd);
                    }
                    return -1;
                }

                if (r == 0) {
                    log_and_disconnect_keyboard(kbs, pfds, i, &active_count,
                                                "Keyboard disconnected (EOF)");
                    break;
                }

                if (r != (ssize_t)sizeof(ev)) {
                    continue;
                }

                if (ev.type == EV_KEY) {
                    if (handle_key_event(st, ev.code, ev.value) < 0) {
                        perror("uinput write");
                        if (uevent_fd >= 0) {
                            close(uevent_fd);
                        }
                        return -1;
                    }
                }
            }
        }

        if (active_count == 0) {
            if (!waiting_logged) {
                if (!opts->disable_hotplug) {
                    fprintf(stderr, "Waiting for matching keyboard hotplug...\n");
                }
                waiting_logged = true;
            }
        } else {
            waiting_logged = false;
        }
    }

    if (uevent_fd >= 0) {
        close(uevent_fd);
    }
    return 0;
}

int main(int argc, char **argv) {
    struct options opts;
    struct keyboard_info all_keyboards[MAX_KEYBOARDS];
    struct selected_keyboard enabled[MAX_KEYBOARDS];
    struct runtime_state st;
    int all_count;
    int enabled_count;
    int instance_lock_fd = -1;
    int i;
    int rc = 1;

    memset(&st, 0, sizeof(st));
    st.uinput_fd = -1;
    for (i = 0; i < MAX_KEYBOARDS; i++) {
        enabled[i].fd = -1;
    }

    if (parse_args(argc, argv, &opts) < 0) {
        usage(argv[0]);
        goto cleanup;
    }

    all_count = scan_keyboards(all_keyboards);
    if (all_count < 0) {
        goto cleanup;
    }

    if (opts.list_keyboards) {
        for (i = 0; i < all_count; i++) {
            printf("%s\t%s\n",
                   all_keyboards[i].cli_name,
                   all_keyboards[i].path);
        }
        rc = 0;
        goto cleanup;
    }

    instance_lock_fd = acquire_instance_lock();
    if (instance_lock_fd < 0) {
        goto cleanup;
    }

    refresh_filter_mappings(&opts, all_keyboards, all_count);
    enabled_count = select_keyboards(&opts, all_keyboards, all_count, enabled);
    if (enabled_count <= 0) {
        if (opts.disable_hotplug) {
            fprintf(stderr, "No matching keyboards currently accessible and hotplug is disabled.\n");
            goto cleanup;
        } else {
            fprintf(stderr, "No matching keyboards currently accessible; waiting for hotplug.\n");
        }
    }

    st.autoshift_timeout_ms = opts.autoshift_timeout_ms;
    st.uinput_fd = open_uinput_keyboard();
    if (st.uinput_fd < 0) {
        goto cleanup;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (opts.filter_mode == FILTER_MODE_ALLOW_ALL) {
        fprintf(stderr, "No allow/deny list provided; allowing all keyboards.\n");
    }

    fprintf(stderr, "vautoshiftd started. autoshift timeout=%dms, keyboards=%d, mode=%s\n",
            st.autoshift_timeout_ms,
            active_keyboard_count(enabled, MAX_KEYBOARDS),
            opts.filter_mode == FILTER_MODE_ALLOW_LIST ? "allow-list" :
            (opts.filter_mode == FILTER_MODE_DENY_LIST ? "deny-list" : "allow-all"));
    if (opts.disable_hotplug) {
        fprintf(stderr, "Hotplug disabled: only initially selected keyboards are used.\n");
    }

    if (run_loop(&opts, enabled, MAX_KEYBOARDS, &st) == 0) {
        rc = 0;
    }

cleanup:
    if (st.pending.state == PENDING_PASSTHROUGH_HOLD && st.uinput_fd >= 0) {
        emit_key(st.uinput_fd, st.pending.code, 0);
    }

    release_keyboards(enabled, MAX_KEYBOARDS);
    if (st.uinput_fd >= 0) {
        ioctl(st.uinput_fd, UI_DEV_DESTROY);
        close(st.uinput_fd);
    }
    if (instance_lock_fd >= 0) {
        close(instance_lock_fd);
    }
    return rc;
}
