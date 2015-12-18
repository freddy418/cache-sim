#include "tcache.h"
#include <cstring>

tcache::tcache(i32 ns, i32 as, i32 bs, i32 tg, i32 ts, i32 cw, i32 hd, i32 md, float re, float we){
  /* initialize cache parameters */
  i32 ofs = (6-log2(tg))-(6-log2(ts));
  sets = new cache_set[ns];
  mem = 0;
  map = 0;
  next_level = 0;
  nsets = ns;
  bsize = bs;
  bvals = bs >> 3;
  assoc = as;
  imask = ns-1;
  oshift = 3 - ofs;
  bmask = (bs >> 3) - 1;
  read_energy = re;
  write_energy = we;
  compress_writeback = cw;

  tmask = (1ULL << (i32)(log2(64/ts)))-1;
  tshift = log2(ts);
  tsize = ts;
  tgmask = (1ULL << tsize)-1;
  gshift = log2(tg >> 3); // tag granularity determines how many bits to downshift from byte address to determine location of tag within doubleword read from memory

  //printf("tshift=%u, tgmask=%llx, tmask=%llx, oshift=%u, ofs=%d, gshift=%u\n", tshift, tgmask, tmask, oshift, ofs, gshift);
  
  ishift = log2(ns);
  bshift = log2(bs) - ofs;
  amask = -1 << bshift;
  hitdelay = hd;
  missdelay = md;

  /* initialize stats counters */
  accs = 0;
  hits = 0;
  misses = 0;
  writebacks = 0;
  allocs = 0;
  total_energy = 0;
 
#ifdef LINETRACK
  mcount = (i32*)calloc(nsets, sizeof(i32));
  acount = (i32*)calloc(nsets, sizeof(i32));
#endif

  for (i32 i=0;i<nsets;i++){
    /* distribute data blocks to cache sets */
    sets[i].blks = new cache_block[assoc];
    for (i32 j=0;j<assoc;j++){
      sets[i].blks[j].valid = 0;
      sets[i].blks[j].dirty = 0;
      sets[i].blks[j].tag = 0;
      sets[i].blks[j].value = 0;
    }
    /* initialize LRU info */ 
    item* node = new item();
    node->val = 0;
    sets[i].lru = node;
    for (i32 j=1;j<assoc;j++){
      node->next = new item();
      node->next->val = j;
      node = node->next;
    }
  }
}

void tcache::clearstats(){
   accs = 0;
   hits = 0;
   misses = 0;
   bwused = 0;
   writebacks = 0;
   allocs = 0;
   total_energy = 0;

#ifdef LINETRACK 
   for(int i=0;i<nsets;i++){
     mcount[i] = acount[i] = 0;
   }
#endif
}

void tcache::writeback(cache_block* bp, i32 addr){
  i32 zero = 1;

  // check if the line is compressible
  if ((compress_writeback == 1) && (map != 0 && map->get_enabled() == 1)){
    zero = 0;
    for (i32 i=0;i<bvals;i++){
      if (bp->value[i] != 0){
	zero = 1;
	break;
      }
    }
  }

  // if it is compressible, just update the ndm bit
  if (zero == 0){
    map->update_block(addr, zero);
  }
  // complete the writeback
  else{
#ifdef DBG
    if ((addr & amask) <= DBG_ADDR && (addr & amask) + (bvals<<oshift) > DBG_ADDR){
      printf("%s (%llu) Writing back (%x) zero(%u):", name, totalaccs, addr, zero);
      fflush(stdout);
    }
#endif
    // L1 writeback
    if (next_level != 0){
      next_level->copy(addr, bp);
      bwused += bsize;
    }
    // L2 writeback
    else if (mem != 0){
      for (i32 i=0;i<bvals;i++){
#ifdef DBG
	if ((addr & amask) <= DBG_ADDR && (addr & amask) + (bvals<<oshift) > DBG_ADDR){
	  printf("%llx,", bp->value[i]);
	}
#endif
	mem->write((addr & amask) + (i<<oshift), bp->value[i]);      
      }
      bwused += bsize;
      mem_energy += MEMWE; // memory write energy
    }

    // update stats that we wrote back a line
    writebacks++;
  }

#ifdef DBG
  if ((addr & amask) <= DBG_ADDR && (addr & amask) + (bvals<<oshift) > DBG_ADDR){
  printf("\n");
}
#endif

  // line is no longer dirty
  bp->dirty = 0;
}

