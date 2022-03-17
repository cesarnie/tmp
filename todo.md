on symbol complete:
    set category_id
    set is_farmonth
    set is_nearmonth
    gAlterCommrootMap
    gTHostSet
    subscribe exchanges
    
send symbol
subscribe symbol


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
    