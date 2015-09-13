#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "utils.h"

typedef struct ent_struct {
  i64 zero; // makeshift bit vector for zero
} map_entry;

typedef struct tlb_ent_struct {
  i32 valid;
  i32 dirty;
  i32 tag;
  map_entry* entry;
} tlb_entry;

typedef struct map_cache_struct {
  tlb_entry* entries;
  item* lru;
  i32 nents;

  i32 accs;
  i32 hits;
  i32 misses;
  i32 zeros;
} mm_cache;

// Core 2 had a 16 entry L1 TLB and 256 entry L2 TLB
// Nehalem had a 64 entry L1 TLB and 512 entry L2 TLB

class mem_map {
  mm_cache* tlb; // L1 tlb
  mm_cache* tlb2; // L2 tlb - updated on eviction
  map_entry * mapents;
  
  i32 pshift;
  i32 bshift;
  i32 bmask;
  i32 enabled;
  i32 os;

  i32 nents;
  i32 psize;
  i32 bsize;
  i64 bwused;
  i32 bvsize;

 public:
  mem_map(i32 enable, i32 ps, i32 bs, i32 cs, i32 tg, i32 ts);
  i32 lookup(i32 addr);
  map_entry* lookup2(i32 addr);
  i32 update_block(i32 addr, i32 zero);
  void update_lru(mm_cache* tlb, i32 hitway);
  void stats();
  void clearstats();
  mm_cache* get_tlb();
  i32 get_enabled();
};

#endif /* MEMMAP_H */
