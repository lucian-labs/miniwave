/* miniwave — JSON parsing helpers
 *
 * Lightweight key-value extraction from JSON strings.
 * No full parser — just enough for config serialization.
 */

#ifndef MINIWAVE_JSON_HELPERS_H
#define MINIWAVE_JSON_HELPERS_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int json_get_string(const char *json, const char *key, char *out, int max) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    /* Find exact key — must be preceded by { , or whitespace, not a letter */
    while ((p = strstr(p, pattern)) != NULL) {
        if (p == json || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ' || p[-1] == '\n')
            break;
        p += strlen(pattern);
    }
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < max - 1) {
        if (*p == '\\' && *(p + 1)) { p++; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int json_get_int(const char *json, const char *key, int *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') { /* string-encoded number */
        p++;
        *out = atoi(p);
        return 0;
    }
    if ((*p >= '0' && *p <= '9') || *p == '-') {
        *out = atoi(p);
        return 0;
    }
    return -1;
}

static int json_get_float(const char *json, const char *key, float *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
        *out = strtof(p, NULL);
        return 0;
    }
    return -1;
}

static int json_get_iarray_first(const char *json, const char *key, int *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if ((*p >= '0' && *p <= '9') || *p == '-') {
        *out = atoi(p);
        return 0;
    }
    return -1;
}

static int json_get_farray_first(const char *json, const char *key, float *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '[') return -1;
    p++;
    while (*p == ' ') p++;
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
        *out = strtof(p, NULL);
        return 0;
    }
    return -1;
}

#endif /* MINIWAVE_JSON_HELPERS_H */
