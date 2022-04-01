P=spf2
CC=g++
PROGS=spf2
#CPP=spf2.cpp ../inc/mdcapi.cpp spf2apx.cpp vip2json.cpp printer.cpp cpu.c -lzmq
CPP=spf2.cpp ringbuffer.cpp log.cpp tools.cpp spf2shm.cpp fmtjson.cpp -lrt -lzmq
OBJECTS = spf2.o ringbuffer.o log.o tools.o spf2shm.o fmtjson.o
CFLAGS=-g -O0 -Wall -Wno-unused-variable -Wno-unused-but-set-variable
CPPFLAGS=-g -O0 -Wall -Wno-unused-variable -Wno-unused-but-set-variable
LDLIBS = -lrt -lzmq

# Default Rules:
$(P): $(OBJECTS)
	$(CC) $(CPPFLAGS) -o $(P) $(OBJECTS) $(LDLIBS)

#all:
#	$(CC) $(CFLAGS) $(CPP) -o $(PROGS)


