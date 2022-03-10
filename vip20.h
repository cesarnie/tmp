
#ifndef __VIP20__
#define __VIP20__

#include <inttypes.h>

#define JsonColLen 41
#define JsonValLen 128
#define JsonEventTimeLen 24
#define JsonMaxColListCnt 40
#define JsonMaxTickCount 80
#define JsonMaxBADepth 20
#define MaxPriceScaleItems 10

struct ColPair {
    char ColName[JsonColLen];
    char ColVal[JsonValLen];
};

struct PVT {
    long Price;
    int Vol;
    int Type;
};

struct Vip2UpdateEvent {
    int Mode;
    int Version;
    char FilterCol[JsonColLen];
    char FilterVal[JsonValLen];
    char EventTime[JsonEventTimeLen];
    int EventType;
    int ColListCnt;
    struct ColPair ColList[JsonMaxColListCnt];
};

struct ScaleItem {
    int ScopeMin;
    int ScopeMax;
    int Numerator;
    int Denominator;
    int MinMovement;
};

struct Vip2AddNews {
    int Mode;
    char Time[JsonValLen];
    char Mkt[JsonValLen];
    char Src[JsonValLen];
    char NewsType[JsonValLen];
    char HeadLine[JsonValLen];
    int LinesCnt;
    char* Body;
};

struct Vip2PriceScaleUpdateEvent {
    int Mode; // 63
    char Symbol[JsonValLen];
    char SecType[JsonValLen];
    int ScaleCount;
    char Time[JsonValLen];
    struct ScaleItem Scales[MaxPriceScaleItems];
};

struct Vip2Quote {
    char LastTime[JsonValLen];
    int PreClose;
    int PreVol;
    int Open;
    int High;
    int Low;
    int Price;
    int Change;
    int ChangeRatio;
    int BidPrice;
    int BidVol;
    int AskPrice;
    int AskVol;
    int WeekHigh52;
    int WeekLow52;
    int TotVol;
    char TotAmt[JsonValLen];
    int BuyCnt;
    int SellCnt;
    char TradeStat[JsonValLen];
    char OrderStat[JsonValLen];
    int MatchSide;
};

struct Vip2Tick {
    int TradeType;
    char Plottable[JsonValLen];
    int UpdStat;
    char TickTime[JsonValLen];
    int TotalVol;
    char TotAmt[JsonValLen];
    int64_t TotCnt;
    int PriceCnt;
    struct PVT Parr[JsonMaxTickCount];
    int NoVolCnt;
};

struct Vip2AddTick {
    int Mode;
    int Version;
    char Symbol[JsonValLen];
    char SecType[JsonValLen];
    struct Vip2Tick Tick;
    //struct Vip2Quote Quote;
};

struct Vip2BAItem {
    long BAPrice;
    int BAVol;
};

struct Vip2BA {
    char BATime[JsonValLen];
    int TradeType;
    int MDFeedT;
    int BidCnt;
    int AskCnt;
    struct Vip2BAItem Bid[JsonMaxBADepth];
    struct Vip2BAItem Ask[JsonMaxBADepth];
    int FBidPrice;
    int FBidVol;
    int FAskPrice;
    int FAskVol;
    //char Exchange[JsonValLen];
};

struct Vip2UpdateBA {
    int Mode;
    int Version;
    char Symbol[JsonValLen];
    char SecType[JsonValLen];
    struct Vip2BA BA;
};
#endif
