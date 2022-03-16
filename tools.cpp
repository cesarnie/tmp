#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

int to_ymd(time_t t) {
    struct tm tm = *localtime(&t);
    return (tm.tm_year + 1900) * 10000 +
        (tm.tm_mon + 1) * 100 +
        tm.tm_mday;
}

int to_hms(time_t t) {
    struct tm tm = *localtime(&t);
    return tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
}

int to_yyyymmdd(struct tm tm) {
    return (tm.tm_year + 1900) * 10000 +
        (tm.tm_mon + 1) * 100 +
        tm.tm_mday;
}

int to_intraday_seconds(int hms) {
    return (hms / 10000) * 3600 + ((hms % 10000) / 100) * 60 + (hms % 100);
}

//return truncated string size
int txstr(const char* c0, char* c1, int protLen) {
    memcpy(c1, c0, protLen);
    c1[protLen] = '\0';
    int i;
    for (i = (protLen - 1); i >= 0 && c0[i] == ' '; --i) {
        c1[i] = '\0';
    }
    if (c1[0] == '\0') {
        return 0;
    }
    return i + 1;
}

void txstr_no_trim(const char* c0, char* c1, int protLen) {
    memcpy(c1, c0, protLen);
    c1[protLen] = '\0';
}

void ctrim(char* s) {
    char* p = s;
    int l = strlen(p);

    while (isspace(p[l - 1]))
        p[--l] = 0;
    while (*p && isspace(*p))
        ++p, --l;

    memmove(s, p, l + 1);
}

int split_name_value_pair(char* inbuf, char* name, char* value, char splitchar) {
    char* splits = strchr(inbuf, splitchar);
    if (splits == NULL) {
        return 0;
    }
    *splits = '\0';
    strcpy(name, inbuf);
    ctrim(name);
    strcpy(value, splits + 1);
    ctrim(value);
    return 1;
}

int64_t signed_bcd_to_int64(const char* src_bcd, int num) {
    int i;
    int64_t val = 0;
    for (i = 1; i < num; ++i) {
        val = val * 100 + ((unsigned char)src_bcd[i] >> 4) * 10 + ((unsigned char)src_bcd[i] & 0x0f);
    }
    if (src_bcd[0] == '-') {
        return -val;
    }
    return val;
}

int64_t bcd_to_int64(const char* bcd, int len) {
    int64_t val = 0;
    int i;
    for (i = 0; i < len; ++i) {
        val = val * 100 + ((unsigned char)bcd[i] >> 4) * 10 + ((unsigned char)bcd[i] & 0x0f);
    }
    return val;
}
