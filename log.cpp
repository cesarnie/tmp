#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <glob.h>
#include <string.h>

#include "log.h"

#define MAX_LOGFILE_CNT (10)

static enum E_LOG_LEVEL loglevel = LL_INFO;

static FILE* fhs[MAX_LOGFILE_CNT];
static int fhs_cnt = 0;

void set_log_level(enum E_LOG_LEVEL l) {
    Logf("set debug level: %d", l);
    loglevel = l;
}

void add_log_file(FILE* fp) {
    if (fp == NULL) {
        return;
    }
    int i;
    for (i = 0; i < fhs_cnt; ++i) {
        if (fhs[i] == fp) {
            return;
        }
    }
    if (fhs_cnt >= MAX_LOGFILE_CNT) {
        return;
    }
    fhs[fhs_cnt] = fp;
    ++fhs_cnt;
}

void init_log(const char* fn, const char * mode) {
    FILE* fh = fopen(fn, mode);
    if (fh==NULL) {
        fprintf(stderr, "init log error: %s\n", fn);
    }
    add_log_file(fh);
}

void rotate_log(const char* wildcard, int dtpos, int days_before) {
    size_t i;
    int flags = 0;
    glob_t results;
    int ret;

    time_t nowT = time(NULL);
    time_t refT = nowT - (24 * 3600 * days_before);
    struct tm refTm = *localtime(&refT);
    char refYyyymmdd[30];
    sprintf(refYyyymmdd, "%04d%02d%02d", refTm.tm_year + 1900, refTm.tm_mon + 1, refTm.tm_mday);

    ret = glob(wildcard, flags, NULL, &results);
    if (ret == 0) {
        for (i = 0; i < results.gl_pathc; i++) {
            char logYyyymmdd[10];
            strncpy(logYyyymmdd, results.gl_pathv[i] + dtpos, 8);
            logYyyymmdd[8] = '\0';
            if (strcmp(logYyyymmdd, refYyyymmdd) < 0) {
                remove(results.gl_pathv[i]);
                Logf("log rotate: remove %s", results.gl_pathv[i]);
            }
        }
    }
    globfree(&results);
}

void remove_log_before(const char* wildcard, int dtpos, int ymd) {
    size_t i;
    int flags = 0;
    glob_t results;
    int ret;

    ret = glob(wildcard, flags, NULL, &results);
    if (ret == 0) {
        for (i = 0; i < results.gl_pathc; i++) {
            char logYyyymmdd[10];
            strncpy(logYyyymmdd, results.gl_pathv[i] + dtpos, 8);
            logYyyymmdd[8] = '\0';
            if (atoi(logYyyymmdd)<ymd) {
                remove(results.gl_pathv[i]);
                Logf("log rotate: remove %s", results.gl_pathv[i]);
            }
        }
    }
    globfree(&results);
}

void write_item(const char* s) {
    int i;
    for (i = 0; i < fhs_cnt; ++i) {
        struct timespec tspec;
        clock_gettime(CLOCK_REALTIME, &tspec);
        struct tm t1 = *localtime(&tspec.tv_sec);
        char buf[20480];
        int sz = sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d.%09ld %s\n",
            t1.tm_year + 1900, t1.tm_mon + 1, t1.tm_mday,
            t1.tm_hour, t1.tm_min, t1.tm_sec, tspec.tv_nsec,
            s);
        fwrite(buf, 1, sz, fhs[i]);
        fflush(fhs[i]);
    }
}

void write_item_raw(const char* s) {
    int i;
    for (i = 0; i < fhs_cnt; ++i) {
        char buf[20480];
        int sz = sprintf(buf, "%s\n", s);
        fwrite(buf, 1, sz, fhs[i]);
        fflush(fhs[i]);
    }
}

void Log(const char* s) {
    if (loglevel < LL_INFO) {
        return;
    }
    write_item(s);
}

void Logf(const char* fmt, ...) {
    if (loglevel < LL_INFO) {
        return;
    }
    char buf[20480];
    va_list args;
    va_start(args, fmt);
    int sz = vsprintf(buf, fmt, args);
    va_end(args);
    write_item(buf);
}

void LogErr(const char* fmt, ...) {
    if (loglevel < LL_INFO) {
        return;
    }
    char buf[20480];
    va_list args;
    va_start(args, fmt);
    int sz = vsprintf(buf, fmt, args);
    va_end(args);
    write_item(buf);
    fprintf(stderr, "%s\n", buf);
}

void Debug(const char* s) {
    if (loglevel < LL_DEBUG) {
        return;
    }
    write_item(s);
}

void Debugf(const char* fmt, ...) {
    if (loglevel < LL_DEBUG) {
        return;
    }
    char buf[20480];
    va_list args;
    va_start(args, fmt);
    int sz = vsprintf(buf, fmt, args);
    va_end(args);
    write_item(buf);
}

void Logb( const char * buf, int sz ) {
    int i;
    char s[4096];  // let go peace with this bug
    int lineno = 0;
    int bytesPerLine = 10;
    const char * prefix = "                         ";
    int head = 0;
    head = sprintf(s, "%s", prefix);
    for (i=0; i<sz; ++i) {        
        if ((i/bytesPerLine)!=lineno) {
            head += sprintf(s+head, "\n");
            write_item_raw( s );
            lineno += 1;
            head = sprintf(s, "%s", prefix);
        }
        head += sprintf(s+head, "%02x ", (unsigned char)buf[i]);
    }
    head += sprintf(s+head, "\n");
    write_item_raw( s );
}

void close_log() {
    int i;
    for (i = 0; i < fhs_cnt; ++i) {
        fclose(fhs[i]);
    }
}
