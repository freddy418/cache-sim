#ifndef MONDRV_H
#define MONDRV_H

#include "utils.h"
#include "store.h"
#include "tcache.h"
#include "memmap.h"
#include "freader.h"
#include "coredrv.h"

class mondrv {
  i32 taggran;
  i32 tagsize;
  i32 mapon;
  i32 rdalloc;
  i32 l1assoc;
  i32 l1sets;
  i32 l2assoc;
  i32 l2sets;
  i32 bsize;
  i32 skip;
  i32 pagesize;
  float l1_read_energy;
  float l1_write_energy;
  float l2_read_energy;
  float l2_write_energy;
  tcache* dl1;
  tcache* dl2;
  mem_map* mp;
  freader* fr;
  i32 done;
  std::queue<cache_req*>* qp;
  tmemory* sp;
  // for performance
  sim_req curr_req;
  i64 curr_ic;
  // for statistic collection
  char* name;
  i64 mismatches;
  i64 mismaps;
  i32 nbupdates;
  i64 qstallcyc;
  i64 totaligap;
  i64 lastic;
  i32 accesses;
  i64 warm_ic;
  i64 warm_ck;
  i32 warm_accs;
  i64 clocks;
  i64 totaldelay;
  i32 delays[MEM_LVLS];
  cache_req* temp_req;
public:
  mondrv(core_args* args);
  void stats();
  i32 clock();
  i32 get_accs();
  i64 get_ic();
  void set_done();
};

#endif /* MONDRV_H */
