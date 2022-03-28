#ifndef __FMTJSON_H__
#define __FMTJSON_H__

int MakeJsonAddTick(struct Vip2AddTick* t, char* buf, size_t buflen);
int MakeJsonUpdateEvent(struct Vip2UpdateEvent* si, char* bufout, size_t bufoutlen);
int MakeJsonUpdateBA(struct Vip2UpdateBA* ba, char* buf, size_t buflen);

#endif
