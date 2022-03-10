#include <stdio.h>

#include "vip20.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

int MakeJsonAddTick(struct Vip2AddTick* t, char* buf, size_t buflen) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);

    w.StartObject();

    w.Key("11000");
    w.Int(t->Mode);
    w.Key("15000");
    w.Int(t->Version);
    w.Key("48");
    w.String(t->Symbol);
    w.Key("11001");
    w.String(t->SecType);
    w.Key("11500");
    w.StartObject();
    w.Key("339");
    w.Int(t->Tick.TradeType);
    w.Key("12000");
    w.String(t->Tick.Plottable);
    w.Key("12001");
    w.Int(t->Tick.UpdStat);
    w.Key("273");
    w.String(t->Tick.TickTime);
    w.Key("14");
    w.Int(t->Tick.TotalVol);
    w.Key("2162");
    w.String(t->Tick.TotAmt);
    w.Key("1179");
    w.Int(t->Tick.PriceCnt);

    w.Key("12010");
    w.StartArray();
    w.StartObject();
    w.Key("31");
    w.Int64(t->Tick.Parr[0].Price);
    w.Key("1020");
    w.Int(t->Tick.Parr[0].Vol);
    w.Key("12031");
    w.Int(t->Tick.Parr[0].Type);
    w.EndObject();
    w.EndArray();

    w.Key("12027");
    w.Int(t->Tick.NoVolCnt);
    w.EndObject();
    w.EndObject();

    if ((sb.GetSize() + 1) > buflen) {
        return -1;
    }
    int len = sprintf(buf, "%s", sb.GetString());
    return len;
}

// returns the size in bytes of buffer filled,
// if bufout not enough, returns -1
int MakeJsonUpdateEvent(struct Vip2UpdateEvent* si, char* bufout, size_t bufoutlen) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);

    w.StartObject();
    w.Key("11000"); // mode
    w.Uint(si->Mode);
    w.Key("15000"); // version
    w.Uint(si->Version);
    w.Key("12014"); // FilterCol
    w.String(si->FilterCol);
    w.Key("12015"); // FilterVal
    w.String(si->FilterVal);
    w.Key("273"); // EventTime
    w.String(si->EventTime);
    w.Key("12013"); // EventType
    w.Uint(si->EventType);
    w.Key("870"); // ColListCnt
    w.Uint(si->ColListCnt);
    w.Key("12016");

    w.StartArray();
    for (int i = 0; i < si->ColListCnt; ++i) {
        w.StartObject();
        w.Key("871"); // ColName
        w.String(si->ColList[i].ColName);
        w.Key("872"); // ColVal
        w.String(si->ColList[i].ColVal);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    if ((sb.GetSize() + 1) > bufoutlen) {
        return -1;
    }
    int len = sprintf(bufout, "%s", sb.GetString());
    return len;
}