void tcache::allocate(i32 addr){
  i32 tag, index, hit, hitway, wbaddr;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;
  hit = 0;
  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }
  bp = &(sets[index].blks[hitway]);

  //hits+=hit;
  //accs++;
  //misses+=(1-hit);

#ifdef TEST
  if (addr == 0){
    printf("%s allocate, set(%X), addr(%X)\n", name, index, addr);
  }
#endif

  // if block is valid and dirty, write it back
  if (hit == 0){
#ifdef LINETRACK
    if (((index%512) == 0) && (strcmp(name, "L2") == 0)){
      printf("ALLOC: address(%08X), tag(%X), w0(%X), w1(%X)\n", addr, tag, sets[index].blks[0].tag, sets[index].blks[1].tag);
    }
#endif
    total_energy += read_energy * bvals; // extra writeback read energy
    if (bp->valid == 1 && bp->dirty == 1){
      wbaddr = ((bp->tag) << (ishift+bshift)) + (index<<(bshift));
      this->writeback(bp, wbaddr);
    }

    if (bp->value == 0){
      bp->value = new i64[bvals];
      if (bp->value <= 0){
	printf("FATAL: calloc ran out of memory!\n");
	exit(1);
      }
    }

    bp->tag = tag;
    bp->valid = 1;
    bp->dirty = 0;
    for (i32 i=0;i<bvals;i++){
      bp->value[i] = 0;
    }
  } // otherwise just update LRU info

  update_lru(&(sets[index]), hitway);
  total_energy += write_energy * bvals;
  allocs++;
}

void tcache::touch(i32 addr){
  i32 tag, index, hit, hitway;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;
  hit = 0;

  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

  if (hit == 1){
    update_lru(&(sets[index]), hitway);
  }

  // update tag energy
}

