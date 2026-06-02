#ifndef VAUTOSHIFTD_PARSER_H
#define VAUTOSHIFTD_PARSER_H

#include <linux/input.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_FILTER_IDS 128
#define DEFAULT_AUTOSHIFT_TIMEOUT_MS 175

struct enabled_target {
    char name[256];
    bool has_id;
    struct input_id id;
};

enum filter_mode {
    FILTER_MODE_ALLOW_ALL = 0,
    FILTER_MODE_ALLOW_LIST,
    FILTER_MODE_DENY_LIST
};

struct options {
    bool list_keyboards;
    bool disable_hotplug;
    int autoshift_timeout_ms;
    enum filter_mode filter_mode;
    struct enabled_target allow[MAX_FILTER_IDS];
    size_t allow_count;
    struct enabled_target deny[MAX_FILTER_IDS];
    size_t deny_count;
};

void usage(const char *argv0);
int parse_args(int argc, char **argv, struct options *opts);

#endif
