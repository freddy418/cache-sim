PROG = cache_sim
CC = g++ -g
SRCS = utils.cpp plru.cpp store.cpp memmap.cpp mtcache.cpp tcache.cpp freader.cpp coredrv.cpp mondrv.cpp cache_sim.cpp
OBJS = ${SRCS:.cpp=.o}
CFLAGS = -g -O3 -fno-omit-frame-pointer -DTARGET_IA32 -DHOST_IA32 -DTARGET_LINUX
#CFLAGS += -DTEST
#CFLAGS+= -DSTATIC_DICT -DCOMPRESS_WB
#CFLAGS += -DDBG -DDBG_ADDR=4288469413

.SUFFIXES: .o .cpp

.cpp.o :
	$(CC) $(CFLAGS) -c $? -o $@

all : $(PROG)

$(PROG) : $(OBJS)
	$(CC) $^ -o $@ -lm

clean :
	rm -rf $(PROG) *.o
