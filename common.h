#ifndef COMMON_H__
#define COMMON_H__
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define tb_alloc(__s_t) (__s_t *)malloc(sizeof(__s_t))

#define MAX_MSG 250
#define tb_error tb_debug
#define tb_info tb_debug

static void tb_debug(const char *message, ...) {
    va_list ap;
    char xmessage[MAX_MSG];
    char t_s[40];
    time_t t;
    struct tm now;
    t = time(NULL);
    localtime_r(&t, &now);
    strftime(t_s, 40, "%T", &now);
    snprintf(xmessage, MAX_MSG, "%s: %s\n", t_s, message);
    va_start(ap, message);
    vfprintf(stderr, xmessage, ap);
    va_end(ap);
}
#endif
