send price scale
    0;0,0,0,1,1,1|2;0,0,0,2,256,1
    0	/* 預設(其他) */
	1	/* 最近月 */
	2	/* 價差商品 */
	Mdct_Price		ScopeMin;			/* 價格範圍最小值(下限) */
	Mdct_Price		ScopeMax;			/* 價格範圍是大值(上限) */
	Mdct_ScopeMode	ScopeMode;			/* 價格範圍比較模式 */
	Mdct_Float		Numerator;			/* 價格基準分子 */
	Mdct_Float		Denominator;		/* 價格基準分母 */
	Mdct_Int		MinMovement;		/* 最小跳動點數 */
sigint cannot kill process    


2022/2/18
    open shared memory to store data
    read data from socket
2022/2/21
    read zmq output setting
    read tcp input setting
    log rotate
    need position parameter as config filename, if obsent, use default.config
    send login
    receive login
    receive heartbeat
2022/2/22
    log IO
    parse tick
2022/2/24
    parse symbol
    parse symbol group
    put symbol info into index
    put symbol info into shm
    parse ba
2022/3/1
    parse quote
2022/3/2
    log parsed data
2022/3/4
    sinopac specific indexes
2022/3/8
    read symbols to request from ftpdata
2022/3/17
    parse market data protocol
2022/3/25
    set category_id
    set is_farmonth
    set is_nearmonth
    gAlterCommrootMap
    gTHostSet
    subscribe exchanges
    send symbol
    subscribe symbol
2022/3/28
    send ba
    convertOptionCode
2022/3/30
    print subscribe table
    處理併筆格式
    spf1.0的很多zmq send沒有實做到2.0
    open event
