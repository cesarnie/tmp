P=spf2tool
CC=g++
PROGS=spf2tool
CPP=spf2tool.cpp log.cpp tools.cpp spf2shm.cpp -lrt
OBJECTS = spf2tool.o log.o tools.o spf2shm.o
CFLAGS=-g -O0
CPPFLAGS=-g -O0
LDLIBS = -lrt

# Default Rules:
$(P): $(OBJECTS)
	$(CC) $(CPPFLAGS) -o $(P) $(OBJECTS) $(LDLIBS)



