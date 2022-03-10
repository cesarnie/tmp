#ifndef __VIP2_TYPES_H__
#define __VIP2_TYPES_H__

#pragma pck(1)

struct m2_head {
    char begin;   //0xff
    char fmt[1];  //
    char ver[1];  //
    char time[5]; //hhmmssffff
    char len[4];  //body len, without head (12 bytes)
};

struct m2_heartbeat {
    struct m2_head head; // 0: from server; 51: from client
};

struct m2cl_login {
    struct m2_head head; //
    char ver[2];         //
    char sysname[20];    //
    char acc[12];        //
    char pwd[12];        //
};

struct m2cl_symbol {
    struct m2_head head; // 52
    char exchange[12];   //
};

struct m2cl_req_quote {
    struct m2_head head; // 53
    char cmd;            //
    char feedid[1];      //
    char start_seqno[8]; // mmddhhmm+ssssssss
};

struct m2cl_symbol_group {
    struct m2_head head; // 56
    char exchange[12];   //
};

struct m2_privilege_rec {
    char feed_id[1];   //
    char feed_type;    //
    char exchange[12]; //
};

struct m2sv_login {
    struct m2_head head;                             // 1
    char result;                                     // 'Y': login success; 'N': login fail
    char message[64];                                //
    char expire[4];                                  // yyyymmdd
    char priv_cnt[2];                                // privilege info count
    char tail[99 * sizeof(struct m2_privilege_rec)]; // array of m2_privilege_rec
};

struct m2_symbol_rec {
    char symbol[24];            //
    char name[48];              //
    char kind[12];              //
    char duemon[3];             // yyyymm
    char native_duemon[3];      // yyyymm, 交易所原生契約月份
    char start_date[4];         // 上市日期
    char settle_date[4];        // 結算日期
    char first_notice_date[4];  // 第一通知日期
    char second_notice_date[4]; // 第二通知日期
    char end_date[4];           // 市場最後交易日
    char last_trade_date[4];    // 最後可交易日期
    char type_fg;               // 商品類別
    char lot_size[4];           // 最小單位股數
    char strike_price[12];      // 履約價
    char extra_fg;              // 商品類別附加資訊
    char scale[72];             // 價格間距資訊
};

struct m2sv_symbol {
    struct m2_head head;                    //
    char result;                            // 'Y'成功; 'N':失敗; 'L':成功最後一包
    char exchange[12];                      //
    char symbol_cnt[2];                     // max 500
    char tail[500 * sizeof(m2_symbol_rec)]; //
};

struct m2sv_sub_reply {
    struct m2_head head; //
    char result;         // 'Y'; 'N'
    char cmd;            // 'S': 純訂閱; 'X':快照+訂閱; 'U':解訂閱
    char feed_id[1];     // 副本ID, 1~99
    char seqno[8];       // 起始序號 mmddhhmm+ssssssss
    char message[96];    //
};

struct m2_quote {
    char exist_fg[2];             //
    char update_fg[2];            //
    char trade_session_status[1]; //
    char trade_session_type[1];   //
    char trade_date[4];           //
    char rise_limit[7];           //
    char fall_limit[7];           //
    char open_ref[7];             //
    char close[7];                //
    char settlement[7];           //
    char prev_close[7];           //
    char prev_settlement[7];      //
    char prev_oi[5];              //
    char open[7];                 //
    char high[7];                 //
    char low[7];                  //
};

struct m2_tick {
    char fg;               // 成交行情 組合加工模式 旗號
    char cnt[1];           // 以下成交行情 加工筆數
    char date[4];          // yyyymmdd
    char time[5];          // hhmmssffff
    char price[7];         //
    char tickvol[3];       //
    char totvol[6];        //
    char bid[7];           //
    char bidvol[3];        //
    char ask[7];           //
    char askvol[3];        //
    char value_dec[1];     // 成交金額小數位
    char value[5];         //
    char tot_value_dec[1]; // 總成交金額小數位
    char tot_value[6];     // 總成交金額數值
    char balance[5];       // 合約持倉量
    char oi[5];            // 當前未平倉量
    char ba_offset;        // 價位於委託簿位置
    char trade_status;     // 交易狀態
};

struct m2_tick_ext {
    char fg;               // 成交行情 組合加工模式 旗號
    char cnt[1];           // 以下成交行情 加工筆數
    char date[4];          // yyyymmdd
    char time[5];          // hhmmssffff
    char price[7];         //
    char tickvol[3];       //
    char totvol[6];        //
    char bid[7];           //
    char bidvol[3];        //
    char ask[7];           //
    char askvol[3];        //
    char value_dec[1];     // 成交金額小數位
    char value[5];         //
    char tot_value_dec[1]; // 總成交金額小數位
    char tot_value[6];     // 總成交金額數值
    char balance[5];       // 合約持倉量
    char oi[5];            // 當前未平倉量
    char ba_offset;        // 價位於委託簿位置
    char trade_status;     // 文易狀態
    char open[7];          //
    char high[7];          //
    char low[7];           //
};

struct m2_pv {
    char price[7]; //
    char vol[3];   //
};

struct m2_ba {
    char date[4];                          // utc
    char time[5];                          // utc
    char depth[1];                         // 重覆筆數
    char tail[100 * sizeof(struct m2_pv)]; // for max depth of 50
};

struct m2sv_quote {
    struct m2_head head;                                               //
    char orig_feed_id[1];                                              // 源頭行情副本
    char feed_id[1];                                                   // 實際行情副本
    char seqno[8];                                                     //
    char exchange[12];                                                 //
    char symbol[24];                                                   //
    char decimals[1];                                                  //
    char data_type;                                                    // ‘R’:即時, ‘P’:回補, ‘S’:快照
    char content_fg;                                                   // 接續行情內容 (X)+(Y)+(Z)
    char tail[sizeof(m2_quote) + sizeof(m2_tick_ext) + sizeof(m2_ba)]; //;
};

struct m2sv_message {
    struct m2_head head; //
    char msgid[2];       //
    char msglen[2];      //
    char tail[10000];    //
};

struct m2_symbol_group_rec {
    char group_code[24];             //
    char group_name[48];             //
    char sessions[256];              //
    char scales[384];                //
    char deriv_exchange[24];         //
    char deriv_symbol[24];           //
    char contract_multiplier_dec[1]; //
    char contract_multiplier[5];     //
    char currency[8];                //
    char fee_dec[1];                 //
    char fee[5];                     //
    char tax_dec[1];                 //
    char tax[5];                     //
    char lot_size[4];                //
    char category;                   //
    char timezone[2];                //
    char extension;                  //
};

struct m2sv_symbol_group {
    struct m2_head head; //
    char result;         // 'Y'成功; 'N':失敗; 'L':成功最後一包
    char exchange[12];   //
    char cnt[2];         // max 200
    char tail[200 * sizeof(struct m2_symbol_group_rec)];
};

#pragma pack()

#endif
