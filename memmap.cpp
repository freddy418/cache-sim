#include "memmap.h"

mem_map::mem_map(i32 en, i32 ps, i32 bs, i32 cs, i32 tg, i32 ts){
  // enable - 0-off, 1-on
  // ps - page size in bytes
  // bs - cache block size in bytes
  // cs - cache size in entries
  // ofs - data offset w.r.t to tags (e.g. ofs=1 is tag that is 2x the size of data)
  // create the memory map;
  
  enabled = en;
  os = (6-log2(tg))-(6-log2(ts));
  pshift = log2(ps) - os;
  bshift = log2(bs) - os;
  bsize = bs;
  psize = ps;
  bmask = (ps/bs) - 1;
  bwused = 0;
  nents = (1 << (22+os)) / (ps >> 10);
  //assert(pow2(nents));
  mapents = new map_entry[nents];
  for (i32 i=0;i<nents;i++){
    mapents[i].zero = 0ULL; // entry is zero
  }
  bvsize = (ps/bs) >> 3;

  printf("MEMMAP: pshift=%u, bshift=%u, bmask=%x, bvsize=%u\n", pshift, bshift, bmask, bvsize);

  // create the l1 map tlb
  tlb = new mm_cache();
  tlb->nents = cs;
  tlb->accs = 0;
  tlb->hits = 0;
  tlb->misses = 0;
  tlb->zeros = 0;
  tlb->entries = new tlb_entry[cs];
  for (i32 i=0;i<cs;i++){
    tlb->entries[i].entry = 0;
    tlb->entries[i].valid = 0;
    //tlb->entries[i]->dirty = 0;
    tlb->entries[i].tag = 0;
  }

  // initialize LRU info for l1 tlb
  item* node = new item();
  node->val = 0;
  tlb->lru = node;
  for (i32 i=1;i<tlb->nents;i++){
    node->next = new item();
    node->next->val = i;
    node = node->next;
  }

  // create the l2 map tlb
  tlb2 = new mm_cache();
  tlb2->nents = cs << 2;
  tlb2->accs = 0;
  tlb2->hits = 0;
  tlb2->misses = 0;
  tlb2->zeros = 0;
  tlb2->entries = new tlb_entry[cs << 2];
  for (i32 i=0;i<tlb2->nents;i++){
    tlb2->entries[i].entry = 0;
    tlb2->entries[i].valid = 0;
    //tlb2->entries[i]->dirty = 0;
    tlb2->entries[i].tag = 0;
  }

  // initialize LRU info for l1 tlb
  node = new item();
  node->val = 0;
  tlb2->lru = node;
  for (i32 i=1;i<tlb2->nents;i++){
    node->next = new item();
    node->next->val = i;
    node = node->next;
  }

  //printf("Initialized mem_map with %u TLB entries\n", tlb->nents);
}

