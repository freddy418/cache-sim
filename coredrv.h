#ifndef COREDRV_H
#define COREDRV_H

#include "utils.h"
#include "store.h"
#include "tcache.h"
#include "memmap.h"
#include "freader.h"

typedef struct core_args_t {
  i32 taggran;
  i32 tagsize;
  i32 pagesize;
  i32 mapon;
  i32 assoc;
  i32 sets;
  i32 bsize;
  i32 skip;
  char * dir;
  char * bench;
  char * name;
  std::queue<cache_req*>* qp;
  tmemory* sp;
} core_args;

class coredrv {
  i32 taggran;
  i32 tagsize;
  i32 mapon;
  i32 sets;
  i32 bsize;
  i32 assoc;
  i32 pmask;
  i32 skip;
  i32 pagesize;
  tcache* dl1;
  tcache* dl2;
  mem_map* mp;
  freader* fr;
  std::queue<cache_req*>* qp;
  tmemory* sp;
  // for performance
  sim_req curr_req;
  i64 curr_ic;
  // for statistic collection
  char* name;
  i64 mismatches;
  i64 mismaps;
  i32 m1cyc;
  i32 m20cyc;
  i32 m200cyc;
  i32 qstallcyc;
  i32 totaligap;
  i32 lastic;
  i32 accesses;
  i64 warm_ic;
  i64 warm_ck;
  i32 clocks;
  cache_req* temp_req;
public:
  coredrv(core_args* args);
  void stats();
  i32 clock(i32 curr_ck);
  i32 get_accs();
};

#endif /* COREDRV_H */
