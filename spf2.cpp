#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <zmq.h>
#include <time.h>
#include <map>
#include <vector>
#include <string>
#include <queue>

#include "log.h"
#include "ringbuffer.h"
#include "spf2types.h"
#include "spf2shm.h"
#include "tools.h"
#include "vip20.h"
#include "fmtjson.h"
#include "spf2.h"

#define SPF2_VER "0.0.5"
#define MAX_URL_LEN 256

#define si2midx(x) ((x) - (gsm->symbols))
#define sr2midx(x) ((x) - (gsm->roots))
#define IS_SPOT(x) (((x)->type_fg) == (0x20))
#define IS_STOCK(x) (((x)->type_fg) == (0x20))
#define IS_WARRANT(x) (((x)->type_fg) == (0x21))
#define IS_INDEX(x) (((x)->type_fg) == (0x10))
#define IS_FUTURE(x) ((((x)->type_fg) & 0x30) == 0x30)
#define IS_OPTION(x) ((((x)->type_fg) & 0x40) == 0x40)
#define IS_SPREAD(x) (((x)->extra_fg) & 0x04)

//#define fchanged(p, mask) ((((*((uint8_t*)p->exist_fg)) & (mask)) == (mask)) && (((*((uint8_t*)p->update_fg)) & (mask)) == (mask)))
//#define fexist(p, mask) (((*((uint8_t*)p->exist_fg)) & (mask)) == (mask))
#define TRADE_CLEAR 0
#define TRADE_OPEN 2
#define TRADE_CLOSE 7

FILE* g_outfp = NULL;
void* g_zmqctx = NULL;
int g_mdc_sock = -1;
void* g_zout1 = NULL;
void* g_zout2 = NULL;
int g_log_rotate_days = 3;
int gSleepKB = 0;
int gRefreshInterval = 300000000;
int g_plain = 0;
char g_outurl1[MAX_URL_LEN];
char g_outurl2[MAX_URL_LEN];
char g_sysname[MAX_URL_LEN];
char g_account[MAX_URL_LEN];
char g_pwd[MAX_URL_LEN];
char g_mdc_addr[MAX_URL_LEN];
char gFtpPath[200];
char g_cfg_fn[MAX_URL_LEN];
FILE* g_sav_fh = NULL;
FILE* g_in_fh = NULL;
simap_t g_simap;                                      // index for symbols
simap_t g_srmap;                                      // index for symbol group
simap_t g_subtbl;                                     // key=exchage;symbolroot;month;type(f/o)
simap_t g_exchg2feed_map;                             // exchange to feed id map
s2smap_t g_THostMap;                                  // key: CME;CL.2103   value: CL.2103
simap_t g_THostSet;                                   // key: CME;S.2010    value: 0: no symbol received, 1: received symbol, value is a future as T Host
simap_t gSymbol2CategoryMap;                          // category is Sinopac specific classification, CME;EW;f/o --> 1  (農產品,金屬,能源...)
s2spairmap_t g_fut_to_spot_map;                       // key="SMX;TWN", value=spot symbol/name pair
std::map<std::string, struct MonthsTable*> gMonTable; // key:"o;CMX;AABC", source:GWHSTM.TXT , first item: o/f
simap_t gCurrMonthMap;                                // key is "exchange;commodity root", source: GWHSTM.TXT
simap_t gFarMonthMap;                                 //
std::vector<std::string> gExchanges;                  //
std::queue<std::string> g_req_exchanges;              //
char g_ibuf[MDC_MAX_SZ];                              //
s2smap_t gAlterCommrootMap;                           //CME;EW;f/o --> ES, 用來map周期貨/選擇權到實際的月商品代碼

static unsigned long dec_tbl[] = {
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
    10000000000,
    100000000000,
    1000000000000
};

static double fdec_tbl[] = {
    1.,
    10.,
    100.,
    1000.,
    10000.,
    100000.,
    1000000.,
    10000000.,
    100000000.,
    1000000000.,
    10000000000.,
    100000000000.,
    1000000000000.
};

void mdc_close(int mdc_sock) {
    close(mdc_sock);
    Logf("disconnect mdc socket");
}

void destroy_app() {
    if (g_mdc_sock != -1) {
        mdc_close(g_mdc_sock);
        g_mdc_sock = -1;
    }
    if (g_zout1) {
        zmq_close(g_zout1);
        g_zout1 = NULL;
    }
    if (g_zout2) {
        zmq_close(g_zout2);
        g_zout2 = NULL;
    }
    if (g_zmqctx) {
        zmq_ctx_destroy(g_zmqctx);
        g_zmqctx = NULL;
    }
    if (g_sav_fh) {
        fclose(g_sav_fh);
        g_sav_fh = NULL;
    }
    spf2shm_destroy();
}

