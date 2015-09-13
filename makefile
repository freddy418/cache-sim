PROG = cache_sim
CC = g++ -g -pthread
SRCS = utils.cpp store.cpp memmap.cpp tcache.cpp freader.cpp cache_sim.cpp
OBJS = ${SRCS:.cpp=.o}
CFLAGS = -g -O3 -fno-omit-frame-pointer -pthread -DTARGET_IA32 -DHOST_IA32 -DTARGET_LINUX
#CFLAGS += -DTEST
#CFLAGS+= -DSTATIC_DICT -DCOMPRESS_WB
#CFLAGS += -DDBG -DDBG_ADDR=4290170001 -DDBG_ACCS=0

.SUFFIXES: .o .cpp

.cpp.o :
	$(CC) $(CFLAGS) -c $? -o $@

all : $(PROG)

$(PROG) : $(OBJS)
	$(CC) $^ -o $@ -lm

clean :
	rm -rf $(PROG) *.o
