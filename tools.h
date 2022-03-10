#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <inttypes.h>

int to_ymd(time_t t);
int to_hms(time_t t);
int to_yyyymmdd(struct tm tm);
int to_intraday_seconds(int hms);
int txstr(const char* c0, char* c1, int protLen);
void ctrim(char* s);
int split_name_value_pair(char* inbuf, char* name, char* value);
int64_t signed_bcd_to_int64(const char* src_bcd, int num);
int64_t bcd_to_int64(const char* bcd, int len);
const char* zmq_sockettype_str(int n);
int split_name_value_pair(char* inbuf, char* name, char* value, char splitchar);

#define bcd32(x) (int32_t)(bcd_to_int64((x), (sizeof(x))))
#define bcd64(x) bcd_to_int64((x), (sizeof(x)))
#define sbcd32(x) (int32_t)(signed_bcd_to_int64((x), (sizeof(x))))
#define sbcd64(x) signed_bcd_to_int64((x), sizeof(x))

#endif
