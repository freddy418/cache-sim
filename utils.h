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

#define L1DELAY 1
#define L2DELAY 20
#define MEMDELAY 200
#define QSIZE 16

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

typedef struct cache_set
{
  cache_block* blks;
  item* lru;
} cache_set;

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