i32 mem_map::lookup(i32 addr){
  i32 hit, hitway, block, tag, zero;
  tag = addr >> (pshift);
  block = (addr >> bshift) & bmask;
  hit = hitway = 0;

  // check tags of tlb entries
  for(i32 i=0;i<tlb->nents;i++){
    if ((tlb->entries[i].tag == tag) && (tlb->entries[i].valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

  // update bookkeeping
  if (hit == 1){
    tlb->hits++;
  }else{
    tlb->misses++;
    hitway = tlb->lru->val;
    tlb->entries[hitway].entry = lookup2(addr); //&(mapents[tag]);
    tlb->entries[hitway].tag = tag;
    tlb->entries[hitway].valid = 1;
  }
  update_lru(tlb, hitway);
  tlb->accs++;

  //printf("Map lookup for addr: %X, hitway: %u, block: %u, bv: %X, result: %u\n", addr,  hitway, block, tlb->entries[hitway]->zero, ((tlb->entries[hitway]->zero >> block) & 1));
  //printf("bshift: %u, bmask: %x\n", bshift, bmask);

  if (enabled == 0){
    // just model the TLB lookup
    return 1;
  }

  zero = ((tlb->entries[hitway].entry->zero >> block) & 1);
  if (zero == 0){
    tlb->zeros++;
  }

#ifdef DBG
  if (addr == DBG_ADDR){
    printf("Lookup proceeding for address(%x) tag(%d) in map (%llx) returning %d\n", addr, tag, tlb->entries[hitway].entry->zero, zero);
  }
#endif

  return zero;
}

map_entry* mem_map::lookup2(i32 addr){
  i32 hit, hitway, tag;
  tag = addr >> (pshift);
  hit = hitway = 0;

  // check tags of tlb entries
  for(i32 i=0;i<tlb2->nents;i++){
    if ((tlb2->entries[i].tag == tag) && (tlb2->entries[i].valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

  // update bookkeeping
  if (hit == 1){
    tlb2->hits++;
  }else{
    tlb2->misses++;
    hitway = tlb2->lru->val;
    tlb2->entries[hitway].entry = &(mapents[tag]);
    tlb2->entries[hitway].tag = tag;
    tlb2->entries[hitway].valid = 1;
    bwused += 8;
    if (enabled == 1){
      bwused += bvsize;
    }
  }
  update_lru(tlb2, hitway);
  tlb2->accs++;

#ifdef DBG
  if (addr == DBG_ADDR){
    printf("Lookup2 proceeding for address(%x) tag(%d) in map (%llx)\n", addr, tag, tlb2->entries[hitway].entry->zero);
  }
#endif

  //printf("Map lookup for addr: %X, hitway: %u, block: %u, bv: %X, result: %u\n", addr,  hitway, block, tlb->entries[hitway]->zero, ((tlb->entries[hitway]->zero >> block) & 1));
  //printf("bshift: %u, bmask: %x\n", bshift, bmask);

  return (tlb2->entries[hitway].entry);
}

// 1-3-15 - changed the semantics of this function from void to return an integer
// 0 - indicates successful update of block, 1 - indicates unsuccessful
i32 mem_map::update_block(i32 addr, i32 zero){
  i32 block, hit, hitway, tag;
  i64 before;

  if (enabled == 0){
    // unsuccessful return
    return 0;
  }

  tag = addr >> (pshift);
  hit = hitway = 0;
  block = (addr >> bshift) & bmask;

  // check tags of tlb entries
  for(i32 i=0;i<tlb->nents;i++){
    if ((tlb->entries[i].tag == tag) && (tlb->entries[i].valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

  // update bookkeeping
  if (hit == 1){
    tlb->hits++;
  }else{
    tlb->misses++;
    hitway = tlb->lru->val;
    if (tlb->entries[hitway].dirty == 1){
      bwused += 8;
      if (enabled == 1){
	//TODO: this is wrong, tlb2 needs to be updated and tlb2 needs to write memory
	bwused += bvsize; // write through bandwidth usage
      }
      tlb->entries[hitway].dirty = 0;
    }
    tlb->entries[hitway].entry = lookup2(addr); //&(mapents[tag]);
    tlb->entries[hitway].tag = tag;
    tlb->entries[hitway].valid = 1;
  }
  update_lru(tlb, hitway);
  before = mapents[tag].zero;

  if (zero == 1){ // update memory and tlb2 to avoid writeback
    mapents[tag].zero |= (1LL << block);
  }else{
    mapents[tag].zero &= (~(1LL << block));
  }

#ifdef DBG
  if (addr>>bshift == DBG_ADDR>>bshift){
    printf("Update_block(%u) proceeding for address(%x) tag(%d) in map before(%llx) after(%llx)\n", zero, addr, tag, before, mapents[tag].zero);
    }
#endif

  tlb->entries[hitway].dirty = 1;
  tlb->accs++;

  // successful return
  return 1;
}

void mem_map::update_lru(mm_cache * tlb, i32 hitway){
  item * hitnode;
  item * node = tlb->lru;
  item * temp;

  /*temp = tlb->lru;
  printf("before: way %d referenced, (%d,", hitway, teval);
  while (tenext != 0){
    temp = tenext;
    printf("%d,", teval);
  }
  printf(")\n");*/

  if (node->next == 0){
    // direct-mapped cache
    return;
  }
  
  if (node->val == hitway){
    hitnode = node;
    tlb->lru = node->next;
    while (node->next != 0){
      node = node->next;
    }
  }else{
    while (node->next != 0){
      if (node->next->val == hitway){
	hitnode = node->next;
	node->next = hitnode->next;
      }else{
	node = node->next;
      }
    }
  }
  node->next = hitnode;
  hitnode->next = 0;

  /*temp = tlb->lru;
  printf("after: way %d referenced, (%d,", hitway, teval);
  while (tenext != 0){
    temp = tenext;
    printf("%d,", teval);
  }
  printf(")\n");*/
}

void mem_map::stats(){
  printf("%d entry L1 TLB stats\n", tlb->nents);
  printf("%d accesses, %d hits, %d misses, %d avoided accesses\n", tlb->accs, tlb->hits, tlb->misses, tlb->zeros);
  printf("miss rate: %1.8f\n", (((double)tlb->misses)/(tlb->accs)));
  printf("%d entry L2 TLB stats\n", tlb2->nents);
  printf("%d accesses, %d hits, %d misses\n", tlb2->accs, tlb2->hits, tlb2->misses);
  printf("miss rate: %1.8f\n", (((double)tlb2->misses)/(tlb2->accs)));
  printf("bandwidth used: %llu KB\n", (bwused >> 10));
}

mm_cache* mem_map::get_tlb(){
  return tlb;
}

void mem_map::clearstats(){
  tlb->accs = 0;
  tlb->hits = 0;
  tlb->misses = 0;
}

i32 mem_map::get_enabled(){
  return enabled;
}
