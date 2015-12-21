#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>
#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <queue>
#include <algorithm>

typedef unsigned int i32;
typedef unsigned long long i64;

extern i64 curr_ck;
extern float mem_energy;

// cache latencies
// core caches
#define CL1DELAY 2
#define CL2DELAY 10
// metadata caches
#define ML1DELAY 1
#define ML2DELAY 8
// memory
#define MEMDELAY 200

// cache energies
// TLB reads
#define TLB1RE 0.0130335e-9
#define TLB2RE 0.0287198e-9
// TLB writes
#define TLB1WE 0.0130335e-9
#define TLB2WE 0.0287198e-9
// core caches
#define CL1WE (0.435469e-9)*.75
#define CL1RE (0.435469e-9)*.75
#define CL2RE 0.182316e-9
#define CL2WE 0.182316e-9
// metadata caches
#define ML1RE 0.0527827e-9
#define ML1WE 0.0527827e-9
#define ML2RE 0.104604e-9
#define ML2WE 0.104604e-9
// DRAM
#define MEMRE 4e-9 // todo: what's typ dram read energy?
#define MEMWE 4e-9 // todo: what's typ dram read energy?

#define QSIZE 16
#define PSIZE 12 // log2 of page size in bytes

// lru implementation
typedef struct llnode {
   unsigned int val;
   struct llnode * next;
} item;

unsigned int pow2(unsigned int v);

typedef struct cache_blk_t
{
  i32 valid;
  i32 dirty;
  i32 tag;
  i64 * value;
} cache_block;

typedef struct crdata_t
{
  i64 value;
  i32 delay;
} crdata;

typedef struct cache_req_t
{
  i32 valid;
  i32 ismem;
  i32 isRead;
  i32 addr;
  i64 icount;
  i64 value;
} cache_req;

typedef struct sim_req_t
{
  i32 valid;
  i32 inprogress;
  i32 isRead;
  i32 addr;
  i64 icount;
  i64 value;
  i64 fill_cycle;
} sim_req;

typedef struct dict_struct {
  i64 * v; // actual values used from compression
#ifndef STATIC_DICT
  i64 * c; // candidates
  i64 * rc;
  item * plru;
#endif
} comp_dict;

typedef struct range_cache_entry {
  i64 value;  // value of entry
  i32 la; // start address of range
  i32 ha; // end address of range
  i32 dirty;
  i32 valid;
} rc_ent;

#endif /* UTILS_H */
