#ifndef __SPF2_H__
#define __SPF2_H__

struct StringPair {
    std::string str1;
    std::string str2;
};

struct MonthsTable {
    int count;
    int mons[20];
    char category[64];
};

struct SpotItem {
    char exchange[24];
    char cls[24];
    char symbol[24];
    char name[256];
    char future[24];
};

enum MDC_SessionStatus {
    SS_STOP = 0,    /* 停止行情處理(不處理行情) */
    SS_START = 1,   /* 開始行情處理 */
    SS_CLEAR = 2,   /* 清除行情 */
    SS_OPEN = 3,    /* 行情開盤 */
    SS_CLOSE = 4,   /* 行情收盤 */
    SS_PREOPEN = 5, /* 重新收單(試撮) */
    SS_SUSPEND = 6, /* 休盤或暫停交易 */
    SS_VOID = 8,    /* 無法處理交易時段的商品(不處理行情) */
    SS_REMOVE = 9   /* 到期商品(不處理行情) */
};

enum SplitError {
    SHORT_ERR,
    CHECKSUM_ERR,
    TERMINAL_CODE_ERR,
    ENVELOP_ERR,
    CONTENT_ERR,
    SIZE_OVERFLOW_ERR,
    NOT_ERR
};

enum QuoteFieldMask {
    QM_SESSION_STATUS = 0x0001,
    QM_SESSION_TYPE = 0x0002,
    QM_TRADEDATE = 0x0004,
    QM_RISE_LIMIT = 0x0008,
    QM_FALL_LIMIT = 0x0010,
    QM_OPEN_REF = 0x0020,
    QM_CLOSE = 0x0040,
    QM_SETTLEMENT = 0x0080,
    QM_PREV_CLOSE = 0x0100,
    QM_PREV_SETTLEMENT = 0x0200,
    QM_PREV_OI = 0x0400,
    QM_OPEN = 0x0800,
    QM_HIGH = 0x1000,
    QM_LOW = 0x2000
};

typedef std::map<std::string, int> simap_t;
typedef std::map<std::string, struct StringPair> s2spairmap_t;
typedef std::map<std::string, std::string> s2smap_t;

#define MDC_MAX_SZ (1024 * 1024)
#define MDC_MIN_SZ 12

#define TRADE_CLEAR 0
#define TRADE_OPEN 2
#define TRADE_CLOSE 7

#endif
