#define _GNU_SOURCE

#include "hotplug.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void build_cli_name(const char *name, char out[256]) {
    size_t i;
    size_t j = 0;
    bool last_was_underscore = false;

    for (i = 0; name[i] != '\0' && j < 255; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c)) {
            out[j++] = (char)c;
            last_was_underscore = false;
        } else if (!last_was_underscore && j < 255) {
            out[j++] = '_';
            last_was_underscore = true;
        }
    }

    while (j > 0 && out[j - 1] == '_') {
        j--;
    }

    if (j == 0) {
        snprintf(out, 256, "unknown");
        return;
    }

    out[j] = '\0';
}

static bool has_key(const unsigned char *bits, int code) {
    return (bits[code / 8] >> (code % 8)) & 0x1;
}

static bool looks_like_keyboard(int fd) {
    unsigned char key_bits[(KEY_MAX + 8) / 8];
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    return has_key(key_bits, KEY_A) &&
           has_key(key_bits, KEY_Z) &&
           has_key(key_bits, KEY_1) &&
           has_key(key_bits, KEY_ENTER);
}

static bool is_own_virtual_keyboard(const char *name) {
    return strcmp(name, VIRTUAL_KEYBOARD_NAME) == 0;
}

int scan_keyboards(struct keyboard_info out[MAX_KEYBOARDS]) {
    DIR *dir;
    struct dirent *ent;
    int count = 0;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir /dev/input");
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        int fd;
        char path[PATH_MAX];
        char name[256] = {0};

        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }

        if (count >= MAX_KEYBOARDS) {
            break;
        }

        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        if (!looks_like_keyboard(fd)) {
            close(fd);
            continue;
        }

        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            snprintf(name, sizeof(name), "unknown");
        }
        if (is_own_virtual_keyboard(name)) {
            close(fd);
            continue;
        }

        memset(&out[count], 0, sizeof(out[count]));
        snprintf(out[count].path, sizeof(out[count].path), "%s", path);
        snprintf(out[count].name, sizeof(out[count].name), "%s", name);
        build_cli_name(out[count].name, out[count].cli_name);
        if (ioctl(fd, EVIOCGID, &out[count].id) < 0) {
            memset(&out[count].id, 0, sizeof(out[count].id));
        }

        count++;
        close(fd);
    }

    closedir(dir);
    return count;
}

static bool input_id_equal(const struct input_id *a, const struct input_id *b) {
    return a->bustype == b->bustype &&
           a->vendor == b->vendor &&
           a->product == b->product &&
           a->version == b->version;
}

static void refresh_target_mappings(struct enabled_target *targets,
                                    size_t target_count,
                                    const struct keyboard_info *all,
                                    int all_count) {
    size_t j;
    for (j = 0; j < target_count; j++) {
        int i;
        targets[j].has_id = false;
        for (i = 0; i < all_count; i++) {
            if (strcmp(targets[j].name, all[i].cli_name) == 0) {
                targets[j].id = all[i].id;
                targets[j].has_id = true;
                break;
            }
        }
    }
}

static bool target_matches_keyboard(const struct enabled_target *targets,
                                    size_t target_count,
                                    const struct keyboard_info *kb) {
    size_t i;
    for (i = 0; i < target_count; i++) {
        if (targets[i].has_id) {
            if (input_id_equal(&targets[i].id, &kb->id)) {
                return true;
            }
        } else if (strcmp(targets[i].name, kb->cli_name) == 0) {
            return true;
        }
    }
    return false;
}

void refresh_filter_mappings(struct options *opts,
                             const struct keyboard_info *all,
                             int all_count) {
    refresh_target_mappings(opts->allow, opts->allow_count, all, all_count);
    refresh_target_mappings(opts->deny, opts->deny_count, all, all_count);
}

