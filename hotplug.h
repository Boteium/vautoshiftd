#ifndef VAUTOSHIFTD_HOTPLUG_H
#define VAUTOSHIFTD_HOTPLUG_H

#include <linux/limits.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>

#include "parser.h"

#define MAX_KEYBOARDS 128
#define VIRTUAL_KEYBOARD_NAME "virtual autoshift keyboard"

struct keyboard_info {
    char path[PATH_MAX];
    char name[256];
    char cli_name[256];
    struct input_id id;
};

struct selected_keyboard {
    struct keyboard_info info;
    int fd;
};

int scan_keyboards(struct keyboard_info out[MAX_KEYBOARDS]);
void refresh_filter_mappings(struct options *opts,
                             const struct keyboard_info *all,
                             int all_count);
bool keyboard_allowed(const struct options *opts, const struct keyboard_info *kb);

int select_keyboards(const struct options *opts,
                     const struct keyboard_info *all,
                     int all_count,
                     struct selected_keyboard out[MAX_KEYBOARDS]);
int active_keyboard_count(const struct selected_keyboard *kbs, int count);
void release_keyboards(struct selected_keyboard *kbs, int count);
void disconnect_keyboard(struct selected_keyboard *kbs,
                         struct pollfd *pfds,
                         int index);
int attach_keyboard(struct selected_keyboard *kbs,
                    struct pollfd *pfds,
                    int n,
                    const struct keyboard_info *info);
int refresh_keyboards(struct options *opts,
                      struct selected_keyboard *kbs,
                      struct pollfd *pfds,
                      int n);

#endif