int isSubscribed(struct SymbolInfo* si) {
    char key[128];
    char* abbr = si->root;
    char commtype = 's';
    if (IS_FUTURE(si)) {
        commtype = 'f';
    }
    else if (IS_OPTION(si)) {
        commtype = 'o';
    }
    sprintf(key, "%s;%s;%c", si->exchange, abbr, commtype);
    //if (0) {
    //    std::map<std::string, int>::iterator excluiter = gExcludeSymbols.find(key);
    //    if (excluiter != gExcludeSymbols.end()) {
    //        return 0;
    //    }
    //}
    //bm
    if (IS_SPREAD(si)) {
        char* tok;
        char fullsymb[64];
        strcpy(fullsymb, si->symbol);
        char symb[12];
        char mon1[8];
        char mon2[8];
        tok = strtok(fullsymb, ".");
        if (tok != NULL) {
            strcpy(symb, tok);
        }
        else {
            return 0;
        }
        tok = strtok(NULL, "/");
        if (tok != NULL) {
            strcpy(mon1, tok);
        }
        else {
            return 0;
        }
        tok = strtok(NULL, "/");
        if (tok != NULL) {
            strcpy(mon2, tok);
        }
        else {
            return 0;
        }

        char leg[128];
        sprintf(leg, "%s;%s;20%s;f", si->exchange, abbr, mon1);
        simap_t::iterator finditer = g_subtbl.find(leg);
        if (finditer == g_subtbl.end()) {
            return 0;
        }
        sprintf(leg, "%s;%s;20%s;f", si->exchange, abbr, mon2);
        finditer = g_subtbl.find(leg);
        if (finditer == g_subtbl.end()) {
            return 0;
        }
    }
    else if (IS_FUTURE(si)) {
        sprintf(key, "%s;%s;%d;f", si->exchange, abbr, si->duemon);
        simap_t::iterator finditer = g_subtbl.find(key);
        if (finditer == g_subtbl.end()) {
            return 0;
        }
    }
    else if (IS_OPTION(si)) {
        sprintf(key, "%s;%s;%d;o", si->exchange, abbr, si->duemon);
        simap_t::iterator finditer = g_subtbl.find(key);
        if (finditer == g_subtbl.end()) {
            return 0;
        }
    }
    else if (IS_INDEX(si)) {
        sprintf(key, "%s;%s;%d;s", si->exchange, abbr, 999999);
        simap_t::iterator finditer = g_subtbl.find(key);
        if (finditer == g_subtbl.end()) {
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

// exchgcomm : "o;CMX;AABC"
void addMonsTableYMEntry(const char* exchgcomm, int yyyymm) {
    std::map<std::string, struct MonthsTable*>::iterator iter = gMonTable.find(exchgcomm);
    if (iter != gMonTable.end()) {
        if (iter->second->count >= 20) {
            return;
        }
        iter->second->mons[iter->second->count] = yyyymm;
        iter->second->count += 1;
    }
    else {
        MonthsTable* item = new MonthsTable;
        memset(item->mons, 0x00, sizeof(int) * 20);
        item->mons[0] = yyyymm;
        item->count = 1;
        gMonTable[exchgcomm] = item;
    }
}

// commroot : "f/o;CMX;AABC"
MonthsTable* getMonthsTableEntry(const char* commroot) {
    std::map<std::string, MonthsTable*>::iterator iter = gMonTable.find(commroot);
    if (iter != gMonTable.end()) {
        return iter->second;
    }
    return NULL;
}

void addCurrentMonthEntry(const char* exchgcomm, int deliveryYM) {
    simap_t::iterator fiter = gCurrMonthMap.find(exchgcomm);
    if (fiter == gCurrMonthMap.end()) {
        gCurrMonthMap[exchgcomm] = deliveryYM;
    }
    else {
        if (deliveryYM < fiter->second) {
            gCurrMonthMap[exchgcomm] = deliveryYM;
        }
    }
}

//CBT;C     ;202112;20210629;20211214;20211110;20211214;20211127  (GWHSTM.TXT)
//CBT;S     ;202204;20210428;20220325;20220325;20220325;20220326;1CBTS     ;202205  (MHSMB.TXT)
void LoadMonthTable(char sectype, const char* fn) {
    FILE* pf = fopen(fn, "rt");
    if (pf == NULL) {
        LogErr("cannot open file %s", fn);
        exit(1);
    }

    char todaystr[64];
    time_t nowt = time(NULL);
    struct tm ltime = *localtime(&nowt);
    sprintf(todaystr, "%04d%02d%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday);

    char linebuf[200];
    char exchgbuf[20];
    char commrootbuf[64];
    char deliveryYM[64];
    char lastTradeDate[64];
    while (fgets(linebuf, 200, pf) != NULL) {
        size_t linesz = strlen(linebuf);
        if (linesz > 0 && linebuf[0] == '#') {
            continue;
        }
        if (linebuf[linesz - 1] == '\n') {
            linebuf[linesz - 1] = '\0';
        }
        char* tok;
        tok = strtok(linebuf, ";"); //CBT
        if (tok != NULL) {
            strcpy(exchgbuf, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //C
        if (tok != NULL) {
            strcpy(commrootbuf, tok);
            ctrim(commrootbuf);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //202112
        if (tok != NULL) {
            strcpy(deliveryYM, tok);
            ctrim(deliveryYM);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok == NULL) {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok == NULL) {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok == NULL) {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok != NULL) {
            strcpy(lastTradeDate, tok);
            ctrim(lastTradeDate);
            if (strcmp(todaystr, lastTradeDate) > 0) {
                continue;
            }
        }
        else {
            continue;
        }
        char tmpOptMapFut[64];       //1HKFHCEI
        char optMapFut[32];          //HCEI, symbol root
        char optMapFutDueMonStr[64]; //202108
        tmpOptMapFut[0] = 0;
        optMapFut[0] = 0;
        optMapFutDueMonStr[0] = 0;
        if (sectype == 'o') {
            tok = strtok(NULL, ";");
            if (tok == NULL) {
                continue;
            }
            tok = strtok(NULL, ";"); //1CBTTY
            if (tok == NULL) {
                continue;
            }
            else {
                strcpy(tmpOptMapFut, tok);
                ctrim(tmpOptMapFut);
                int tmpOptMapFutLen = strlen(tmpOptMapFut);
                strncpy(optMapFut, tmpOptMapFut + 4, tmpOptMapFutLen - 4);
                optMapFut[tmpOptMapFutLen - 4] = 0;
            }
            tok = strtok(NULL, ";"); //202112
            if (tok == NULL) {
                continue;
            }
            else {
                strcpy(optMapFutDueMonStr, tok);
                ctrim(optMapFutDueMonStr);
            }
        }
        char typecommroot[200];
        sprintf(typecommroot, "%c;%s;%s", sectype, exchgbuf, commrootbuf);
        int deliveryYMInt = atoi(deliveryYM);
        addMonsTableYMEntry(typecommroot, deliveryYMInt);
        char commroot[100]; //CME;ES
        sprintf(commroot, "%s;%s", exchgbuf, commrootbuf);
        if (sectype == 'f') {
            addCurrentMonthEntry(commroot, deliveryYMInt);
        }
        else if (sectype == 'o') {
            char thoststr[64]; //ES.yymm
            sprintf(thoststr, "%s.%s", optMapFut, optMapFutDueMonStr + 2);
            char thostkey[128]; //CME;ES;2207
            sprintf(thostkey, "%s.%d", commroot, deliveryYMInt % 10000);
            g_THostMap[thostkey] = thoststr;
            char tmpbuf2[100];
            sprintf(tmpbuf2, "%s;%s", exchgbuf, thoststr);
            g_THostSet[tmpbuf2] = 0;
            Logf("default T host for %s is %s", thostkey, tmpbuf2);
        }
    }
    fclose(pf);
}

void LoadSpotTxt(const char* fn) {
    FILE* pf = fopen(fn, "rt");
    if (pf == NULL) {
        LogErr("cannot open %s", fn);
        return;
    }

    char linebuf[256];
    struct SpotItem rec;
    while (fgets(linebuf, 256, pf) != NULL) {
        ctrim(linebuf);
        char* tok;
        tok = strtok(linebuf, ";");
        if (tok != NULL) {
            ctrim(tok);
            strcpy(rec.exchange, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok != NULL) {
            ctrim(tok);
            strcpy(rec.cls, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok != NULL) {
            ctrim(tok);
            strcpy(rec.symbol, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok != NULL) {
            ctrim(tok);
            strcpy(rec.name, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";");
        if (tok != NULL) {
            ctrim(tok);
            strcpy(rec.future, tok);
        }
        else {
            continue;
        }

        //struct SpotItem * insertRec = (struct SpotItem*)malloc(sizeof(struct SpotItem));
        //memcpy(insertRec, &rec, sizeof(struct SpotItem));
        //spot_vec.push_back(insertRec);

        struct StringPair spair;
        spair.str1 = rec.symbol; // symbol code of spot
        spair.str2 = rec.name;   // symbol name of spot
        char keybuf[64];
        sprintf(keybuf, "%s;%s", rec.exchange, rec.future);
        g_fut_to_spot_map[keybuf] = spair;
        Logf("add g_fut_to_spot_map[%s]=%s,%s", keybuf, spair.str1.c_str(), spair.str2.c_str());

        char subkey[128];
        sprintf(subkey, "%s;%s;%d;s", rec.exchange, rec.symbol, 999999);
        g_subtbl[subkey] = 1;
    }
    fclose(pf);
}

//2;CME;EW    ;Mini S&P月底選擇權;EWE         ;USD;1;1CMEES    ;CMEEI
void LoadSubscribeTableAPEXHSTB() {
    char fn[300];
    sprintf(fn, "%s/APEXHSTB.TXT", gFtpPath);
    FILE* pf = fopen(fn, "rt");
    if (pf == NULL) {
        fprintf(stderr, "cannot open file %s\n", fn);
        return;
    }
    char sectype;
    char linebuf[256];
    char commtype[3];
    char exchgbuf[64];
    char commrootbuf[64];
    char chtname[64];
    char unknown1[64];
    char currency[64];
    char commcategory[64];
    char alter_commroot[64];
    char sessiontype[64];
    while (fgets(linebuf, 256, pf) != NULL) {
        size_t linesz = strlen(linebuf);
        if (linesz > 0 && linebuf[0] == '#') {
            continue;
        }
        if (linebuf[linesz - 1] == '\n') {
            linebuf[linesz - 1] = '\0';
        }
        char* tok;
        tok = strtok(linebuf, ";"); //2
        if (tok != NULL) {
            strcpy(commtype, tok);
            ctrim(commtype);
            if (commtype[0]=='1') {
                sectype = 'f';
            }
            else if (commtype[0]=='2') {
                sectype = 'o';
            }
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //CME
        if (tok != NULL) {
            strcpy(exchgbuf, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //EW
        if (tok != NULL) {
            strcpy(commrootbuf, tok);
            ctrim(commrootbuf);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //Mini S&P月底選擇權
        if (tok != NULL) {
            strcpy(chtname, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //EWE
        if (tok != NULL) {
            strcpy(unknown1, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //USD
        if (tok != NULL) {
            strcpy(currency, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //1
        if (tok != NULL) {
            strcpy(commcategory, tok);
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //1CMEES
        if (tok != NULL) {
            strcpy(alter_commroot, tok);
            ctrim(alter_commroot); 
        }
        else {
            continue;
        }
        tok = strtok(NULL, ";"); //CMEEI
        if (tok != NULL) {
            strcpy(sessiontype, tok);
            ctrim(sessiontype);
        }
        else {
            continue;
        }
//bm
        char exchgcomm[200];
        char typeexchgcomm[200];
        sprintf(exchgcomm, "%c;%s;%s", sectype, exchgbuf, commrootbuf);
        sprintf(typeexchgcomm, "%s;%s;%s", commtype, exchgbuf, commrootbuf);
        if (strlen(sessiontype) > 0) {
            MonthsTable* mtbl = getMonthsTableEntry(exchgcomm);
            if (mtbl != NULL) {
                for (int i = 0; i < mtbl->count; ++i) {
                    char fullsubitem[200];
                    if (strcmp(commtype, "1") == 0) {
                        sprintf(fullsubitem, "%s;%s;%d;f", exchgbuf, commrootbuf, mtbl->mons[i]);
                        g_subtbl[fullsubitem] = 1;
                    }
                    else if (strcmp(commtype, "2") == 0) {
                        sprintf(fullsubitem, "%s;%s;%d;o", exchgbuf, commrootbuf, mtbl->mons[i]);
                        g_subtbl[fullsubitem] = 1;
                    }
                }
            }
        }
        if (strlen(alter_commroot) > 0) {
            char commtype2 = (commtype[0] == '1') ? 'f' : 'o';
            char typeexchgcomm2[200];
            sprintf(typeexchgcomm2, "%s;%s;%c", exchgbuf, commrootbuf, commtype2);
            gAlterCommrootMap[typeexchgcomm2] = alter_commroot + 4;
        }
        gSymbol2CategoryMap[typeexchgcomm] = atol(commcategory);
    }
    fclose(pf);
}

//bm
int ConvertToApxTradeSessionStatus(int state) {
    switch (state) {
    case 2:
        return TRADE_CLEAR;
    case 3:
        return TRADE_OPEN;
    case 4:
        return TRADE_CLOSE;
    }
    return TRADE_OPEN;
}

void sigint_handler(int sig) {
    signal(SIGINT, SIG_DFL);
    Logf("catch SIGINT");
    destroy_app();
    exit(0);
}

void print_version() {
    fprintf(stderr, "spf2 %s\n", SPF2_VER);
}

void print_help() {
    const char* ident = "  ";
    print_version();
    fprintf(stderr, "%s%s\n", ident, "h --help: print help message");
}

const char* zmq_sockettype_str(int n) {
    switch (n) {
    case ZMQ_PAIR:
        return "pair";
    case ZMQ_PUB:
        return "pub";
    case ZMQ_SUB:
        return "sub";
    case ZMQ_REQ:
        return "request";
    case ZMQ_REP:
        return "reply";
    case ZMQ_DEALER:
        return "dealer";
    case ZMQ_ROUTER:
        return "router";
    case ZMQ_PULL:
        return "pull";
    case ZMQ_PUSH:
        return "push";
    case ZMQ_XPUB:
        return "xpub";
    case ZMQ_XSUB:
        return "xsub";
    case ZMQ_STREAM:
        return "stream";
    }
    return "";
}

void* open_zmq_out(const char* addr, int mode, int isbind) {
    if (strlen(addr)==0) {
        return NULL;
    }
    void* ret = zmq_socket(g_zmqctx, mode);
    if (!ret) {
        Logf("error in zmq_socket: %s, mode=%d", zmq_strerror(errno), mode);
        return NULL;
    }

    int rate = 100000000;
    int rc = zmq_setsockopt(ret, ZMQ_RATE, &rate, sizeof(rate));
    if (rc != 0) {
        Logf("error in zmq_setsockopt: %s", zmq_strerror(errno));
        zmq_close(ret);
        return NULL;
    }

    if (isbind) {
        rc = zmq_bind(ret, addr);
    }
    else {
        rc = zmq_connect(ret, addr);
    }
    if (rc != 0) {
        Logf("error in zmq_%s: %s, addr=%s", isbind ? "bind" : "connect", zmq_strerror(errno), addr);
        zmq_close(ret);
        return NULL;
    }
    Logf("zmq out socket mode=%s, %s, %s", zmq_sockettype_str(mode), isbind ? "bind" : "connect", addr);
    return ret;
}

// returns socket open
// if open socket error, return -1
int mdc_open(const char* ip, const char* service) {
    int socketRet = -1;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0x00, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;
    int addRet = getaddrinfo(ip, service, &hints, &result);
    if (addRet != 0) {
        LogErr("getaddrinfo err: %s(%s:%s)", gai_strerror(addRet), ip, service);
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        char ipaddr[20];
        inet_ntop(rp->ai_family, &((struct sockaddr_in*)rp->ai_addr)->sin_addr, ipaddr, 20);
        if (1) {
            Logf("try connection: ip=%s, port=%d", ipaddr, ntohs(((struct sockaddr_in*)rp->ai_addr)->sin_port));
        }

        socketRet = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketRet == -1) {
            LogErr("socket err: family=%d, socket_type=%d, protocol=%d", rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            continue;
        }
        if (connect(socketRet, rp->ai_addr, rp->ai_addrlen) == -1) {
            close(socketRet);
            LogErr("connect err: %d(%s)", errno, strerror(errno));
            continue;
        }
        else {
            Logf("connected to %s:%d", ipaddr, ntohs(((struct sockaddr_in*)rp->ai_addr)->sin_port));
            break;
        }
    }
    freeaddrinfo(result);

    if (rp == NULL) { // No address succeeded
        return -1;
    }
    return socketRet;
}

void int64_to_bcd(uint64_t data, char* buffer, int num) {
    uint64_t c_value = (uint64_t)data;
    int i;
    for (i = num - 1; i >= 0; i--) {
        uint64_t cut = c_value % 100L;
        buffer[i] = ((cut / 10) << 4) + (cut % 10);
        c_value = c_value / 100L;
    }
}

int mdc_send(char* data, size_t sz) {
    write(g_mdc_sock, data, sz);
    if (g_sav_fh) {
        fwrite(data, sz, 1, g_sav_fh);
        fflush(g_sav_fh);
    }
    return 0;
}

int mdc_req_login(int ver, const char* sysname, const char* acc, const char* pwd) {
    struct m2cl_login p;
    memset(&p, 0x00, sizeof(m2cl_login));
    p.head.begin = 0xff;
    int64_to_bcd(51, p.head.fmt, sizeof(p.head.fmt));
    int64_to_bcd(1, p.head.ver, sizeof(p.head.ver));
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int hms = to_ymd(tp.tv_sec);
    int64_to_bcd(hms * 10000 + tp.tv_nsec / 100000, p.head.time, sizeof(p.head.time));
    int64_to_bcd(sizeof(struct m2cl_login) - sizeof(struct m2_head), p.head.len, sizeof(p.head.len));
    int64_to_bcd(1, p.ver, sizeof(p.ver));
    fmt_padded_str(p.sysname, sysname, (int)sizeof(p.sysname));
    //sprintf(p.sysname, "%s", sysname);
    fmt_padded_str(p.acc, acc, (int)sizeof(p.acc));
    fmt_padded_str(p.pwd, pwd, (int)sizeof(p.pwd));

    Logf("send login: ver=%d, sys=[%s], acc=[%s], pwd=[%s]", 1, p.sysname, p.acc, p.pwd);

    mdc_send((char*)&p, sizeof(p));
    return 0;
}

char g_plain_head[256];

const char* fmt_plain_head(struct m2_head* head) {
    int64_t t = bcd64(head->time);
    sprintf(g_plain_head, "mdc %d,%d,%02ld:%02ld:%02ld.%04ld",
        bcd32(head->fmt),
        bcd32(head->ver),
        t / 100000000,
        (t % 100000000) / 1000000,
        (t % 1000000) / 10000,
        t % 10000);
    return g_plain_head;
}

void dump_client_symbol_request(struct m2cl_symbol* p) {
    char exchange[13];
    txstr_no_trim(p->exchange, exchange, 12);
    Logf("%s req symbol: exchange=%s", fmt_plain_head(&p->head), exchange);
}

int mdc_req_symbol(const char* exchange) {
    struct m2cl_symbol p;
    p.head.begin = 0xff;
    int64_to_bcd(52, p.head.fmt, 1);
    int64_to_bcd(1, p.head.ver, 1);
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int hms = to_ymd(tp.tv_sec);
    int64_to_bcd(hms * 10000 + tp.tv_nsec / 100000, p.head.time, sizeof(p.head.time));
    int64_to_bcd(sizeof(struct m2cl_symbol) - sizeof(struct m2_head), p.head.len, sizeof(p.head.len));

    fmt_padded_str(p.exchange, exchange, (int)sizeof(p.exchange));
    dump_client_symbol_request(&p);

    mdc_send((char*)&p, sizeof(p));
    return 0;
}

int mdc_req_mktdata(char cmd, int feedid, int64_t seqno) {
    struct m2cl_req_quote p;
    p.head.begin = 0xff;
    int64_to_bcd(53, p.head.fmt, 1);
    int64_to_bcd(1, p.head.ver, 1);
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int hms = to_ymd(tp.tv_sec);
    int64_to_bcd(hms * 10000 + tp.tv_nsec / 100000, p.head.time, sizeof(p.head.time));
    int64_to_bcd(sizeof(struct m2cl_req_quote) - sizeof(struct m2_head), p.head.len, sizeof(p.head.len));

    p.cmd = cmd;
    int64_to_bcd(feedid, p.feedid, 1);
    int64_to_bcd(seqno, p.start_seqno, 8);

    mdc_send((char*)&p, sizeof(p));
    return 0;
}

void dump_symbol_root_request(struct m2cl_symbol_root* p) {
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    Logf("%s req symbol root: exchange=%s", fmt_plain_head(&p->head), exchange);
}

int mdc_req_symbol_root(const char* exchange) {
    struct m2cl_symbol_root p;
    p.head.begin = 0xff;
    int64_to_bcd(56, p.head.fmt, 1);
    int64_to_bcd(1, p.head.ver, 1);
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    int hms = to_ymd(tp.tv_sec);
    int64_to_bcd(hms * 10000 + tp.tv_nsec / 100000, p.head.time, sizeof(p.head.time));
    int64_to_bcd(sizeof(struct m2cl_symbol) - sizeof(struct m2_head), p.head.len, sizeof(p.head.len));
    fmt_padded_str(p.exchange, exchange, (int)sizeof(p.exchange));

    mdc_send((char*)&p, sizeof(p));
    dump_symbol_root_request(&p);
    return 0;
}

int on_mdc_connect() {
    mdc_req_login(1, g_sysname, g_account, g_pwd);
    return 0;
}

int mdc_reconnect(const char* addr) {
    char addrbuf[MAX_URL_LEN];
    strcpy(addrbuf, addr);

    char ip[64];
    char service[24];
    split_name_value_pair(addrbuf, ip, service, ':');

    int sock = -1;
    while (sock == -1) {
        sock = mdc_open(ip, service);
        sleep(1);
    }
    return sock;
}

int mdc_read_stdin(FILE* fp, ringbuf_t* ring) {
    char inputbuf[1024];
    size_t readcnt = fread(inputbuf, 1, 1024, fp);
    if (readcnt == 0) {
        return -1;
    }
    size_t writecnt = ring_write(inputbuf, readcnt, ring);
    if (writecnt < readcnt) {
        Logf("err: ring buffer full on write");
    }
    return (int)writecnt;
}

// returns -1 if error, in this case, we should try to reconnect
// otherwise, return number of bytes read
int mdc_read_async(int mdc_sock, ringbuf_t* ring) {
    char inputBuf[1024];
    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(mdc_sock, &readfds);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int selRet, readCnt;
    selRet = select(mdc_sock + 1, &readfds, NULL, NULL, &timeout);
    if (selRet == 0) {
        return 0;
    }
    else if (selRet == -1) {
        Logf("select err: %d(%s)", errno, strerror(errno));
        return 0;
    }
    readCnt = read(mdc_sock, inputBuf, 1024);
    if (readCnt == -1) {
        if (errno == EINTR) {
            return 0;
        }
        else {
            LogErr("read socket err: %d(%s)", errno, strerror(errno));
            return -1;
        }
    }
    else if (readCnt == 0) { //socket closed
        return -1;
    }
    size_t writecnt = ring_write(inputBuf, readCnt, ring);
    if ((int)writecnt < readCnt) {
        Logf("err: ring buffer full on write");
    }
    return writecnt;
}

int read_args(int argc, char** argv) {
    int c;
    int option_index = 0;
    time_t now;
    struct option long_options[] = {
        { "help", no_argument, 0, 'h' },
        { "stdout", no_argument, 0, 'o' },
        { "saveio", no_argument, 0, 's' },
        { "stdin", no_argument, 0, 'i' },
        { "plain", no_argument, 0, 'p' },
        { "version", no_argument, 0, 'v' },
        // { "delete", required_argument, 0, 0 },
        // { "verbose", no_argument, 0, 0 },
        // { "create", required_argument, 0, 'c' },
        // { "file", required_argument, 0, 0 },
        { 0, 0, 0, 0 }
    };
    while (1) {
        c = getopt_long(argc, argv, "vhosip", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 0:
            break;
        case '?':
            LogErr("unknown usage");
            print_help();
            break;
        case 'h':
            print_help();
            exit(0);
            break;
        case 'v':
            print_version();
            exit(0);
            break;
        case 'o':
            g_outfp = stdout;
            Logf("redirect to stdout");
            break;
        case 's':
            now = time(NULL);
            char fn[64];
            sprintf(fn, "io_%d_%d.sav", to_ymd(now), to_hms(now));
            g_sav_fh = fopen(fn, "wb");
            break;
        case 'i':
            g_in_fh = stdin;
            break;
        case 'p': // plain
            g_plain = 1;
            break;
        default:
            LogErr("unknown arg %c", c);
            print_help();
            break;
        }
    }
    if (optind < argc) {
        sprintf(g_cfg_fn, "%s", argv[optind]);
        Logf("config file: %s", g_cfg_fn);
    }
    return 0;
}

int mdc_parse_heartbeat(const char* buf, size_t sz) {
    struct m2_heartbeat* p = (struct m2_heartbeat*)buf;
    if (g_plain) {
        Logf("%s heartbeat", fmt_plain_head(&p->head));
    }
    return 0;
}

struct SymbolRootInfo* get_commroot_info(const char* root, const char* exchange, char sectype) {
    char rootkey[36];
    sprintf(rootkey, "%s;%s;%c", exchange, root, sectype);
    simap_t::iterator it = g_srmap.find(rootkey);
    if (it != g_srmap.end()) {
        return gsm->roots + it->second;
    }
    return NULL;
}

const char* getCategoryNameById(int id) {
    switch (id) {
    case 1:
        return "指數";
    case 2:
        return "利率";
    case 3:
        return "匯率";
    case 4:
        return "債券";
    case 5:
        return "個股";
    case 6:
        return "農產品";
    case 7:
        return "金屬";
    case 8:
        return "能源";
    case 9:
        return "其他";
    case 100:
        return "現貨";
    }
    return "未歸類";
}

const char* convert_to_apex_symbol_type(char fg) {
    switch (fg) {
    case 0x10: // index
        return "IX";
    case 0x20: // common stock
        return "CS";
    case 0x21: // warrant
        return "WR";
    case 0x30: // commodity future
        return "CF";
    case 0x31: // index future
        return "NF";
    case 0x32: // stock future
        return "SF";
    case 0x33: // interest future
        return "IF";
    case 0x34: // bond future
        return "BF";
    case 0x35: // currency future
        return "XF";
    case 0x40: // index option
        return "NO";
    case 0x41: // stock option
        return "SO";
    case 0x42: // future  option
        return "FO";
    case 0x43: // bond option
        return "BO";
    case 0x44: // currency option
        return "XO";
    case 0x45: // interest option
        return "IO";
    }
    return "  ";
}

const char* ConvertToMIC(const char* exchg) {
    return exchg;
}

void FillTimeString(char* buf, int buflen) {
    time_t t0 = time(NULL);
    struct tm* t1 = localtime(&t0);
    strftime(buf, buflen, "%Y%m%d_%H:%M:%S.000", t1);
}

void convertOptionCode(const char* c0, char* c1) {
    const char* dashPos = strchr(c0, '/');
    if (dashPos == NULL) {
        strcpy(c1, c0);
        return;
    }
    char left[20];
    char right[20];
    char commroot[20];
    strncpy(left, c0, dashPos - c0); // YM.1903
    left[dashPos - c0] = '\0';
    strncpy(right, dashPos + 1, 19); // 1906

    dashPos = strchr(c0, '.');
    if (dashPos == NULL) {
        strcpy(c1, c0);
        return;
    }
    strncpy(commroot, c0, dashPos - c0);
    commroot[dashPos - c0] = '\0';
    sprintf(c1, "%s/%s.%s", left, commroot, right);
}

void make_symbol_update_event(struct SymbolInfo* si, struct Vip2UpdateEvent* ue) {
    ue->Mode = 57;
    ue->Version = 0;

    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);
    strcpy(ue->FilterCol, "SYMBO");
    sprintf(ue->FilterVal, "%s.%s", newSymbolCode, "US");
    FillTimeString(ue->EventTime, JsonValLen);
    ue->EventType = 4;
    int cidx = 0;
    strcpy(ue->ColList[cidx].ColName, "527"); // country
    strcpy(ue->ColList[cidx].ColVal, "US");
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "17"); // exchangecode
    strcpy(ue->ColList[cidx].ColVal, ConvertToMIC(si->exchange));
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "48"); // symbol
    sprintf(ue->ColList[cidx].ColVal, "%s.%s", newSymbolCode, "US");
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "RegularizeCode");
    sprintf(ue->ColList[cidx].ColVal, "%s.%s", newSymbolCode, "US");
    ++cidx;

    strcpy(ue->ColList[cidx].ColName, "20113"); // decimals
    sprintf(ue->ColList[cidx].ColVal, "%d", si->decimals);
    ++cidx;

    strcpy(ue->ColList[cidx].ColName, "1151"); // type
    sprintf(ue->ColList[cidx].ColVal, "%c", si->apex_symbol_type);
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "55"); // engname
    ue->ColList[cidx].ColVal[0] = '\0';
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "30153"); // chtname
    sprintf(ue->ColList[cidx].ColVal, "%s", si->name);
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "62"); // expire

    struct tm t2;
    t2.tm_sec = 0;
    t2.tm_min = 0;
    t2.tm_hour = 6;
    t2.tm_mday = si->end_date % 100;
    t2.tm_mon = ((si->end_date / 100) % 100) - 1;
    t2.tm_year = si->end_date / 10000 - 1900;
    time_t t3 = mktime(&t2);
    time_t t4 = t3 + 24 * 3600;
    struct tm* t5 = localtime(&t4);

    strftime(ue->ColList[cidx].ColVal, JsonValLen, "%Y%m%d_%H:%M:%S.000", t5);
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "200"); // mature month
    sprintf(ue->ColList[cidx].ColVal, "%d", si->duemon);
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "205"); // mature date
    sprintf(ue->ColList[cidx].ColVal, "%02d", si->end_date % 100);
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "1227");

    char exchgcomm[64];
    sprintf(exchgcomm, "%s;%s", si->exchange, si->root);
    strcpy(ue->ColList[cidx].ColVal, getCategoryNameById(si->category_id));
    ++cidx;
    if (IS_OPTION(si)) {
        strcpy(ue->ColList[cidx].ColName, "201");
        if (si->extra_fg & 0x01) {
            strcpy(ue->ColList[cidx].ColVal, "C");
        }
        else if (si->extra_fg & 0x02) {
            strcpy(ue->ColList[cidx].ColVal, "P");
        }
        ++cidx;
        strcpy(ue->ColList[cidx].ColName, "202");

        sprintf(ue->ColList[cidx].ColVal, "%s", si->strike_price);
        ++cidx;
        strcpy(ue->ColList[cidx].ColName, "20114"); // strike price decimals
        sprintf(ue->ColList[cidx].ColVal, "%d", 0);
        ++cidx;
        strcpy(ue->ColList[cidx].ColName, "12024");
        sprintf(ue->ColList[cidx].ColVal, "%s", si->root_ext);
        ++cidx;

        strcpy(ue->ColList[cidx].ColName, "12026");
        sprintf(ue->ColList[cidx].ColVal, "%s.%d", si->root_ext, si->duemon % 10000);
        ++cidx;

        strcpy(ue->ColList[cidx].ColName, "20112");
        std::string thost;
        char findkey[128];
        sprintf(findkey, "%s.%04d", exchgcomm, si->duemon % 10000);
        std::map<std::string, std::string>::iterator fiter = g_THostMap.find(findkey);
        if (fiter != g_THostMap.end()) {
            thost = fiter->second;
        }
        else {
            Logf("thostmap for %s not found", findkey);
        }
        if (thost.size() == 0) {
            ue->ColList[cidx].ColVal[0] = 0;
        }
        else {
            sprintf(ue->ColList[cidx].ColVal, "%s.US", thost.c_str());
        }
        ++cidx;

        strcpy(ue->ColList[cidx].ColName, "20122");
        strcpy(ue->ColList[cidx].ColVal, "");
        ++cidx;
    }
    if (IS_FUTURE(si)) {
        strcpy(ue->ColList[cidx].ColName, "20122");
        //todo
        if (strlen(si->near_alias) > 0) {
            sprintf(ue->ColList[cidx].ColVal, "%s.%s", si->near_alias, "US");
        }
        else if (strlen(si->far_alias) > 0) {
            sprintf(ue->ColList[cidx].ColVal, "%s.%s", si->far_alias, "US");
        }
        else {
            ue->ColList[cidx].ColVal[0] = '\0';
        }
        ++cidx;
    }
    if (IS_SPREAD(si)) {
        struct SymbolRootInfo* pRoot = get_commroot_info(si->root_ext, si->exchange, si->apex_symbol_type);
        if (pRoot != NULL) {
            strcpy(ue->ColList[cidx].ColName, "20123");
            sprintf(ue->ColList[cidx].ColVal, "%d", (pRoot->extension) ? 1 : 0);
            ++cidx;
        }
    }
    strcpy(ue->ColList[cidx].ColName, "TM");
    if (si->category_id == 100) {
        sprintf(ue->ColList[cidx].ColVal, "%d", 0);
    }
    else {
        sprintf(ue->ColList[cidx].ColVal, "%d", 1);
    }
    ++cidx;
    strcpy(ue->ColList[cidx].ColName, "12032");
    if (si->category_id == 100) {
        sprintf(ue->ColList[cidx].ColVal, "%d", 0);
    }
    else {
        sprintf(ue->ColList[cidx].ColVal, "%d", 1);
    }
    ++cidx;
    ue->ColListCnt = cidx;
}

int emd_send(const char* buf, size_t len, int flags) {
    /*
    struct timespec tp;
    if (bandwidth_control) {
        clock_gettime(CLOCK_REALTIME, &tp);
        double difft = diff_timespec(&bandwidth_timeout, &tp);
        if (difft<0.0) {   // timeout
            bandwidth_timeout.tv_sec++;
            current_bandwidth = len;
        }
        else {   // not timeout
            if (current_bandwidth<(max_kbytes_per_sec*1024)) {
                current_bandwidth += len;
            }
            else {
                struct timespec sleeptp;
                sleeptp.tv_sec = 0;
                sleeptp.tv_nsec = difft*1.0e9;
                nanosleep(&sleeptp, NULL);
                Logf("sleep %d nanoseconds", sleeptp.tv_nsec);
                current_bandwidth = len;
                bandwidth_timeout.tv_sec++;
            }
        }
    }
    */
    int ret;
    if (g_outfp) {
        ret = fwrite(buf, 1, len, g_outfp);
        ret += fwrite("\n", 1, 1, g_outfp);
        fflush(g_outfp);
    }
    if (g_zout1 != NULL) {
        ret = zmq_send(g_zout1, buf, len, flags);
    }
    if (g_zout2 != NULL) {
        ret = zmq_send(g_zout2, buf, len, flags);
    }
    return ret;
}

//todo
int MakePriceScaleEvent(struct SymbolInfo* si, struct Vip2PriceScaleUpdateEvent* ps) {
    ps->Mode = 63;

    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);

    sprintf(ps->Symbol, "%s.%s", newSymbolCode, "US");
    sprintf(ps->SecType, "%c", si->apex_symbol_type);
    ps->ScaleCount = si->scale_cnt;
    FillTimeString(ps->Time, JsonValLen);
    for (int i = 0; i < si->scale_cnt; ++i) {
        ps->Scales[i].ScopeMin = si->scale_items[i].ScopeMin;
        ps->Scales[i].ScopeMax = si->scale_items[i].ScopeMax;
        ps->Scales[i].Numerator = si->scale_items[i].Numerator;
        ps->Scales[i].Denominator = si->scale_items[i].Denominator;
        ps->Scales[i].MinMovement = si->scale_items[i].MinMovement;
    }
    return 1;
}

int send_all_symbols(int sleepOnSz) {
    size_t sendsz = 0;
    size_t onesz = 0;
    int flowctrl = 0;
    size_t outsz;
    char sendbuf[512];
    int i;
    for (i = 0; i < gsm->symbol_cnt; ++i) {
        struct SymbolInfo* si = gsm->symbols + i;
        if (!isSubscribed(si)) {
            continue;
        }
        si->is_subscribed = 1;
        Vip2UpdateEvent ue;
        make_symbol_update_event(si, &ue);
        char jsonbuf[2048];
        int len = MakeJsonUpdateEvent(&ue, jsonbuf, 2048);
        if (len > 0) {
            outsz = sprintf(sendbuf, "V01$%s$A$UE$ALL", ue.ColList[1].ColVal);
            emd_send(sendbuf, outsz, ZMQ_SNDMORE);
            emd_send(jsonbuf, len, 0);
            onesz = outsz + len;
        }

        //todo
        if (si->scale_cnt) {
            struct Vip2PriceScaleUpdateEvent ps;
            MakePriceScaleEvent(si, &ps);
            len = MakeJsonUpdateScalesEvent(&ps, jsonbuf, 2048);
            if (len > 0) {
                outsz = sprintf(sendbuf, "V01$%s$%c$Scale$%s",
                    si->exchange,
                    si->apex_symbol_type,
                    ps.Symbol);
                emd_send(sendbuf, outsz, ZMQ_SNDMORE);
                emd_send(jsonbuf, len, 0);
                onesz = outsz + len;
            }
        }
        sendsz += onesz;
        flowctrl += onesz;
        if (sleepOnSz > 0 && flowctrl > sleepOnSz) {
            flowctrl = 0;
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = 100000000;
            nanosleep(&req, NULL);
        }
    }
    return sendsz;
}

// returns yyyymm
int select_option_T_host(const char* exchgcomm, int deliveryYM) {
    char futbuf[64];
    sprintf(futbuf, "f;%s", exchgcomm);
    MonthsTable* mtbl = getMonthsTableEntry(futbuf);
    if (mtbl == NULL) {
        return 0;
    }
    int resDeliveryYM = 0;
    int i;
    for (i = 0; i < mtbl->count; ++i) {
        if (mtbl->mons[i] > deliveryYM) {
            continue;
        }
        if (mtbl->mons[i] > resDeliveryYM) {
            resDeliveryYM = mtbl->mons[i];
        }
    }
    return resDeliveryYM;
}

struct SymbolInfo* make_symbol(const char* symbol, const char* exchange) {
    struct SymbolInfo* si = NULL;
    simap_t::iterator iter = g_simap.find(symbol);
    if (iter == g_simap.end()) {
        si = alloc_symbol();
        int midx = si2midx(si);
        memset(si, 0x00, sizeof(struct SymbolInfo));
        si->category_id = -1;
        g_simap[symbol] = midx;
        strcpy(si->symbol, symbol);
        strcpy(si->exchange, exchange);
        si->session_status = -1;
        Logf("alloc symbol: %s", symbol);
    }
    else {
        si = gsm->symbols + iter->second;
    }
    return si;
}

void on_symbol_complete() {
    Logf("symbol complete");
    int i;
    for (i = 0; i < gsm->symbol_cnt; ++i) {
        struct SymbolInfo* si = gsm->symbols + i;
        char typecode = ' ';
        int typecode2 = 1;
        if (IS_FUTURE(si)) {
            typecode = 'f';
        }
        else if (IS_OPTION(si)) {
            typecode = 'o';
            typecode2 = 2;
        }

        //產生類別名稱
        char exchgcomm[64];
        sprintf(exchgcomm, "%s;%s", si->exchange, si->root_ext);
        //todo
        simap_t::iterator fiter = gFarMonthMap.find(exchgcomm);
        if (fiter != gFarMonthMap.end()) {
            if (IS_FUTURE(si) && !IS_SPREAD(si) && si->duemon == fiter->second) {
                si->is_farmonth = 1;
                char farSymbol[25];
                sprintf(farSymbol, "%s.FF", si->root_ext);
                struct SymbolInfo* siFar = make_symbol(farSymbol, si->exchange);
                if (siFar) {
                    memcpy(siFar, si, sizeof(struct SymbolInfo));
                    strcpy(siFar->symbol, farSymbol);
                    sprintf(si->far_alias, "%s", siFar->symbol);
                    sprintf(siFar->far_alias, "%s", si->symbol);
                    si->far_alias_midx = si2midx(siFar);
                    siFar->far_alias_midx = si2midx(si);
                }
            }
        }
        fiter = gCurrMonthMap.find(exchgcomm);
        // 產生近月商品代碼
        if (fiter != gCurrMonthMap.end()) {
            if (IS_FUTURE(si) && !IS_SPREAD(si) && si->duemon == fiter->second) {
                si->is_nearmonth = 1;
                char nearSymbol[25];
                sprintf(nearSymbol, "%s.00", si->root_ext);
                struct SymbolInfo* siNear = make_symbol(nearSymbol, si->exchange);
                if (siNear) {
                    memcpy(siNear, si, sizeof(struct SymbolInfo));
                    sprintf(siNear->symbol, "%s.00", siNear->root_ext);
                    sprintf(si->near_alias, "%s", siNear->symbol);
                    sprintf(siNear->near_alias, "%s", si->symbol);
                    si->near_alias_midx = si2midx(siNear);
                    siNear->near_alias_midx = si2midx(si);
                }
            }
        }
    }
    simap_t::iterator fi;
    for (fi = g_THostSet.begin(); fi != g_THostSet.end(); fi++) {
        if (fi->second != 0) {
            continue;
        }
        char commroot[32];
        char deliveryYMStr[32];
        if (split_name_value_pair(fi->first.c_str(), commroot, deliveryYMStr, '.')) {
            int thost = select_option_T_host(commroot, atol(deliveryYMStr) + 200000); //commroot: CME;S   deliveryYMInt:202010
            if (thost > 0) {
                char commcode[32];
                char exchg[32];
                if (split_name_value_pair(fi->first.c_str(), exchg, commcode, ';')) {
                    g_THostMap[fi->first.c_str()] = commcode;
                    Logf("option T host for %s is %s (second try)", fi->first.c_str(), commcode);
                }
            }
            else {
                Logf("option T host for %s is not found (symbol complete)", fi->first.c_str());
            }
        }
    }

    send_all_symbols(gSleepKB * 1024);
    //todo: send scale
    mdc_req_mktdata('S', 4, 0);
}

void on_symbol_root_complete() {
    Logf("symbol root complete");
    for (size_t j = 0; j < gExchanges.size(); ++j) {
        g_req_exchanges.push(gExchanges[j]);
    }
    if (g_req_exchanges.size() > 0) {
        std::string exchange = g_req_exchanges.front();
        g_req_exchanges.pop();
        mdc_req_symbol(exchange.c_str());
    }
}

void on_login_ok() {
    for (size_t j = 0; j < gExchanges.size(); ++j) {
        g_req_exchanges.push(gExchanges[j]);
    }
    if (g_req_exchanges.size() > 0) {
        std::string exchange = g_req_exchanges.front();
        g_req_exchanges.pop();
        mdc_req_symbol_root(exchange.c_str());
    }
}
int mdc_parse_login(const char* buf, size_t sz) {
    struct m2sv_login* p = (struct m2sv_login*)buf;
    char msg[65];
    int i;
    txstr(p->message, msg, 64);
    int priv_cnt = bcd32(p->priv_cnt);
    if (g_plain) {
        Logf("%s login: result=%c, msg=%s, expire=%d, cnt=%d",
            fmt_plain_head(&p->head),
            p->result,
            msg,
            bcd32(p->expire),
            priv_cnt);
        struct m2_privilege_rec* rec = (struct m2_privilege_rec*)p->tail;
        for (i = 0; i < priv_cnt; ++i) {
            char exchange[13];
            txstr(rec->exchange, exchange, 12);
            int feedid = bcd32(rec->feed_id);
            Logf("  |%s login: ID=%d, fg=0x%02x, exchange=%s",
                fmt_plain_head(&p->head),
                feedid,
                (uint8_t)rec->feed_type,
                exchange);
            rec++;
        }
    }
    if (p->result == 'Y') {
        Logf("login OK: msg=%s, expire=%d, cnt=%d", msg, bcd32(p->expire), priv_cnt);
        struct m2_privilege_rec* rec = (struct m2_privilege_rec*)p->tail;
        for (i = 0; i < priv_cnt; ++i) {
            char exchange[13];
            txstr(rec->exchange, exchange, 12);
            int feedid = bcd32(rec->feed_id);

            simap_t::iterator fi = g_exchg2feed_map.find(exchange);
            if (fi == g_exchg2feed_map.end()) {
                g_exchg2feed_map[exchange] = feedid;
            }

            Logf("  |ID=%d, fg=0x%02x, exchange=%s", feedid, (uint8_t)rec->feed_type, exchange);
            rec++;
        }
        on_login_ok();
    }
    else if (p->result == 'N') {
        LogErr("mdc login fail: %s", msg);
        destroy_app(); // should we retry on login failure or exit
        exit(EXIT_FAILURE);
    }
    return 0;
}

// sectype: O;F
struct SymbolRootInfo* make_symbol_root(const char* root, const char* exchange, char sectype) {
    char key[40];
    sprintf(key, "%s;%s;%c", exchange, root, sectype);
    struct SymbolRootInfo* sr = NULL;
    simap_t::iterator iter = g_srmap.find(key);
    if (iter == g_srmap.end()) {
        sr = alloc_symbol_root();
        int midx = sr2midx(sr);
        memset(sr, 0x00, sizeof(struct SymbolRootInfo));
        g_srmap[key] = midx;
        strcpy(sr->group_code, root);
        strcpy(sr->exchange, exchange);
        sr->type_fg = sectype;
        Logf("alloc symbol root: %s", key);
    }
    else {
        sr = gsm->roots + iter->second;
    }
    return sr;
}

void mdc_dump_symbol(struct m2_head* h, struct m2sv_symbol* p) {
    int symbolcnt = bcd32(p->symbol_cnt);
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    Logf("%s symbol: result=%c, exchange=%s, cnt=%d",
        fmt_plain_head(h), p->result, exchange, symbolcnt);
    int i;
    struct m2_symbol_rec* rec = (struct m2_symbol_rec*)p->tail;
    for (i = 0; i < symbolcnt; ++i) {
        char symbol[25];
        txstr(rec->symbol, symbol, 24);
        char name[49];
        txstr(rec->name, name, 48);
        char root[13];
        txstr(rec->root, root, 12);
        char strikep[13];
        txstr(rec->strike_price, strikep, 12);
        char scale[73];
        txstr(rec->scale, scale, 72);
        Logf("  |%s symbol=%s, name=%s, root=%s, exchange=%s, duemon=%d, nativemon=%d, start=%d, settledt=%d, 1stnotice=%d, 2ndnotice=%d, enddt=%d, lastdt=%d, type=%02x, lotsz=%d, strikeprice=%s, extrafg=%02x, scale=%s",
            fmt_plain_head(h),
            symbol, name, root, exchange,
            bcd32(rec->duemon),
            bcd32(rec->native_duemon),
            bcd32(rec->start_date),
            bcd32(rec->settle_date),
            bcd32(rec->first_notice_date),
            bcd32(rec->second_notice_date),
            bcd32(rec->end_date),
            bcd32(rec->last_trade_date),
            rec->type_fg,
            bcd32(rec->lot_size),
            strikep,
            rec->extra_fg,
            scale);
        rec++;
    }
}

void addRemoteMonthEntry(const char* exchgcomm, int deliveryYM) {
    if (strncmp(exchgcomm, "TCE", 3) != 0 && strncmp(exchgcomm, "TGE", 3) != 0 && strncmp(exchgcomm, "JPX", 3) != 0) {
        return;
    }
    simap_t::iterator fiter = gFarMonthMap.find(exchgcomm);
    if (fiter == gFarMonthMap.end()) {
        Logf("--- add %s, %d to gFarMonthMap", exchgcomm, deliveryYM);
        gFarMonthMap[exchgcomm] = deliveryYM;
    }
    else {
        if (deliveryYM > fiter->second) {
            Logf("--- replace %s, %d to gFarMonthMap", exchgcomm, deliveryYM);
            gFarMonthMap[exchgcomm] = deliveryYM;
        }
    }
}

void add_spot_symbol(struct SymbolInfo* si) {
    char findkey[64];
    sprintf(findkey, "%s;%s", si->exchange, si->root_ext);
    s2spairmap_t::iterator fsiter = g_fut_to_spot_map.find(findkey);
    if (fsiter == g_fut_to_spot_map.end()) {
        return;
    }
    Logf("%s found in fut_to_spot_map", findkey);
    struct SymbolInfo* sNew = make_symbol(fsiter->second.str1.c_str(), si->exchange);
    if (sNew == NULL) {
        return;
    }
    memcpy(sNew, si, sizeof(struct SymbolInfo));
    sNew->type_fg = 0x20;
    sNew->apex_symbol_type = convert_to_apex_symbol_type(sNew->type_fg)[1];
    strcpy(sNew->symbol, fsiter->second.str1.c_str());
    strcpy(sNew->name, fsiter->second.str2.c_str());
    strcpy(sNew->root_ext, fsiter->second.str1.c_str());
    strcpy(sNew->root, si->root_ext);
    Logf("add symbol %s, %s to map", fsiter->second.str1.c_str(), sNew->name);

    struct SymbolRootInfo* root0 = get_commroot_info(si->root_ext, si->exchange, si->apex_symbol_type);
    if (root0) {
        struct SymbolRootInfo* newRoot = make_symbol_root(sNew->root_ext, sNew->exchange, sNew->apex_symbol_type);
        memcpy(newRoot, root0, sizeof(struct SymbolRootInfo));
        strcpy(newRoot->exchange, sNew->exchange);
        strcpy(newRoot->group_code, sNew->root_ext);
    }

    char typeexchgcomm[200];
    sprintf(typeexchgcomm, "%d;%s;%s", 0, sNew->exchange, sNew->root_ext);
    gSymbol2CategoryMap[typeexchgcomm] = 100;

    sNew->category_id = 100;
}

int proc_scale_line(std::string& s, struct ScaleItem* scaleItem) {
    char buf[100];
    strcpy(buf, s.c_str()); //0,0,0,2,256,1
    char* tok = strtok(buf, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->ScopeMin = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->ScopeMax = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->ScopeMode = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->Numerator = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->Denominator = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->MinMovement = atol(tok);
    return 1;
}

int get_decimal_count_from_denominator(int numerator, int denominator) {
    char nbuf[64];
    if (numerator == 0) {
        numerator = 1;
    }
    sprintf(nbuf, "%.0f", (1000000000000.0 / denominator) * numerator);
    int zerocnt = 0;
    int len = strlen(nbuf);
    int i;
    for (i = len - 1; i >= 0; --i) {
        if (nbuf[i] == '0') {
            zerocnt++;
            continue;
        }
        break;
    }
    int ret = 12 - zerocnt;
    if (ret < 0) {
        ret = 0;
    }
    return ret;
}

int mdc_parse_symbol(const char* buf, size_t sz) {
    struct m2sv_symbol* p = (struct m2sv_symbol*)buf;
    if (g_plain) {
        mdc_dump_symbol(&p->head, p);
    }

    if (p->result == 'N') {
        Logf("symbol reply err");
        return 0;
    }
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    int cnt = bcd32(p->symbol_cnt);
    int i;
    struct m2_symbol_rec* rec = (struct m2_symbol_rec*)p->tail;
    for (i = 0; i < cnt; ++i) {
        char symbol[25];
        txstr(rec->symbol, symbol, 24);
        struct SymbolInfo* si = make_symbol(symbol, exchange);
        if (!si) {
            continue;
        }
        int is_spread = 0;
        if (strchr(symbol, '/')) {
            is_spread = 1;
        }
        simap_t::iterator efi = g_exchg2feed_map.find(exchange);
        if (efi != g_exchg2feed_map.end()) {
            si->feedID = efi->second;
        }
        txstr(rec->name, si->name, 48);
        txstr(rec->root, si->root_ext, 12);
        strcpy(si->root, si->root_ext);
        si->duemon = bcd32(rec->duemon);
        si->native_duemon = bcd32(rec->native_duemon);
        si->start_date = bcd32(rec->start_date);
        si->settle_date = bcd32(rec->settle_date);
        si->first_notice_date = bcd32(rec->first_notice_date);
        si->second_notice_date = bcd32(rec->second_notice_date);
        si->end_date = bcd32(rec->end_date);
        si->last_trade_date = bcd32(rec->last_trade_date);
        si->type_fg = rec->type_fg;
        si->apex_symbol_type = convert_to_apex_symbol_type(si->type_fg)[1];
        si->lot_size = bcd32(rec->lot_size);
        txstr(rec->strike_price, si->strike_price, sizeof(rec->strike_price));
        si->extra_fg = rec->extra_fg;
        txstr(rec->scale, si->scale, sizeof(rec->scale));

        std::vector<std::string> scale_lines;
        char sctmp[sizeof(si->scale)];
        strcpy(sctmp, si->scale); // 0;0,0,0,1,1,1|2;0,0,0,2,256,1
        char* tok = strtok(sctmp, ";");
        while (tok) {
            scale_lines.push_back(tok);
            tok = strtok(NULL, ";");
        }
        size_t i;
        for (i = 0; i < scale_lines.size(); ++i) {
            if (proc_scale_line(scale_lines[i], si->scale_items + si->scale_cnt)) {
                si->scale_cnt++;
                if (si->scale_cnt >= 5) {
                    Logf("err: scale count array size not enough for symbol: %s", si->symbol);
                    break;
                }
            }
        }

        if (si->scale_cnt == 0) {
            char rootkey[30];
            sprintf(rootkey, "%s;%s;%c", si->exchange, si->root, si->apex_symbol_type);
            simap_t::iterator it = g_srmap.find(rootkey);
            if (it != g_srmap.end()) {
                struct SymbolRootInfo* sr = gsm->roots + it->second;
                for (i = 0; i < (size_t)sr->scale_cnt; ++i) {
                    //if ((si->extra_fg & 0x04) && sr->scale_items[i].type == 2) {  //spread
                    //     memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                    //     si->scale_cnt++;
                    //     si->scale_from_root = 1;
                    // }
                    // 上游丟的這個旗標數值是錯的，所以暫時不判斷
                    // else if ((si->extra_fg & 0x08) && sr->scale_items[i].type == 1) {
                    //     memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                    //     si->scale_cnt++;
                    //     si->scale_from_root = 1;
                    // }
                    // else if (sr->scale_items[i].type == 0) {
                    //     memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                    //     si->scale_cnt++;
                    //     si->scale_from_root = 1;
                    // }
                    if (is_spread) {
                        if (sr->scale_items[i].type == 2) {
                            memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                            si->scale_cnt++;
                            si->scale_from_root = 1;
                        }
                    }
                    else {
                        if (sr->scale_items[i].type == 0) {
                            memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                            si->scale_cnt++;
                            si->scale_from_root = 1;
                        }
                    }
                }
            }
            else {
                Logf("warn: no root scale info for symbol %s, extra_fg=%02x", si->symbol, si->extra_fg);
            }
        }
        //如果還是找不到scale，就把default搬過來用
        if (si->scale_cnt == 0) {
            char rootkey[30];
            sprintf(rootkey, "%s;%s;%c", si->exchange, si->root, si->apex_symbol_type);
            simap_t::iterator it = g_srmap.find(rootkey);
            if (it != g_srmap.end()) {
                struct SymbolRootInfo* sr = gsm->roots + it->second;
                for (i = 0; i < (size_t)sr->scale_cnt; ++i) {
                    if (sr->scale_items[i].type == 0) {
                        memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                        si->scale_cnt++;
                        si->scale_from_root = 1;
                    }
                }
                if (si->scale_cnt) {
                    Logf("warn: use default scale for spread symbol=%s", si->symbol);
                }
            }
        }
        //如果還是找不到scale，就把價差的scale搬來當default的用
        if (si->scale_cnt == 0) {
            char rootkey[30];
            sprintf(rootkey, "%s;%s;%c", si->exchange, si->root, si->apex_symbol_type);
            simap_t::iterator it = g_srmap.find(rootkey);
            if (it != g_srmap.end()) {
                struct SymbolRootInfo* sr = gsm->roots + it->second;
                for (i = 0; i < (size_t)sr->scale_cnt; ++i) {
                    memcpy(si->scale_items + si->scale_cnt, &sr->scale_items[i].item, sizeof(struct ScaleItem));
                    si->scale_cnt++;
                    si->scale_from_root = 1;
                }
                if (si->scale_cnt) {
                    Logf("warn: use spread scale for common symbol=%s", si->symbol);
                }
            }
        }
        if (si->scale_cnt) {
            si->decimals = get_decimal_count_from_denominator(si->scale_items[0].Numerator, si->scale_items[0].Denominator);
        }
        else {
            Logf("err: no scale info for symbol %s", si->symbol);
        }

        char exchgcommroottype[64];
        char typecode = ' ';
        int typecode2 = 1;
        if (IS_FUTURE(si)) {
            typecode = 'f';
        }
        else if (IS_OPTION(si)) {
            typecode = 'o';
            typecode2 = 2;
        }

        char categoryMapKey[64];
        sprintf(categoryMapKey, "%d;%s;%s", typecode2, si->exchange, si->root_ext);
        simap_t::iterator categorymap_iter = gSymbol2CategoryMap.find(categoryMapKey);
        if (categorymap_iter != gSymbol2CategoryMap.end()) {
            si->category_id = categorymap_iter->second;
        }

        if (IS_FUTURE(si) && !(IS_SPREAD(si))) {
            if (isSubscribed(si)) {
                char exchgcomm[65];
                sprintf(exchgcomm, "%s;%s", si->exchange, si->root_ext);
                addRemoteMonthEntry(exchgcomm, si->duemon);
            }
        }
        //把周期貨/選擇權的root換掉

        sprintf(exchgcommroottype, "%s;%s;%c", si->exchange, si->root_ext, typecode);
        s2smap_t::iterator alteriter = gAlterCommrootMap.find(exchgcommroottype);
        if (alteriter != gAlterCommrootMap.end()) {
            sprintf(si->root_ext, "%s", alteriter->second.c_str());
        }
        // create new symbol entry for spot index
        add_spot_symbol(si);

        rec++;
    }
    if (p->result == 'L') {
        if (g_req_exchanges.size() > 0) {
            std::string nextExchange = g_req_exchanges.front();
            g_req_exchanges.pop();
            mdc_req_symbol(nextExchange.c_str());
        }
        else {
            on_symbol_complete();
        }
    }
    return 0;
}

int mdc_parse_sub_reply(const char* buf, size_t sz) {
    struct m2sv_sub_reply* p = (struct m2sv_sub_reply*)buf;
    if (g_plain) {
        char msgdump[97];
        txstr(p->message, msgdump, 96);
        Logf("%s sub reply: result=%c, cmd=%c, feedid=%d, seqno=%ld, msg=%s",
            fmt_plain_head(&p->head),
            p->result,
            p->cmd,
            bcd32(p->feed_id),
            bcd64(p->seqno),
            msgdump);
    }
    if (p->result == 'Y') {
        Logf("sub reply OK: cmd=%c");
    }
    else if (p->result == 'N') {
        Logf("sub reply failed: cmd=%c");
    }
    int feedid = bcd32(p->feed_id);
    int64_t seqno = bcd64(p->seqno);
    char msg[97];
    txstr(p->message, msg, 96);
    Logf("  |cmd=%c, copyid=%d, seqno=%ld, msg=%s", p->cmd, feedid, seqno, msg);
    return 0;
}

// returns NULL if not found
struct SymbolInfo* get_symbol(const char* symbol) {
    simap_t::iterator iter = g_simap.find(symbol);
    if (iter == g_simap.end()) {
        return NULL;
    }
    return gsm->symbols + iter->second;
}

/*
int send_tick(const LPMdcs_QuotationPtr q, struct SymbolCache* psi) {
    const Mdcs_Commodity* si = &psi->commodity;
    struct Vip2AddTick t;
    MakeAddTickEvent(q, &t, psi);
    if (psi->commodity.Category == Mdct_CC_Stock_MIN) {
        if (t.Tick.Parr[0].Price == 0) {
            return 0;
        }
    }

    //bm
    if (psi->ba_dirty) {
        if ((psi->depth.BidPrice1 > q->Deal->LastPrice) || (psi->depth.AskPrice1 < q->Deal->LastPrice)) {
            logf("tick outrange orderbook");
            SendBAforReal(&psi->depth, psi);
            psi->ba_dirty = 0;
        }
    }

    char jsonbuf[2048];
    int len = MakeJsonAddTick(&t, jsonbuf, 2048);
    if (len > 0) {
        char sendbuf[512];
        int outsz = sprintf(sendbuf, "V01$%s$%c$Tick$%s",
            ConvertToMIC(si->ExchangeAbbr),
            ConvertToApxSymbolType(si->Category)[1],
            t.Symbol);
        ZmqSend(sendbuf, outsz, ZMQ_SNDMORE);
        ZmqSend(jsonbuf, len, 0);
        return outsz + len;
    }
    return 0;
}
*/

// 0: clear quote, 2: open, 7: close
void MakeSymbolTradeSessionUpdateEvent(const char* symbol, struct Vip2UpdateEvent* r1, int status) {
    r1->Mode = 57;
    r1->Version = 0;

    char newSymbolCode[64];
    convertOptionCode(symbol, newSymbolCode);

    strcpy(r1->FilterCol, "SYMBO");
    sprintf(r1->FilterVal, "%s.%s", newSymbolCode, "US");
    FillTimeString(r1->EventTime, JsonValLen);
    r1->EventType = 5;
    r1->ColListCnt = 1;
    strcpy(r1->ColList[0].ColName, "340");
    sprintf(r1->ColList[0].ColVal, "%d", status);
}

int SendTradeSessionStatus(struct SymbolInfo* si, int status) {
    //int sleepOnSz = gSleepKB * 1024;
    //static int flowCtrl = 0;
    struct Vip2UpdateEvent s;
    MakeSymbolTradeSessionUpdateEvent(si->symbol, &s, status);
    char jsonbuf[512];
    int len = MakeJsonUpdateEvent(&s, jsonbuf, 512);
    if (len > 0) {
        char sendbuf[512];
        int outsz = sprintf(sendbuf, "V01$%s$%c$UE$%s",
            ConvertToMIC(si->exchange),
            si->apex_symbol_type,
            s.FilterVal);
        emd_send(sendbuf, outsz, ZMQ_SNDMORE);
        emd_send(jsonbuf, len, 0);
        /*
        if (quotelogfh) {
            fprintf(quotelogfh, "send trade status: symbol=%s, %s(%d)\n", psi->commodity.Code, get_trade_status_string(status), status);
        }
        flowCtrl += outsz + len;
        if (sleepOnSz > 0 && flowCtrl > sleepOnSz) {
            flowCtrl = 0;
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = 100000000;
            nanosleep(&req, NULL);
        }
        */
        return outsz + len;
    }
    return 0;
}

void fill_timestamp(int ymd, int64_t hmsffff, char* buf, int buflen) {
    sprintf(buf, "%d_%02ld:%02ld:%02ld.%03ld",
        ymd,
        hmsffff / 100000000,
        (hmsffff % 100000000) / 1000000,
        (hmsffff % 1000000) / 10000,
        (hmsffff % 10000) / 10);
}

int fexist(struct m2_quote* q, int mask) {
    return (*(uint16_t*)q->exist_fg) & (mask << 8 | mask >> 8);
}

int fupdated(struct m2_quote* q, int mask) {
    return (*(uint16_t*)q->update_fg) & (mask << 8 | mask >> 8);
}

int fchanged(struct m2_quote* q, int mask) {
    return fexist(q, mask) && fupdated(q, mask);
}

void emd_send_quote(struct SymbolInfo* si, struct m2_head* head, struct m2_quote* p) {
    struct Vip2UpdateEvent r1;
    r1.Mode = 57;
    r1.Version = 0;

    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);

    strcpy(r1.FilterCol, "SYMBO");
    sprintf(r1.FilterVal, "%s.%s", newSymbolCode, "US");
    time_t nowt = time(NULL);
    int ymd = to_ymd(nowt); //#todo, date value will be incorrect on midnight
    int64_t headtime = bcd64(head->time);
    fill_timestamp(ymd, headtime, r1.EventTime, JsonValLen);
    r1.EventType = 2;
    int col = 0;
    if (fexist(p, QM_OPEN_REF)) {
        strcpy(r1.ColList[col].ColName, "1150"); // open ref
        sprintf(r1.ColList[col++].ColVal, "%ld", si->open_ref);
    }
    if (fexist(p, QM_PREV_CLOSE)) {
        strcpy(r1.ColList[col].ColName, "140"); // previous close
        sprintf(r1.ColList[col++].ColVal, "%ld", si->prev_close);
    }
    if (fexist(p, QM_OPEN)) {
        strcpy(r1.ColList[col].ColName, "1025"); // open
        sprintf(r1.ColList[col++].ColVal, "%ld", si->open);
    }
    if (fexist(p, QM_HIGH)) {
        strcpy(r1.ColList[col].ColName, "332"); // high
        sprintf(r1.ColList[col++].ColVal, "%ld", si->high);
    }
    if (fexist(p, QM_LOW)) {
        strcpy(r1.ColList[col].ColName, "333"); // low
        sprintf(r1.ColList[col++].ColVal, "%ld", si->low);
    }
    if (fexist(p, QM_CLOSE)) {
        strcpy(r1.ColList[col].ColName, "31"); // close
        sprintf(r1.ColList[col++].ColVal, "%ld", si->close);
    }
    if (fexist(p, QM_RISE_LIMIT)) {
        strcpy(r1.ColList[col].ColName, "1149"); // up limit
        sprintf(r1.ColList[col++].ColVal, "%ld", si->rise_limit);
    }
    if (fexist(p, QM_FALL_LIMIT)) {
        strcpy(r1.ColList[col].ColName, "1148"); // down limit
        sprintf(r1.ColList[col++].ColVal, "%ld", si->fall_limit);
    }
    if (fexist(p, QM_SETTLEMENT)) {
        strcpy(r1.ColList[col].ColName, "20201"); // settlement price
        sprintf(r1.ColList[col++].ColVal, "%ld", si->settlement);
    }
    if (fexist(p, QM_PREV_SETTLEMENT)) {
        strcpy(r1.ColList[col].ColName, "20206"); // prev settlement price
        sprintf(r1.ColList[col++].ColVal, "%ld", si->prev_settlement);
    }
    if (fexist(p, QM_PREV_OI)) {
        strcpy(r1.ColList[col].ColName, "20116"); // previous open interest
        sprintf(r1.ColList[col++].ColVal, "%ld", si->prev_oi);
    }

    r1.ColListCnt = col;

    char jsonbuf[2048];
    int len = MakeJsonUpdateEvent(&r1, jsonbuf, sizeof(jsonbuf));
    if (len > 0) {
        char sendbuf[512];
        int outsz = sprintf(sendbuf, "V01$%s$%c$UE$%s",
            ConvertToMIC(si->exchange), //#todo, remap to spf1's exchange code
            si->apex_symbol_type,
            si->symbol);
        emd_send(sendbuf, outsz, ZMQ_SNDMORE);
        emd_send(jsonbuf, len, 0);
    }
}

void emd_send_high_low(struct SymbolInfo* si) {
    struct Vip2UpdateEvent r1;
    r1.Mode = 57;
    r1.Version = 0;

    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);

    strcpy(r1.FilterCol, "SYMBO");
    sprintf(r1.FilterVal, "%s.%s", newSymbolCode, "US");
    fill_timestamp(si->tick_ymd, si->tick_hmsf, r1.EventTime, JsonValLen);
    r1.EventType = 2;
    int col = 0;
    strcpy(r1.ColList[col].ColName, "332"); // high
    sprintf(r1.ColList[col++].ColVal, "%ld", si->high);
    strcpy(r1.ColList[col].ColName, "333"); // low
    sprintf(r1.ColList[col++].ColVal, "%ld", si->low);
    r1.ColListCnt = col;

    char jsonbuf[512];
    int len = MakeJsonUpdateEvent(&r1, jsonbuf, sizeof(jsonbuf));
    if (len > 0) {
        char sendbuf[140];
        int outsz = sprintf(sendbuf, "V01$%s$%c$UE$%s",
            ConvertToMIC(si->exchange), //#todo, remap to spf1's exchange code
            si->apex_symbol_type,
            r1.FilterVal);
        emd_send(sendbuf, outsz, ZMQ_SNDMORE);
        emd_send(jsonbuf, len, 0);
    }
}

void on_session_clear(struct SymbolInfo* si, int oldStatus) {
    Logf("OnClear: %s", si->symbol);
    SendTradeSessionStatus(si, TRADE_CLOSE);
    SendTradeSessionStatus(si, TRADE_CLEAR);
}

void on_session_open(struct SymbolInfo* si, int oldStatus) {
    SendTradeSessionStatus(si, TRADE_CLOSE);
    SendTradeSessionStatus(si, TRADE_CLEAR);
    SendTradeSessionStatus(si, TRADE_OPEN);
}

void on_session_close(struct SymbolInfo* si, int oldStatus) {
    if (si->session_status != 3) {
        Logf("send additional open signal: symbol=%s", si->symbol);
        SendTradeSessionStatus(si, TRADE_OPEN);
    }
    SendTradeSessionStatus(si, TRADE_CLOSE);
}

int mdc_parse_quote(struct SymbolInfo* si, struct m2_head* head, struct m2_quote* p, int decimals) {
    int decDiff = decimals - si->decimals;
    if (decDiff<0) {
        Logf("err: decdiff negative for symbol=%s", si->symbol);
        return 0;
    }

    si->session_date = bcd32(p->trade_date);
    si->rise_limit = sbcd64(p->rise_limit) / dec_tbl[decDiff];
    si->fall_limit = sbcd64(p->fall_limit) / dec_tbl[decDiff];
    si->open_ref = sbcd64(p->open_ref) / dec_tbl[decDiff];
    si->close = sbcd64(p->close) / dec_tbl[decDiff];
    si->settlement = sbcd64(p->settlement) / dec_tbl[decDiff];
    si->prev_close = sbcd64(p->prev_close) / dec_tbl[decDiff];
    si->prev_settlement = sbcd64(p->prev_settlement) / dec_tbl[decDiff];
    si->prev_oi = bcd64(p->prev_oi);
    si->open = sbcd64(p->open) / dec_tbl[decDiff];
    si->high = sbcd64(p->high) / dec_tbl[decDiff];
    si->low = sbcd64(p->low) / dec_tbl[decDiff];

    if (!si->is_subscribed) {
        return 0;
    }
    if ((si->session_status == -1 && fexist(p, QM_SESSION_STATUS)) ||
        fchanged(p, QM_SESSION_STATUS)) {
        int apexStatus = ConvertToApxTradeSessionStatus(bcd32(p->trade_session_status));
        // Logf("initial trade session status for symbol %s=%d", si->symbol, p->trade_session_status);
        switch (apexStatus) {
        case TRADE_CLEAR:
            on_session_clear(si, si->session_status);
            break;
        case TRADE_OPEN:
            on_session_open(si, si->session_status);
            break;
        case TRADE_CLOSE:
            on_session_close(si, si->session_status);
            break;
        }
        Logf("symbol %s trade session status changed from %d to %d", si->symbol, si->session_status, apexStatus);
        si->session_status = apexStatus;
    }
    emd_send_quote(si, head, p);
    return 0;
}

void emd_send_tick(struct SymbolInfo* si) {
    struct Vip2AddTick t;
    t.Mode = 51;
    t.Version = 48;

    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);
    sprintf(t.Symbol, "%s.US", newSymbolCode);
    sprintf(t.SecType, "%c", si->apex_symbol_type);

    t.Tick.TradeType = 3;
    strcpy(t.Tick.Plottable, "11");
    t.Tick.UpdStat = 1;
    fill_timestamp(si->tick_ymd, si->tick_hmsf, t.Tick.TickTime, JsonValLen);
    t.Tick.TotalVol = si->tot_vol;
    sprintf(t.Tick.TotAmt, "%.0f", si->tot_value);
    t.Tick.PriceCnt = 1;
    t.Tick.NoVolCnt = 0;
    t.Tick.Parr[0].Price = si->close;
    t.Tick.Parr[0].Vol = si->tick_vol;
    t.Tick.Parr[0].Type = 0;

    char jsonbuf[2048];
    int len = MakeJsonAddTick(&t, jsonbuf, sizeof(jsonbuf));
    if (len > 0) {
        char sendbuf[512];
        int outsz = sprintf(sendbuf, "V01$%s$%c$Tick$%s",
            ConvertToMIC(si->exchange), //#todo, remap to spf1's exchange code
            si->apex_symbol_type,
            t.Symbol);
        emd_send(sendbuf, outsz, ZMQ_SNDMORE);
        emd_send(jsonbuf, len, 0);
    }
}

int mdc_parse_tick(struct SymbolInfo* si, struct m2_tick* t0, int pricedec) {
    int decDiff = pricedec - si->decimals;
    if (decDiff<0) {
        Logf("err: decdiff negative for symbol=%s", si->symbol);
        return 0;
    }

    si->tick_ymd = bcd32(t0->date);
    si->tick_hmsf = (uint32_t)bcd64(t0->time);
    si->close = sbcd64(t0->price) / dec_tbl[decDiff];
    si->tick_vol = bcd32(t0->tickvol);
    si->tot_vol = bcd64(t0->totvol);
    si->tick_value = bcd64(t0->value) / fdec_tbl[bcd32(t0->value_dec)];
    si->tot_value = bcd64(t0->tot_value) / fdec_tbl[bcd32(t0->tot_value_dec)];
    si->balance = bcd64(t0->balance);
    si->oi = bcd64(t0->oi);
    si->price_offset = t0->ba_offset;
    si->trade_status = t0->trade_status;
    if (si->close > si->high) {
        si->high = si->close;
    }
    if (si->close < si->low) {
        si->low = si->close;
    }

    if (!si->is_subscribed) {
        return 0;
    }
    emd_send_tick(si);
    return 0;
}

int mdc_parse_ba(struct SymbolInfo* si, struct m2_ba* ba, int pricedec) {
    int decDiff = pricedec - si->decimals;
    if (decDiff<0) {
        Logf("err: decdiff negative for symbol=%s", si->symbol);
        return 0;
    }

    si->ba_ymd = bcd32(ba->date);
    si->ba_hmsf = (uint32_t)bcd64(ba->time);
    int depth = bcd32(ba->depth);
    if (depth > 20) {
        depth = 20;
    }
    struct m2_pv* baItem = (struct m2_pv*)ba->tail;
    int i;
    for (i = 0; i < depth; ++i) {
        si->bid[i].price = sbcd64(baItem->price) / dec_tbl[decDiff];
        si->bid[i].vol = bcd32(baItem->vol);
        baItem++;
        si->ask[i].price = sbcd64(baItem->price) / dec_tbl[decDiff];
        si->ask[i].vol = bcd32(baItem->vol);
        baItem++;
    }

    if (!si->is_subscribed) {
        return 0;
    }
    struct Vip2UpdateBA r1;
    memset(&r1, 0x00, sizeof(struct Vip2UpdateBA));
    r1.Mode = 53;
    r1.Version = 48;
    char newSymbolCode[64];
    convertOptionCode(si->symbol, newSymbolCode);
    sprintf(r1.Symbol, "%s.US", newSymbolCode);
    sprintf(r1.SecType, "%c", si->apex_symbol_type);
    fill_timestamp(si->ba_ymd, si->ba_hmsf, r1.BA.BATime, JsonValLen);
    r1.BA.TradeType = 3;
    r1.BA.MDFeedT = 101;
    r1.BA.FBidPrice = 0;
    r1.BA.FBidVol = 0;
    r1.BA.FAskPrice = 0;
    r1.BA.FAskVol = 0;

    for (i = 0; i < depth; ++i) {
        r1.BA.Bid[i].BAPrice = si->bid[i].price;
        r1.BA.Bid[i].BAVol = si->bid[i].vol;
        r1.BA.Ask[i].BAPrice = si->ask[i].price;
        r1.BA.Ask[i].BAVol = si->ask[i].vol;
    }
    char jsonbuf[2048];
    int len = MakeJsonUpdateBA(&r1, jsonbuf, 2048);
    if (len > 0) {
        char sendbuf[512];
        int outsz = sprintf(sendbuf, "V01$%s$%c$BA$%s",
            ConvertToMIC(si->exchange),
            si->apex_symbol_type,
            r1.Symbol);
        emd_send(sendbuf, outsz, ZMQ_SNDMORE);
        emd_send(jsonbuf, len, 0);
        return outsz + len;
    }
    return 0;
}

//todo
int mdc_parse_tick_ext(struct SymbolInfo* si, struct m2_tick_ext* t, int pricedec) {
    int decDiff = pricedec - si->decimals;
    if (decDiff<0) {
        Logf("err: decdiff negative for symbol=%s", si->symbol);
        return 0;
    }
    int dirty = 0;
    int64_t high = sbcd64(t->high) / dec_tbl[decDiff];    
    if (high>si->high) {
        si->high = high;
        dirty = 1;
    }
    int64_t low = sbcd64(t->low) / dec_tbl[decDiff];
    if (low<si->low) {
        si->low = low;
        dirty = 1;
    }
    if (dirty) {
        emd_send_high_low(si);
    }
    return 0;
}

void dump_quote(struct m2_head* h, struct m2_quote* q) {
    Logf("  |%s quote: existfg=0x%02x%02x, updatefg=0x%02x%02x, sessionstatus=%d, sessiontype=%d, tradedt=%d, risel=%ld, falll=%ld, openref=%ld, close=%ld, settle=%ld, prevclose=%ld, prevsettle=%ld, prevoi=%ld, open=%ld, high=%ld, low=%ld",
        fmt_plain_head(h),
        (uint8_t)q->exist_fg[0], (uint8_t)q->exist_fg[1],
        (uint8_t)q->update_fg[0], (uint8_t)q->update_fg[1],
        bcd32(q->trade_session_status),
        bcd32(q->trade_session_type),
        bcd32(q->trade_date),
        sbcd64(q->rise_limit), sbcd64(q->fall_limit),
        sbcd64(q->open_ref),
        sbcd64(q->close),
        sbcd64(q->settlement),
        sbcd64(q->prev_close),
        sbcd64(q->prev_settlement),
        bcd64(q->prev_oi),
        sbcd64(q->open),
        sbcd64(q->high), sbcd64(q->low));
}

void dump_tick(struct m2_head* h, struct m2_tick* t) {
    Logf("  |%s tick: fg=%02x, cnt=%d, date=%d, time=%ld, price=%ld, tickvol=%d, totvol=%ld, bid=%ld, bidvol=%d, ask=%ld, askvol=%d, valdec=%d, value=%ld, totvaldec=%d, totval=%ld, balance=%ld, oi=%ld, baoffset=%c, tradestatus=%c",
        fmt_plain_head(h),
        t->fg, bcd32(t->cnt),
        bcd32(t->date), bcd64(t->time),
        sbcd64(t->price), bcd32(t->tickvol),
        bcd64(t->totvol),
        sbcd64(t->bid), bcd64(t->bidvol),
        sbcd64(t->ask), bcd64(t->askvol),
        bcd32(t->value_dec), bcd64(t->value),
        bcd32(t->tot_value_dec), bcd64(t->tot_value),
        bcd64(t->balance), bcd64(t->oi),
        t->ba_offset, t->trade_status);
}

void dump_tick_ext(struct m2_head* h, struct m2_tick_ext* t) {
    dump_tick(h, (struct m2_tick*)t);
    Logf("  |%s tickext: open=%ld, high=%ld, low=%ld",
        fmt_plain_head(h),
        sbcd64(t->open), sbcd64(t->high), sbcd64(t->low));
}

void dump_ba(struct m2_head* h, struct m2_ba* b) {
    int depth = bcd32(b->depth);
    Logf("  |%s ba: date=%d, time=%ld, depth=%d",
        fmt_plain_head(h),
        bcd32(b->date), bcd64(b->time), depth);
    struct m2_pv* rec = (struct m2_pv*)b->tail;
    int i;
    struct m2_pv* bidpv = rec;
    struct m2_pv* askpv = bidpv + 1;
    for (i = 0; i < depth; ++i) {
        Logf("    |%s ba%d: bid=(%ld, %d), ask=(%ld, %d)",
            fmt_plain_head(h), i,
            sbcd64(bidpv->price), bcd32(bidpv->vol),
            sbcd64(askpv->price), bcd32(askpv->vol));
        rec += 2;
        bidpv = rec;
        askpv = bidpv + 1;
    }
}

void dump_mktdata(struct m2sv_quote* p) {
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    char symbol[25];
    txstr(p->symbol, symbol, 24);
    Logf("%s mktdata: feed_id=%d, data_id=%d, feedseqno=%ld, exchange=%s, symbol=%s, dec=%d, type=%c, datafg=%02x",
        fmt_plain_head(&p->head),
        bcd32(p->orig_feed_id),
        bcd32(p->feed_id),
        bcd64(p->seqno),
        exchange, symbol,
        bcd32(p->decimals),
        p->data_type,
        p->content_fg);
    char* tail = p->tail;
    if (p->content_fg & 0x01) {
        dump_quote(&p->head, (struct m2_quote*)tail);
        tail += sizeof(struct m2_quote);
    }
    if (p->content_fg & 0x02) {
        if (*tail == 0x00) {
            dump_tick(&p->head, (struct m2_tick*)tail);
            tail += sizeof(m2_tick);
        }
        else if (*tail == 0x01 || *tail == 0x02 || *tail == 0x04) {
            dump_tick_ext(&p->head, (struct m2_tick_ext*)tail);
            tail += sizeof(m2_tick_ext);
        }
    }
    if (p->content_fg & 0x04) {
        dump_ba(&p->head, (struct m2_ba*)tail);
        tail += sizeof(m2_ba);
    }
}

int mdc_parse_mktdata(const char* buf, size_t sz) {
    struct m2sv_quote* p = (struct m2sv_quote*)buf;
    if (g_plain) {
        dump_mktdata(p);
    }
    if (p->data_type != 'R') {
        return 0;
    }
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    char symbol[25];
    txstr(p->symbol, symbol, 24);
    struct SymbolInfo* si = make_symbol(symbol, exchange);
    if (si == NULL) {
        Logf("cannot find symbol %s on mktdata", symbol);
        return 0;
    }
    int pricedec = bcd32(p->decimals);
    int hasquote = 0;
    int hastick = 0;
    int hasba = 0;
    char* tail = p->tail;
    if (p->content_fg & 0x01) {
        hasquote = 1;
        mdc_parse_quote(si, &p->head, (struct m2_quote*)tail, pricedec);
        tail += sizeof(struct m2_quote);
    }
    if (p->content_fg & 0x02) {
        hastick = 1;
        if (*tail == 0x00) {
            mdc_parse_tick(si, (struct m2_tick*)tail, pricedec);
            tail += sizeof(struct m2_tick);
        }
        else {
            mdc_parse_tick(si, (struct m2_tick*)tail, pricedec);
            tail += sizeof(struct m2_tick);
            mdc_parse_tick_ext(si, (struct m2_tick_ext*)tail, pricedec);
            //#todo, how to deal with high, low, open in this tick?
            tail += sizeof(struct m2_tick_ext);
        }
    }
    if (p->content_fg & 0x04) {
        hasba = 1;
        mdc_parse_ba(si, (struct m2_ba*)tail, pricedec);
    }

    return 0;
}

int mdc_parse_message(const char* buf, size_t sz) {
    struct m2sv_message* p = (struct m2sv_message*)buf;
    int msgid = bcd32(p->msgid);
    int msglen = bcd32(p->msglen);
    char content[10000];
    txstr(p->tail, content, msglen);
    if (g_plain) {
        Logf("%s msg: id=%d, len=%d, msg=%s",
            fmt_plain_head(&p->head),
            msgid, msglen, content);
    }
    switch (msgid) {
    case 1:
        // double login, connection reset
        break;
    case 2:
        // protocol error, connection reset
        break;
    case 1001:
        // client read timeout, connect reset
        break;
    case 1002:
        break;
    }
    Logf("message %d: %s", msgid, content);
    return 0;
}

void dump_symbol_root(struct m2sv_symbol_root* p) {
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    int cnt = bcd32(p->cnt);
    Logf("%s symbolgrp: result=%c, exchange=%s, cnt=%d",
        fmt_plain_head(&p->head), p->result, exchange, cnt);
    int i;
    struct m2_symbol_root_rec* rec = (struct m2_symbol_root_rec*)p->tail;
    for (i = 0; i < cnt; ++i) {
        char grp[25];
        txstr(rec->group_code, grp, 24);
        char grpname[49];
        txstr(rec->group_name, grpname, sizeof(rec->group_name));
        char sessions[257];
        txstr(rec->sessions, sessions, sizeof(rec->sessions));
        char scales[385];
        txstr(rec->scales, scales, sizeof(rec->scales));
        char deriv_exchange[25];
        txstr(rec->deriv_exchange, deriv_exchange, sizeof(rec->deriv_exchange));
        char deriv_symbol[25];
        txstr(rec->deriv_symbol, deriv_symbol, sizeof(rec->deriv_symbol));
        char currency[9];
        txstr(rec->currency, currency, sizeof(rec->currency));
        Logf("  |%s grp%d: code=%s, name=%s, sessions=%s, scales=%s, deriv_exchange=%s, deriv_symbol=%s, multdec=%d, mult=%ld, currency=%s, feedec=%d, fee=%ld, taxdec=%d, tax=%ld, lotsz=%d, cat=0x%02x, tz=%d, ext=%c",
            fmt_plain_head(&p->head),
            i, grp, grpname, sessions, scales, deriv_exchange, deriv_symbol,
            bcd32(rec->contract_multiplier_dec), bcd64(rec->contract_multiplier),
            currency,
            bcd32(rec->fee_dec), bcd64(rec->fee),
            bcd32(rec->tax_dec), bcd64(rec->tax),
            bcd32(rec->lot_size),
            rec->type_fg,
            bcd32(rec->timezone),
            rec->extension);
        rec++;
    }
}

int proc_typed_scale_line(std::string& s, struct TypedScaleItem* scaleItem) {
    char buf[100];
    strcpy(buf, s.c_str()); //2;0,0,0,2,256,1
    char* tok = strtok(buf, ";");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->type = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.ScopeMin = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.ScopeMax = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.ScopeMode = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.Numerator = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.Denominator = atol(tok);
    tok = strtok(NULL, ",");
    if (tok == NULL) {
        return 0;
    }
    scaleItem->item.MinMovement = atol(tok);
    return 1;
}

int mdc_parse_symbol_root(const char* buf, size_t sz) {
    struct m2sv_symbol_root* p = (struct m2sv_symbol_root*)buf;
    if (g_plain) {
        dump_symbol_root(p);
    }
    if (p->result == 'N') {
        Logf("symbol group reply is N");
        return 0;
    }
    char exchange[13];
    txstr(p->exchange, exchange, 12);
    int cnt = bcd32(p->cnt);
    int i;
    struct m2_symbol_root_rec* rec = (struct m2_symbol_root_rec*)p->tail;
    for (i = 0; i < cnt; ++i) {
        char grp[25];
        txstr(rec->group_code, grp, 24);
        struct SymbolRootInfo* sg = make_symbol_root(grp, exchange, convert_to_apex_symbol_type(rec->type_fg)[1]);
        if (sg == NULL) {
            Logf("cannot make symbol group %s", grp);
            continue;
        }
        txstr(rec->group_name, sg->group_name, sizeof(rec->group_name));
        txstr(rec->sessions, sg->sessions, sizeof(rec->sessions));
        txstr(rec->scales, sg->scales, sizeof(rec->scales));

        std::vector<std::string> scale_lines;
        char sctmp[sizeof(sg->scales)];
        strcpy(sctmp, sg->scales); // 0;0,0,0,1,1,1|2;0,0,0,2,256,1
        char* tok = strtok(sctmp, "|");
        while (tok) {
            scale_lines.push_back(tok);
            tok = strtok(NULL, "|");
        }
        size_t i;
        for (i = 0; i < scale_lines.size(); ++i) {
            //經過實際驗證，上游並未丟出flag==0x01的scale類別(近月)，而且spread的flag給的是錯的，所以必須自己檢查是否為spread
            if (proc_typed_scale_line(scale_lines[i], sg->scale_items + sg->scale_cnt)) {
                sg->scale_cnt++;
                if (sg->scale_cnt >= 5) {
                    Logf("err: scale count array size not enough for symbol root: %s", grp);
                    break;
                }
            }
        }

        txstr(rec->deriv_exchange, sg->deriv_exchange, sizeof(rec->deriv_exchange));
        txstr(rec->deriv_symbol, sg->deriv_symbol, sizeof(rec->deriv_symbol));
        sg->contract_multiplier_dec = bcd32(rec->contract_multiplier_dec);
        sg->contract_multiplier = bcd64(rec->contract_multiplier);
        txstr(rec->currency, sg->currency, sizeof(rec->currency));
        sg->fee_dec = bcd32(rec->fee_dec);
        sg->fee = bcd64(rec->fee);
        sg->tax_dec = bcd32(rec->tax_dec);
        sg->tax = bcd64(rec->tax);
        sg->lot_size = bcd32(rec->lot_size);
        sg->type_fg = rec->type_fg;
        sg->timezone = bcd32(rec->timezone);
        sg->extension = rec->extension;

        rec++;
    }
    if (p->result == 'L') {
        if (g_req_exchanges.size() > 0) {
            std::string nextExchange = g_req_exchanges.front();
            g_req_exchanges.pop();
            mdc_req_symbol_root(nextExchange.c_str());
        }
        else {
            on_symbol_root_complete();
        }
    }
    return 0;
}

void dump_client_login(struct m2cl_login* p) {
    char sysname[21], acc[13], pwd[13];
    txstr(p->sysname, sysname, 20);
    txstr(p->acc, acc, 12);
    txstr(p->pwd, pwd, 12);
    Logf("%s req login: ver=%d, sysname=%s, acc=%s, pwd=%s",
        fmt_plain_head(&p->head),
        bcd32(p->ver), sysname, acc, pwd);
}

void dump_quote_request(struct m2cl_req_quote* p) {
    Logf("%s req quote: cmd=%c, feedid=%d, seqno=%ld",
        fmt_plain_head(&p->head),
        p->cmd, bcd32(p->feedid), bcd64(p->start_seqno));
}

int mdc_select(char* buf, size_t sz) {
    struct m2_head* head = (struct m2_head*)buf;
    int fmt = bcd32(head->fmt);
    size_t len = bcd64(head->len);
    switch (fmt) {
    case 0: // heartbeat
        mdc_parse_heartbeat(buf, sz);
        break;
    case 1: // login
        mdc_parse_login(buf, sz);
        break;
    case 2: // symbol info
        mdc_parse_symbol(buf, sz);
        break;
    case 3: // subscribe reply
        mdc_parse_sub_reply(buf, sz);
        break;
    case 4: // quote/tick/ba
        mdc_parse_mktdata(buf, sz);
        break;
    case 5:
        mdc_parse_message(buf, sz);
        break;
    case 6:
        mdc_parse_symbol_root(buf, sz);
        break;
    case 51:
        if (g_plain) {
            dump_client_login((struct m2cl_login*)buf);
        }
        Logf("client login");
        break;
    case 52:
        if (g_plain) {
            dump_client_symbol_request((struct m2cl_symbol*)buf);
        }
        Logf("client symbol request");
        break;
    case 53:
        if (g_plain) {
            dump_quote_request((struct m2cl_req_quote*)buf);
        }
        Logf("client quote request");
        break;
    case 56:
        if (g_plain) {
            dump_symbol_root_request((struct m2cl_symbol_root*)buf);
        }
        Logf("client symbol group request");
        break;
    default:
        LogErr("err: unknown mdc fmt: %d", fmt);
        break;
    }
    return 0;
}

void print_buf_as_text(const char* buf, int sz, FILE* outpf) {
    int i;
    for (i = 0; i < sz; ++i) {
        if ((i % 10) == 0) {
            fprintf(outpf, "%s", "\n\t");
        }
        fprintf(outpf, "%02x ", (unsigned char)buf[i]);
    }
    fprintf(outpf, "%s", "\n");
}

int drop(const char* reason, ringbuf_t* ring, size_t sz, FILE* outpf) {
    const int buf_size = 64 * 1024;
    char buf[buf_size];
    size_t dropsz = (sz > buf_size) ? buf_size : sz;
    ring_read(buf, dropsz, ring);
    fprintf(outpf, "drop: sz=%lu, reason=%s", dropsz, reason);
    print_buf_as_text(buf, dropsz, outpf);
    if (g_sav_fh) {
        fwrite(buf, 1, dropsz, g_sav_fh);
        fflush(g_sav_fh);
    }
    return 0;
}

int mdc_split(ringbuf_t* ring, char* buf, size_t* bytes_consumed, size_t* plen) {
    struct m2_head* head = (struct m2_head*)buf;
    *bytes_consumed = 0;
    *plen = 0;
    int markpos = ring_indexof(0xff, ring);
    if (markpos > 0) {
        *bytes_consumed += markpos;
        drop("align esc", ring, markpos, stderr);
    }
    else if (markpos < 0) { // not found
        if (ring_size(ring) > 0) {
            *bytes_consumed += ring_size(ring);
            drop("esc not found", ring, ring_size(ring), stderr);
        }
        return SHORT_ERR;
    }
    if (ring_size(ring) < MDC_MIN_SZ) {
        return SHORT_ERR;
    }
    ring_peek(buf, sizeof(struct m2_head), ring);
    size_t protlen = bcd32(head->len) + sizeof(struct m2_head);
    if (protlen > MDC_MAX_SZ) {
        *bytes_consumed += 1;
        Logf("err: protocol too long, should increase input buffer: need %lu", protlen);
        drop("prot too long", ring, 1, stderr);
        return SIZE_OVERFLOW_ERR;
    }
    else if (protlen < MDC_MIN_SZ) {
        *bytes_consumed += 1;
        drop("prot len too short", ring, 1, stderr);
        return CONTENT_ERR;
    }
    if (ring_size(ring) < protlen) {
        return SHORT_ERR;
    }
    ring_peek_offset(sizeof(struct m2_head), buf + sizeof(struct m2_head), protlen - sizeof(struct m2_head), ring);
    ring_remove(protlen, ring);
    *bytes_consumed += protlen;
    *plen = protlen;
    if (g_sav_fh) {
        fwrite(buf, 1, protlen, g_sav_fh);
        fflush(g_sav_fh);
    }
    return NOT_ERR;
}

void mdc_exaust(ringbuf_t* ring) {
    char buf[1024 * 1024];
    size_t consumed, protlen;
    enum SplitError splitres;
    while (1) {
        splitres = (SplitError)mdc_split(ring, buf, &consumed, &protlen);
        if (splitres == SHORT_ERR) {
            break;
        }
        switch (splitres) {
        case NOT_ERR:
            mdc_select(buf, protlen);
            continue;
        default:
            continue;
        }
    }
}

void read_config(const char* fn) {
    FILE* pf = fopen(fn, "rt");
    if (pf == NULL) {
        fprintf(stderr, "cannot open %s", fn);
        exit(EXIT_FAILURE);
    }
    gFtpPath[0] = '\0';
    g_mdc_addr[0] = '\0';

    char linebuf[256];
    char key[64];
    char value[256 - 64];
    while (fgets(linebuf, 256, pf) != NULL) {
        size_t linesz = strlen(linebuf);
        if (linesz > 0 && linebuf[0] == '#') {
            continue;
        }
        if (linebuf[linesz - 1] == '\n') {
            linebuf[linesz - 1] = '\0';
        }
        char* tok;
        tok = strtok(linebuf, "=");
        if (tok != NULL) {
            strcpy(key, tok);
            ctrim(key);
        }
        else {
            continue;
        }
        tok = strtok(NULL, "");
        if (tok != NULL) {
            strcpy(value, tok);
            ctrim(value);
        }
        else {
            continue;
        }
        if (strcmp(key, "user") == 0) {
            sprintf(g_account, "%s", value);
        }
        else if (strcmp(key, "pwd") == 0) {
            sprintf(g_pwd, "%s", value);
        }
        else if (strcmp(key, "sysname") == 0) {
            sprintf(g_sysname, "%s", value);
        }
        else if (strcmp(key, "addr") == 0) {
            sprintf(g_mdc_addr, "%s", value);
        }
        else if (strcmp(key, "ftp_path") == 0) {
            sprintf(gFtpPath, "%s", value);
            int slen = strlen(gFtpPath);
            if (gFtpPath[slen - 1] == '/') {
                gFtpPath[slen - 1] = '\0';
            }
        }
        else if (strcmp(key, "sleep_on_KB") == 0) {
            gSleepKB = atoi(value);
        }
        else if (strcmp(key, "ba_refresh_nsec") == 0) {
            gRefreshInterval = atol(value);
        }
        else if (strcmp(key, "out_url1") == 0) {
            strcpy(g_outurl1, value);
        }
        else if (strcmp(key, "out_url2") == 0) {
            strcpy(g_outurl2, value);
        }
        else if (strcmp(key, "log_rotate_days") == 0) {
            g_log_rotate_days = atol(value);
        }
    }
    fclose(pf);
}

void read_exchanges_list() {
    char line[64];
    FILE* fp = fopen("exchanges.txt", "rt");
    if (fp == NULL) {
        fprintf(stderr, "read exchanges.txt fail\n");
        exit(0);
    }
    while (fgets(line, 64, fp) != NULL) {
        ctrim(line);
        if (line[0] == '#') {
            continue;
        }
        gExchanges.push_back(line);
    }
    fclose(fp);
}

void print_subscribe_table() {
    FILE* pf = fopen("subtbl.txt", "w+t");
    if (pf == NULL) {
        return;
    }
    simap_t::iterator fi = g_subtbl.begin();
    for (; fi != g_subtbl.end(); fi++) {
        fprintf(pf, "%s: %d\n", fi->first.c_str(), fi->second);
    }
    fclose(pf);
}

int main(int argc, char** argv) {
    strcpy(g_cfg_fn, "default.config");
    read_args(argc, argv);
    time_t now_timet = time(NULL);
    char logfn[256];
    sprintf(logfn, "spf2_%d.log", to_ymd(now_timet));
    init_log(logfn, "wt");
    Logf("spf2 ver:%s", SPF2_VER);

    rotate_log("spf2_????????.log", 5, g_log_rotate_days);

    read_config(g_cfg_fn);

    read_exchanges_list();
    LoadSpotTxt("stock.txt");

    char fn[256];
    sprintf(fn, "%s/GWHSTM.TXT", gFtpPath);
    LoadMonthTable('f', fn);
    sprintf(fn, "%s/MHSMB.TXT", gFtpPath);
    LoadMonthTable('o', fn);
    LoadSubscribeTableAPEXHSTB();
    print_subscribe_table();

    ringbuf_t g_ring;
    ring_init(1024 * 1024, &g_ring);

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    int ret = sigaction(SIGINT, &sa, NULL);
    if (ret == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char shmname[256];
    sprintf(shmname, "spf2shm.%s", SHM_VER);
    spf2shm_init(shmname, 1);
    Logf("shm ver=%d, size=%ld", gsm->shm_version, gsm->shm_size);

    g_zmqctx = zmq_ctx_new();
    if (!g_zmqctx) {
        LogErr("err: cannot create zmq context");
        exit(1);
    }
    g_zout1 = open_zmq_out(g_outurl1, ZMQ_PUSH, 0);
    g_zout2 = open_zmq_out(g_outurl2, ZMQ_PUSH, 0);
    if (g_zout1==NULL && g_zout2==NULL) {
        LogErr("err: all 2 zmq out socket NULL");
        exit(1);
    }

    char buf[1024];
    g_mdc_sock = mdc_reconnect(g_mdc_addr);
    if (g_mdc_sock!=-1) {
        on_mdc_connect();
    }
    while (1) {
        int readcnt;
        if (g_in_fh) {
            readcnt = mdc_read_stdin(g_in_fh, &g_ring);
            if (readcnt == -1) {
                fprintf(stderr, "eof\n");
                break;
            }
        }
        else {
            readcnt = mdc_read_async(g_mdc_sock, &g_ring);
            if (readcnt == -1) {
                mdc_close(g_mdc_sock);
                g_mdc_sock = -1;
                g_mdc_sock = mdc_reconnect(g_mdc_addr);
                if (g_mdc_sock!=-1) {
                    on_mdc_connect();
                }
            }
        }
        mdc_exaust(&g_ring);
    }
    destroy_app();
    return 0;
}
