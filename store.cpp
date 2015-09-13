#include "store.h"

// create the pointers
tmemory::tmemory(i32 tg, i32 ts){
  //printf("Entering create_memory\n");
  i32 os = (6-log2(tg)) - (6-log2(ts));
  i32 pgs = 1<<(20+os);
  pages = new tpage*[pgs];
  pmask = pgs - 1;
  pshift = 12-os;
  fmask = (1<<pshift) - 1;
  ishift = 3 - os; // 3 bits for 64b values
  //printf("pages: %d, page mask: %08X, frame mask: %08X\n", pages, pmask, fmask);

  //printf("Leaving create_memory\n");
}

i64 tmemory::read(i32 addr){
  //printf("Entering mem_read\n");
  i32 fnum = (addr >> pshift) & pmask;
  i32 findex = (addr & fmask) >> ishift;

  if (pages[fnum] == 0){
    //printf("Memory read0 addr (%X), data(%llX)\n", addr, 0UL);
    //fflush(stdout);
    return 0UL;
  }else{
    //printf("Memory read1 addr (%X), data(%llX)\n", addr, (pages[fnum]->data[findex]));
    //fflush(stdout);
    return pages[fnum]->data[findex];
  }
}

void tmemory::write(i32 addr, i64 data){ 
  //printf("Entering mem_write\n");
  i32 fnum = (addr >> pshift) & pmask;
  i32 findex = (addr & fmask) >> ishift;

  //printf("Memory write addr (%X), data(%llX)\n", addr, data);
  //fflush(stdout);

  if (pages[fnum] == 0){
    pages[fnum] = new tpage();
  }
  pages[fnum]->data[findex] = data;
  //printf("Leaving mem_write\n");
}
