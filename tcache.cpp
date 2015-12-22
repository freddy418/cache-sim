#include "tcache.h"
#include <cstring>

tcache::tcache(i32 ns, i32 as, i32 bs, i32 tg, i32 ts, i32 cw, i32 hd, i32 md, float re, float we){
  /* initialize cache parameters */
  i32 ofs = (6-log2(tg))-(6-log2(ts));
  sets = new tcache_set[ns];
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
    sets[i].repl = new plru(assoc);
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
  i32 skip = 0;

  // check if the line is compressible
  if (compress_writeback){
    i32 zero = 0;
    skip = 1;
    for (i32 i=0;i<bvals;i++){
      if (bp->value[i] != 0){
	zero = 1;
	skip = 0;
	break;
      }
    }
    if (map != 0 && map->get_enabled() == 1){
      map->update_block(addr, zero);
    }
  }

#ifdef DBG
  if ((addr & amask) <= DBG_ADDR && (addr & amask) + (bvals<<oshift) > DBG_ADDR){
    printf("Access (%u): %s Writing back (%lu) with line starting at (%x):", accs, name, DBG_ADDR, addr);
    for (i32 i=0;i<bvals;i++){
      printf("%llx,", bp->value[i]);
    }
    printf("\n");
    fflush(stdout);
  }
#endif

  // complete the writeback
  // L1 writeback
  if (skip == 0){
    if (next_level != 0){
      next_level->copy(addr, bp);
      bwused += bsize;
    }
    // L2 writeback
    else if (mem != 0){
      for (i32 i=0;i<bvals;i++){
	i32 write_addr = (addr & amask) + (i<<oshift);
	i64 write_value = bp->value[i];
#ifdef DBG
	if (write_addr == DBG_ADDR){
	  printf("Access (%u): %s Write back done (%llx) to memory at address (%x)\n", accs, name, write_value, write_addr);
	}
#endif
	mem->write(write_addr, write_value);
      }
      bwused += bsize;
      mem_energy += MEMWE; // memory write energy
    }
    
    // update stats that we wrote back a line
    writebacks++;
  }
#ifdef DBG
  else{
    if ((addr & amask) <= DBG_ADDR && (addr & amask) + (bvals<<oshift) > DBG_ADDR){
      printf("Access (%u): %s Write back (%lu) with line starting at (%x) SKIPPED :", accs, name, DBG_ADDR, addr);
      for (i32 i=0;i<bvals;i++){
	printf("%llx,", bp->value[i]);
      }
      printf("\n");
      fflush(stdout);
    }
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
  hitway = sets[index].repl->get_lru();
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

#ifdef DBG
  if (addr == DBG_ADDR){
    printf("Access (%u): %s Allocating line at index(%u), way(%u) for addr (%u)\n", accs, name, index, hitway, addr);
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

  sets[index].repl->update_lru(hitway);
  total_energy += write_energy * bvals;
  allocs++;
}

void tcache::touch(i32 addr){
  i32 tag, index, hit, hitway;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].repl->get_lru();
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
    sets[index].repl->update_lru(hitway);
  }

  // update tag energy
}

void tcache::copy(i32 addr, cache_block* op){
  i32 tag, index, hitway, wbaddr, hit;
  cache_block* bp;

  index = (addr >> bshift) & imask;
  tag = (addr >> (bshift + ishift));
  hitway = sets[index].repl->get_lru();
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

  sets[index].repl->update_lru(hitway);
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

  if (next_level != 0){
    for (i32 i=0;i<bvals;i++){
      crdata nld = next_level->readw((addr&amask)+(i<<oshift));
      bp->value[i] = nld.value;
      if (i == 0){
	delay = nld.delay;
      }
    }
    next_level->accs -= (bvals-1);
    next_level->hits -= (bvals-1);
    bwused += bsize;
  }
  else if (mem != 0){
    zero = 1;
    if ((map != 0) && (map->get_enabled())){
      zero = map->lookup(addr);
    }

    if (zero == 1){
      for (i=0;i<bvals;i++){
	i32 read_addr = (addr & amask) + (i<<oshift);
	bp->value[i] = mem->read(read_addr);
#ifdef DBG
	if (read_addr == DBG_ADDR){
	  printf("Access (%u): %s Refilled (%llx) from memory address (%x)\n", accs, name, bp->value[i], read_addr);
	}
#endif
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
	i32 read_addr = (addr & amask) + (i<<oshift);
	if (read_addr == DBG_ADDR){
	printf("Access (%u): %s Refilled (%llx) from ndm at address (%x)\n", accs, name, bp->value[i], read_addr);
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

  // this is a mux, no need to model energy
  return ret;
}

crdata tcache::readw(i32 addr){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 pos = (addr >> oshift) & bmask;
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
    hitway = sets[index].repl->get_lru();
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
  ret.value = block->value[pos];

#ifdef DBG
  if (addr == DBG_ADDR){
    printf("Access (%u): %s Reading (%llx) from addr (%x) in index(%u), way(%u), pos(%u)\n", accs, name, ret.value, addr, index, hitway, pos);
  }
#endif
  
  sets[index].repl->update_lru(hitway);
  total_energy += read_energy;
  accs++;
  return ret;
}

i32 tcache::write(i32 addr, i64 data){
  i32 index = (addr >> bshift) & imask;
  i32 tag = (addr >> (bshift + ishift));  
  i32 tagindex = (addr >> gshift) & tmask;
  i32 pos = (addr >> oshift) & bmask;
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
    hitway = sets[index].repl->get_lru();
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
    printf("Access (%u): %s Writing (%llx) to addr (%x) in index(%u), way(%u), pos(%u)\n", accs, name, data, addr, index, hitway, pos);
  }
#endif
  
  if (tsize != 64){
    // read out block->value
    oldval = block->value[pos];
    // zero out bits where data resides
    oldval &= ~(tgmask << (tagindex << tshift));
    // or in new data
    oldval |= data << (tagindex << tshift);
    /*
#ifdef TEST
    printf("%s Writing (%llx) to addr (%x) in index(%u) way(%u) tagindex(%u-%u) tmask(%llx) before(%llX) after (%llX)\n", name, data, addr, index, hitway, tagindex, tagindex<<tshift, tgmask << (tagindex << tshift), block->value[((addr>>oshift)&bmask)], oldval);
#endif
    */
    block->value[pos] = oldval;
  }else{
    block->value[pos] = data;
  }
  block->dirty = 1;
  sets[index].repl->update_lru(hitway);
  total_energy += write_energy;
  accs++;
  return delay;
}

void tcache::stats(){
  i32 size = (nsets) * (assoc) * (bsize);

  printf("%s: %d KB cache:\n", name, size >> 10);
  printf("miss rate: %1.8f\n", (((double)misses)/(accs)));
  printf("%u accesses, %llu hits, %llu misses, %llu writebacks, %llu allocs\n", accs, hits, misses, writebacks, allocs);
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
