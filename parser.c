#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *value_after_prefix(const char *arg, const char *prefix) {
    size_t n = strlen(prefix);
    if (strncmp(arg, prefix, n) == 0) {
        return arg + n;
    }
    return NULL;
}

void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --list-keyboards\n"
            "  %s -l\n"
            "  %s [--allow=<name> ... | --deny=<name> ...] "
            "[--autoshift-timeout=<ms>] [--disable-hotplug]\n\n"
            "Notes:\n"
            "  --allow and --deny are mutually exclusive.\n"
            "  If neither is provided, all keyboards are allowed.\n",
            argv0, argv0, argv0);
}

static int parse_positive_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!s[0] || !end || *end != '\0' || v <= 0 || v > 60000) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int add_filter_name(struct enabled_target *targets,
                           size_t *count,
                           const char *name,
                           const char *opt_name) {
    struct enabled_target *t;
    if (*count >= MAX_FILTER_IDS) {
        fprintf(stderr, "Too many %s values (max %d)\n", opt_name, MAX_FILTER_IDS);
        return -1;
    }
    t = &targets[*count];
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof(t->name), "%s", name);
    (*count)++;
    return 0;
}

static int add_allow_or_deny(struct options *opts, bool is_allow, const char *value) {
    if (is_allow) {
        return add_filter_name(opts->allow, &opts->allow_count, value, "--allow");
    }
    return add_filter_name(opts->deny, &opts->deny_count, value, "--deny");
}

int parse_args(int argc, char **argv, struct options *opts) {
    int i;

    memset(opts, 0, sizeof(*opts));
    opts->autoshift_timeout_ms = DEFAULT_AUTOSHIFT_TIMEOUT_MS;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value;

        if (strcmp(arg, "--list-keyboards") == 0 || strcmp(arg, "-l") == 0) {
            opts->list_keyboards = true;
            continue;
        }

        if (strcmp(arg, "--disable-hotplug") == 0) {
            opts->disable_hotplug = true;
            continue;
        }

        value = value_after_prefix(arg, "--autoshift-timeout=");
        if (value) {
            if (parse_positive_int(value, &opts->autoshift_timeout_ms) < 0) {
                fprintf(stderr, "Invalid --autoshift-timeout value: %s\n", value);
                return -1;
            }
            continue;
        }

        value = value_after_prefix(arg, "--allow=");
        if (value) {
            if (add_allow_or_deny(opts, true, value) < 0) {
                return -1;
            }
            continue;
        }

        value = value_after_prefix(arg, "--deny=");
        if (value) {
            if (add_allow_or_deny(opts, false, value) < 0) {
                return -1;
            }
            continue;
        }

        if (strcmp(arg, "--allow") == 0 || strcmp(arg, "--deny") == 0) {
            bool is_allow = strcmp(arg, "--allow") == 0;
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value after %s\n", arg);
                return -1;
            }
            if (add_allow_or_deny(opts, is_allow, argv[++i]) < 0) {
                return -1;
            }
            continue;
        }

        fprintf(stderr, "Unknown argument: %s\n", arg);
        return -1;
    }

    if (opts->allow_count > 0 && opts->deny_count > 0) {
        fprintf(stderr, "--allow and --deny are mutually exclusive\n");
        return -1;
    }
    if (opts->allow_count > 0) {
        opts->filter_mode = FILTER_MODE_ALLOW_LIST;
    } else if (opts->deny_count > 0) {
        opts->filter_mode = FILTER_MODE_DENY_LIST;
    } else {
        opts->filter_mode = FILTER_MODE_ALLOW_ALL;
    }

    return 0;
}
