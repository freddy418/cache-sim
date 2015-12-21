#include "store.h"

// create the pointers
tmemory::tmemory(i32 tg, i32 ts, i32 hd){
  //printf("Entering create_memory\n");
  i32 os = (6-log2(tg)) - (6-log2(ts));
  i32 pgs = 1<<(32-PSIZE+os);
  hitdelay = hd;
  pages = new tpage*[pgs];
  pmask = pgs - 1;
  pshift = PSIZE-os;
  pvals = (1<<PSIZE)/(ts>>3);
  fmask = pvals - 1;
  ishift = (log2(ts) - 3) - os;
  
  // empty out first level pointers
  for (int i=0;i<pgs;i++){
    pages[i] = 0;
  }
  // sanity checking print
  printf("STORE: %u frames, %u values per frame\n", pgs, pvals);
}

i64 tmemory::load(){
  i64 delay = hitdelay;
  i64 busy = nextfree-curr_ck;
  if (curr_ck < nextfree){
    delay += busy;
  }
  nextfree = curr_ck + delay;
  //printf("memory now return %llu cycle delay: nextfree=%llu, curr_ck=%llu\n", delay, nextfree, curr_ck);
  return delay;
}

i64 tmemory::read(i32 addr){
  //printf("Entering mem_read\n");
  i32 fnum = (addr >> pshift) & pmask;
  i32 findex = (addr >> ishift) & fmask;
  i64 data = 0;
  i32 null = 1;

  if (findex >= pvals){
    fprintf(stderr, "Memory page index (%u) out of range (%u, %u)\n", findex, 0, pvals);
    assert(0);
  }

  if (pages[fnum] != 0){
    data = pages[fnum]->data[findex];
    null = 0;
  }

#ifdef DBG
  if (addr == DBG_ADDR){
    if (null == 0){
      printf("MEMORY: (%llx) read from address (%x) in page frame (%u) index (%u)\n", data, addr, fnum, findex);
    }else{
      printf("MEMORY: (%llx) read from address (%x) because no page allocated at frame (%u)\n", data, addr, fnum);
    }
  }
#endif

  return data;
}

void tmemory::write(i32 addr, i64 data){ 
  i32 fnum = (addr >> pshift) & pmask;
  i32 findex = (addr >> ishift) & fmask;

  if (findex >= pvals){
  fprintf(stderr, "Memory page index (%u) out of range (%u, %u)\n", findex, 0, pvals);
    assert(0);
  }

  if (pages[fnum] == 0){
  // make a blank page to hold new data
    pages[fnum] = new tpage();
    pages[fnum]->data = new i64[pvals];
    for (int i=0;i<pvals;i++){
      pages[fnum]->data[i] = 0;
    }
  }
  pages[fnum]->data[findex] = data;

#ifdef DBG
  if (addr == DBG_ADDR || (fnum == 1005631 && findex == 295)){
  printf("MEMORY: (%llx) written to address (%x) in page frame (%u) index (%u)\n", data, addr, fnum, findex);
  //printf("MEMORY: After write (%llx) in page frame (%u) index (%u)\n", pages[fnum]->data[findex], fnum, findex);
  }
#endif
}
