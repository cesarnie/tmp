#ifndef __LOGHPP__
#define __LOGHPP__

#include <stdio.h>

enum E_LOG_LEVEL {
    LL_NONE,
    LL_INFO,
    LL_DEBUG
};

void init_log(const char* fn, const char * mode);
void set_log_level( enum E_LOG_LEVEL l );
void add_log_file(FILE* fp);
void rotate_log(const char* wildcard, int dtpos, int days_before);
void remove_log_before(const char* wildcard, int dtpos, int ymd);
void Log(const char* s);
void Logf(const char* fmt, ...);
void LogErr(const char* fmt, ...);
void Debug(const char* s);
void Debugf(const char* fmt, ...);
void close_log();
void Logb( const char * buf, int sz );

#endif
