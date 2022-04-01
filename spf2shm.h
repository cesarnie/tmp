#ifndef __VIP2SHM_H__
#define __VIP2SHM_H__

#include <stdint.h>
#include <semaphore.h>

#include "spf2types.h"
#include "vip20.h"

#define SHM_VER "0"
#define SHM_NAME "spf2shm." SHM_VER
#define MAX_SYMBOL_ROOTS 20000
#define MAX_SYMBOLS 500000
#define MAX_BYTE_FLAGS (4 * 1024)
#define MAX_INT_FLAGS 1024
#define MAX_INT64_FLAGS 512

#define MaxTickScaleCnt 12
#define MaxEventQueueEntries 5000
#define get_midx(x) ((x)-gsm->symbols)

enum BYTE_FLAG_NAMES {
    BF_SHM_INITIALIZED, // for file lock
    BF_FLAG_CNT
};

struct QuoteEvent {
    uint32_t midx;
    int64_t field_map; //64 fields
};

struct EventQueue {
    struct QuoteEvent data[MaxEventQueueEntries];
    uint32_t writepos;
};

struct pv {
    int64_t price;
    int vol;
};

struct SymbolInfo {
    char symbol[25];
    char name[49];
    char exchange[13];
    char root_ext[13]; //EW(週期貨)在這裡會被換成ES，原來的EW會放在original_root
    char root[13];     //EW
    unsigned char decimals; //
    char type_fg;           //
    char apex_symbol_type;  //X,R,F,S,O
    int duemon;        //yyyymm
    int native_duemon; //yyyymm
    int start_date;
    int settle_date;
    int first_notice_date;
    int second_notice_date;
    int end_date;
    int last_trade_date;
    int lot_size;
    char strike_price[13];
    char extra_fg;
    char scale[73];
    int tick_ymd;
    uint32_t tick_hmsf;
    int ba_ymd;
    uint32_t ba_hmsf;
    struct pv bid[20];
    struct pv ask[20];
    int session_status;      //
    int session_date;        //
    int64_t rise_limit;      //
    int64_t fall_limit;      //
    int64_t open_ref;        //
    int64_t close;           //
    int64_t settlement;      //
    int64_t prev_close;      //
    int64_t prev_settlement; //
    int64_t prev_oi;         //
    int64_t open;            //
    int64_t high;            //
    int64_t low;             //
    int32_t tick_vol;        //
    int64_t tot_vol;         //
    double tick_value;       //
    double tot_value;        //
    int64_t balance;         // 合约持仓量
    int64_t oi;              // 当前未平仓量
    char price_offset;       // 价位于委托簿位置
    char trade_status;       // 交易狀態
    uint8_t category_id;     // 100: 現貨, data comes from APEXHSTB
    uint8_t is_farmonth;
    uint8_t is_subscribed;
    char far_alias[25];
    int far_alias_midx;
    uint8_t is_nearmonth;
    char near_alias[25];
    int near_alias_midx;
    int feedID;                      //
    uint8_t scale_cnt;               //
    uint8_t scale_from_root;         //
    struct ScaleItem scale_items[5]; //
};

struct SymbolRootInfo {
    char exchange[13];
    char group_code[25];
    char group_name[49];
    char sessions[257];          //
    char scales[385];            //
    char deriv_exchange[25];     //
    char deriv_symbol[25];       //
    int contract_multiplier_dec; //
    int64_t contract_multiplier; //
    char currency[9];            //
    uint8_t fee_dec;             //
    int64_t fee;                 //
    int tax_dec;                 //
    int64_t tax;                 //
    int lot_size;                //
    char category;               //
    int timezone;                //
    char extension;              //
    int scale_cnt;               //
    struct TypedScaleItem scale_items[5]; //
};

struct Spf2Shm {
    int32_t shm_version;
    int64_t shm_size;
    struct SymbolRootInfo roots[MAX_SYMBOL_ROOTS];
    struct SymbolInfo symbols[MAX_SYMBOLS];
    int32_t symbol_cnt;
    int32_t symbol_group_cnt;
    struct EventQueue events;
    int8_t byte_flags[MAX_BYTE_FLAGS];
    int32_t int_flags[MAX_INT_FLAGS];
    int64_t int64_flags[MAX_INT64_FLAGS];
};

extern struct Spf2Shm* gsm;

int spf2shm_is_initialized();
int spf2shm_init(const char* name, int create_exclusive);
int spf2shm_destroy();
void add_event(uint32_t midx, int64_t fm);
struct SymbolInfo* alloc_symbol();
struct SymbolRootInfo* alloc_symbol_root();

#endif
