#ifndef MTCACHE_H
#define MTCACHE_H

#include "utils.h"
#include "memmap.h"
#include "store.h"
#include "plru.h"

//#define LINETRACK 1

// cache implementation
typedef struct cache_set
{
  cache_block* blks;
  plru* repl;
} mtcache_set;

class mtcache {
  mtcache_set* sets;
  i32 compress_writeback;
  // modifications to support multi-granularity tags - 3/10/15
  i64 tmask;
  i32 tshift;
  i32 tsize;
  i64 tgmask;
  i32 gshift;
  // end modificiations
  i32 nsets;
  i32 bsize;
  i32 bvals;
  i32 assoc;
  i32 imask;
  i32 bmask;
  i32 ishift;
  i32 bshift;
  i32 oshift;
  i32 amask;
  i64 accs;
  i64 hits;
  i64 misses;
  i64 writebacks;
  i64 allocs;
  i64 bwused;
  i32 anum;
  i32* acount;
  i32* mcount;
  mtcache* next_level;
  tmemory* mem;
  mem_map* map;
  char * name;
  i32 hitdelay;
  i32 missdelay;
  float total_energy;
  float read_energy;
  float write_energy;
 public:
  mtcache(i32 ns, i32 as, i32 bs, i32 tg, i32 ts, i32 cw, i32 hd, i32 md, float re, float we);
  crdata read(i32 addr); // returns tuple of delay+data
  crdata readw(i32 addr); // returns tuple of delay+data
  i32 write(i32 addr, i64 data); // returns delay
  void writeback(cache_block* bp, i32 addr);
  i32 refill(cache_block* bp, i32 addr); // returns delay (of next level)
  void stats();
  void copy(i32 addr, cache_block* op);
  void allocate(i32 addr);
  void touch(i32 addr);
  void clearstats();
  void set_mem(tmemory* sp);
  void set_map(mem_map* mp);
  void set_nl(mtcache* cp);
  void set_name(char * cp);
  void set_anum(i32 n);
  i64 get_accs();
  i64 get_hits();
  void set_accs(i64 num);
  void set_hits(i64 num);
};

#endif /* MTCACHE_H */