void tcache::copy(i32 addr, cache_block* op){
  i32 tag, index, hitway, wbaddr, hit;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].lru->val;
  hit = 0;
  for(i32 i=0;i<assoc;i++){
    bp = &(sets[index].blks[i]);
    if ((bp->tag == tag) && (bp->valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }
  bp = &(sets[index].blks[hitway]);

  // if block is valid and dirty, write it back
  if (hit == 0){
    total_energy += read_energy * bvals; // extra writeback read energy
    if ((bp->valid == 1) && (bp->dirty == 1)){
      wbaddr = ((bp->tag)<<(ishift+bshift)) + (index<<bshift);
      // no need to refill, whole line is written from L1
      this->writeback(bp, wbaddr);
    }
  }

  if (bp->value == 0){
    bp->value = (i64*) calloc(bvals, sizeof(i64));
    if (bp->value <= 0){
      printf("FATAL: calloc ran out of memory!\n");
      exit(1);
    }
  }

  bp->tag = tag;
  bp->valid = op->valid;
  bp->dirty = op->dirty;

  for (i32 i=0;i<bvals;i++){
    bp->value[i] = op->value[i];
  }

  update_lru(&(sets[index]), hitway);
  total_energy += write_energy * bvals;
}

i32 tcache::refill(cache_block* bp, i32 addr){
  i32 i, index, tag, zero, delay;
  tag = (addr >> (bshift + ishift)); 
  index = (addr >> bshift) & imask;
  zero = 1;
  
  bp->tag = tag;
  bp->valid = 1;
  bp->dirty = 0;
  if (bp->value == 0){
    bp->value = (i64*) calloc(bvals, sizeof(i64));
    if (bp->value == 0){
      printf("FATAL: calloc ran out of memory!\n");
      exit(1);
    }
  }

  if ((map != 0) && (map->get_enabled())){
    zero = map->lookup(addr);
  }

  if (next_level != 0){
    for (i32 i=0;i<bvals;i++){
      crdata nld = next_level->readw((addr&amask)+(i<<oshift));
      bp->value[i] = nld.value;
      if (i == 0){
	delay = nld.delay;
      }
      /*if (strcmp(name, "L1") == 0  && addr == 0){
	printf("%X-%llX,", (addr&amask)+(i<<oshift), bp->value[i]);
	}*/
    }
    /*if (strcmp(name, "L1") == 0  && addr == 0){
      printf("\n");
      }*/
    next_level->accs -= (bvals-1);
    next_level->hits -= (bvals-1);
    bwused += bsize;
  }
  else if (mem != 0){
    //printf("sets(%u), bsize(%u) - Refill from memory - addr(%08X), index(%u), tag(%X)\n", nsets, bsize, addr, index, tag);
    //exit(1);
    if (zero == 1){
      for (i=0;i<bvals;i++){
	bp->value[i] = mem->read((addr & amask) + (i<<oshift));
#ifdef DBG
if ((addr & amask) + (i<<oshift) == DBG_ADDR){
  printf("Reading %llx from memory at addr (%x)\n", bp->value[i], (addr & amask) + (i<<oshift));
}
#endif
	//printf("REFILL (%X): Reading mem addr (%X), data(%llX)\n", addr, ((addr&amask)+(i<<oshift)), bp->value[i]);
      }
      bwused += bsize;
      mem_energy += MEMRE;
      delay = mem->load();
      if ((map != 0) && (map->get_enabled())){
	delay++; // extra cycle to read NDM bit
      }
    }else{
      for (i=0;i<bvals;i++){
        bp->value[i] = 0;
#ifdef DBG
if ((addr & amask) + (i<<oshift) == DBG_ADDR){
  printf("Reading %llx from memory at addr (%x)\n", 0ULL, (addr & amask) + (i<<oshift));
}
#endif
      }
      delay = hitdelay;
    }
  }
  //printf("block size: %d, index: %d, addr: %X, bmask: %X\n", (bsize), (addr>>bshift)&(bmask), addr, bmask);

  total_energy += write_energy * bvals;
  return delay;
}

crdata tcache::read(i32 addr){ 
  i32 tagindex = (addr >> gshift) & tmask;
  crdata ret;
  
  ret = readw(addr);
  if (tsize != 64){
    ret.value = (ret.value >> (tagindex << tshift)) & tgmask;
  }

  /*
#ifdef TEST
  printf("%s Reading (%llx) from addr (%x)\n", name, ret, addr);
#endif
  */

  // this is a mux, no need to model energy
  return ret;
}

crdata tcache::readw(i32 addr){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 hit = 0;
  i32 hitway = 0;
  i32 zero = 0;
  i32 wbaddr;
  i32 delay;
  crdata ret;
  cache_block* block;

  // check tags
  for(i32 i=0;i<assoc;i++){
    if ((sets[index].blks[i].tag == tag) && (sets[index].blks[i].valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

#ifdef LINETRACK
  acount[index]++;
#endif

  // update bookkeeping
  if (hit == 1){
    hits++;
    block = &(sets[index].blks[hitway]);
    ret.delay = hitdelay;
  }else{
    misses++;    
    hitway = sets[index].lru->val;
    block = &(sets[index].blks[hitway]);
    total_energy += read_energy * bvals; // extra writeback read energy
    //printf("miss to index: %d on tag: %x, replaced %d\n", index, tag, hitway);
    if (block->valid == 1 && block->dirty == 1){
      // lock line in next level
      /*if (next_level != 0){
	next_level->touch(addr);
	}*/
      wbaddr = ((block->tag)<<(ishift+bshift)) + (index<<bshift);
      this->writeback(block, wbaddr);
    }
    ret.delay = this->refill(block, addr);
  }
  
  this->update_lru(&(sets[index]), hitway);
  accs++;

  /*#ifdef DBG
  if (addr == DBG_ADDR){
    printf("%s %s Reading (%llx) from addr (%x) in index(%u) and way(%u)\n", name, hit==1?"hit":"miss",block->value[((addr>>(oshift))&bmask)], addr, index, hitway);
    }
    #endif*/
  
  total_energy += read_energy;
  ret.value = block->value[((addr>>oshift)&bmask)];
  return ret;
}

i32 tcache::write(i32 addr, i64 data){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 tagindex = (addr >> gshift) & tmask;
  i32 hit = 0;
  i32 hitway = 0;
  i32 zero = 0;
  i32 wbaddr;
  i64 oldval;
  i32 delay;
  cache_block* block;

  //printf("cache write: address(%08X), data(%llX)\n", addr, data);

  // check tags
  for(i32 i=0;i<assoc;i++){
    block = &(sets[index].blks[i]);
    if ((sets[index].blks[i].tag == tag) && (sets[index].blks[i].valid == 1)){
      hit = 1;
      hitway = i;
      break;
    }
  }

#ifdef LINETRACK
  acount[index]++;
#endif

  // update bookkeeping
  if (hit == 1){
    hits++;
    block = &(sets[index].blks[hitway]);
    delay = hitdelay;
  }else{
    misses++;    
    hitway = sets[index].lru->val;
    block = &(sets[index].blks[hitway]);
    total_energy += read_energy * bvals; // extra writeback read energy
    //printf("miss to index: %d on tag: %x, replaced %d\n", index, tag, hitway);
    if (block->valid == 1 && block->dirty == 1){
      // lock line in next level
      /*if (next_level != 0){
	next_level->touch(addr);
	}*/
      wbaddr =  ((block->tag)<<(ishift+bshift)) + (index<<bshift);
      this->writeback(block, wbaddr);
    }
    delay = this->refill(block, addr);
  }

#ifdef DBG
  if (addr == DBG_ADDR){
    printf("%s Writing (%llx) to addr (%x) in index(%u) and way(%u)\n", name, data, addr, index, hitway);
  }
#endif

  
  if (tsize != 64){
    // read out block->value
    oldval = block->value[((addr>>oshift)&bmask)];
    // zero out bits where data resides
    oldval &= ~(tgmask << (tagindex << tshift));
    // or in new data  
    oldval |= data << (tagindex << tshift);
    /*
#ifdef TEST
    printf("%s Writing (%llx) to addr (%x) in index(%u) way(%u) tagindex(%u-%u) tmask(%llx) before(%llX) after (%llX)\n", name, data, addr, index, hitway, tagindex, tagindex<<tshift, tgmask << (tagindex << tshift), block->value[((addr>>oshift)&bmask)], oldval);
#endif
    */
    block->value[((addr>>oshift)&bmask)] = oldval;
  }else{
    block->value[((addr>>oshift)&bmask)] = data;
  }
  block->dirty = 1;
  this->update_lru(&(sets[index]), hitway);
  accs++;
  total_energy += write_energy;
  return delay;
}

void tcache::stats(){
  i32 size = (nsets) * (assoc) * (bsize);

  printf("%s: %d KB cache:\n", name, size >> 10);
  printf("miss rate: %1.8f\n", (((double)misses)/(accs)));
  printf("%llu accesses, %llu hits, %llu misses, %llu writebacks, %llu allocs\n", accs, hits, misses, writebacks, allocs);
  printf("total energy used: %f Joules\n", total_energy);
  if (mem != 0){
    printf("bandwidth used: %llu KB\n", (bwused >> 10));
  }
 
#ifdef LINETRACK 
  for (i32 i=0;i<nsets;i++){
    printf("Set %u, Accesses %u, Misses %u\n", i, acount[i], mcount[i]);
  }
#endif
}


void tcache::update_lru(cache_set * set, unsigned int hitway){
  item * hitnode;
  item * node = set->lru;
  item * temp;

  /*temp = set->lru;
  printf("before: way %d referenced, (%d,", hitway, temp->val);
  while (temp->next != 0){
    temp = temp->next;
    printf("%d,", temp->val);
  }
  printf(")\n");*/

  if (node->next == 0){
    // direct-mapped cache
    return;
  }
  
  if (node->val == hitway){
    hitnode = node;
    set->lru = node->next;
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

  /*temp = set->lru;
  printf("after: way %d referenced, (%d,", hitway, temp->val);
  while (temp->next != 0){
    temp = temp->next;
    printf("%d,", temp->val);
  }
  printf(")\n");*/
}

void tcache::set_mem(tmemory* sp){
  mem = sp;
}

void tcache::set_map(mem_map* mp){
  map = mp;
}

void tcache::set_nl(tcache* cp){
  next_level = cp;
}

void tcache::set_name(char *cp){
  name = cp;
}

void tcache::set_anum(i32 n){
  anum = n;
}

i64 tcache::get_accs(){
  return accs;
}

i64 tcache::get_hits(){
  return hits;
}

void tcache::set_accs(i64 num){
  accs = num;
}

void tcache::set_hits(i64 num){
  hits = num;
}