bool keyboard_allowed(const struct options *opts, const struct keyboard_info *kb) {
    switch (opts->filter_mode) {
        case FILTER_MODE_ALLOW_LIST:
            return target_matches_keyboard(opts->allow, opts->allow_count, kb);
        case FILTER_MODE_DENY_LIST:
            return !target_matches_keyboard(opts->deny, opts->deny_count, kb);
        case FILTER_MODE_ALLOW_ALL:
        default:
            return true;
    }
}

static int open_grabbed_keyboard(const char *path, const char *name) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (ioctl(fd, EVIOCGRAB, (void *)1) < 0) {
        fprintf(stderr, "Failed to grab %s (%s): %s\n", path, name, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void close_grabbed_keyboard_fd(int *fd) {
    if (*fd >= 0) {
        ioctl(*fd, EVIOCGRAB, (void *)0);
        close(*fd);
        *fd = -1;
    }
}

int select_keyboards(const struct options *opts,
                     const struct keyboard_info *all,
                     int all_count,
                     struct selected_keyboard out[MAX_KEYBOARDS]) {
    int i;
    int n = 0;

    for (i = 0; i < all_count; i++) {
        if (!keyboard_allowed(opts, &all[i])) {
            continue;
        }

        out[n].fd = open_grabbed_keyboard(all[i].path, all[i].name);
        if (out[n].fd < 0) {
            continue;
        }
        out[n].info = all[i];
        n++;
    }

    return n;
}

int active_keyboard_count(const struct selected_keyboard *kbs, int count) {
    int i;
    int active = 0;
    for (i = 0; i < count; i++) {
        if (kbs[i].fd >= 0) {
            active++;
        }
    }
    return active;
}

static int find_keyboard_by_path(const struct selected_keyboard *kbs,
                                 int count,
                                 const char *path) {
    int i;
    for (i = 0; i < count; i++) {
        if (kbs[i].fd >= 0 && strcmp(kbs[i].info.path, path) == 0) {
            return i;
        }
    }
    return -1;
}

void release_keyboards(struct selected_keyboard *kbs, int count) {
    int i;
    for (i = 0; i < count; i++) {
        close_grabbed_keyboard_fd(&kbs[i].fd);
    }
}

void disconnect_keyboard(struct selected_keyboard *kbs,
                         struct pollfd *pfds,
                         int index) {
    close_grabbed_keyboard_fd(&kbs[index].fd);
    pfds[index].fd = -1;
    pfds[index].events = 0;
    pfds[index].revents = 0;
}

int attach_keyboard(struct selected_keyboard *kbs,
                    struct pollfd *pfds,
                    int n,
                    const struct keyboard_info *info) {
    int i;
    int fd;

    for (i = 0; i < n; i++) {
        if (kbs[i].fd < 0) {
            break;
        }
    }
    if (i == n) {
        fprintf(stderr, "No free slot to attach keyboard %s (%s)\n",
                info->path, info->name);
        return -1;
    }

    fd = open_grabbed_keyboard(info->path, info->name);
    if (fd < 0) {
        return -1;
    }

    kbs[i].info = *info;
    kbs[i].fd = fd;
    pfds[i].fd = fd;
    pfds[i].events = POLLIN;
    pfds[i].revents = 0;

    fprintf(stderr, "Keyboard attached: %s (%s)\n", info->path, info->name);
    return 0;
}

int refresh_keyboards(struct options *opts,
                      struct selected_keyboard *kbs,
                      struct pollfd *pfds,
                      int n) {
    struct keyboard_info all_keyboards[MAX_KEYBOARDS];
    int all_count;
    int i;

    all_count = scan_keyboards(all_keyboards);
    if (all_count < 0) {
        fprintf(stderr, "Keyboard scan failed; will retry.\n");
        return -1;
    }
    refresh_filter_mappings(opts, all_keyboards, all_count);

    for (i = 0; i < all_count; i++) {
        if (!keyboard_allowed(opts, &all_keyboards[i])) {
            continue;
        }
        if (find_keyboard_by_path(kbs, n, all_keyboards[i].path) >= 0) {
            continue;
        }
        attach_keyboard(kbs, pfds, n, &all_keyboards[i]);
    }

    return 0;
}
