#include "coredrv.h"

coredrv::coredrv(core_args* args){
  // read input arguments
  i32 l1delay = ((core_args*)args)->l1delay;
  i32 l2delay = ((core_args*)args)->l2delay;
  taggran = ((core_args*)args)->taggran;
  tagsize = ((core_args*)args)->tagsize;
  rdalloc = ((core_args*)args)->rdalloc;
  mapon = ((core_args*)args)->mapon;
  pagesize = ((core_args*)args)->pagesize;
  l1assoc = ((core_args*)args)->l1assoc;
  l1sets = ((core_args*)args)->l1sets;
  l2assoc = ((core_args*)args)->l2assoc;
  l2sets = ((core_args*)args)->l2sets;
  bsize = ((core_args*)args)->bsize;
  skip = ((core_args*)args)->skip;
  //dir = ((core_args*)args)->dir;
  //bench = ((core_args*)args)->bench;
  qp = ((core_args*)args)->qp;
  sp = ((core_args*)args)->sp;
  name = ((core_args*)args)->name;
  fr = ((core_args*)args)->fr;
  l1_read_energy = ((core_args*)args)->l1_read_energy;
  l1_write_energy = ((core_args*)args)->l1_write_energy;
  l2_read_energy = ((core_args*)args)->l2_read_energy;
  l2_write_energy = ((core_args*)args)->l2_write_energy;
  pmask = ~((1 << (int)(log2(pagesize) - (6-log2(taggran))-(6-log2(tagsize)))) - 1);
  done = 0;
  
  accesses = 0;
  // cache delay counters
  totaldelay = 0;
  nbupdates = 0;
  // misc stall counters
  qstallcyc = 0;
  totaligap = 0;
  mismatches = 0;
  mismaps = 0;
  // track instruction count at last access
  lastic = 0;

  // "I'm alive print out"
  printf("MDCacheSim (last updated 3-14-15):\n");
  printf("Tag Size and Granularity: %u-bit tag per %s\n", tagsize, (taggran==8)?"byte":(taggran==32)?"word":"doubleword");
  printf("Optimizations: Locality %s, Read Allocate %s\n", (mapon==1)?"on":"off", (rdalloc==1)?"on":"off");
  printf("Page Size=%u and Page Mask=%x\n", pagesize, pmask); 

  // initialize cache and local variables;
  dl1 = new mtcache(l1sets, l1assoc, bsize, taggran, tagsize, (rdalloc==0)?1:0, l1delay, l2delay, l1_read_energy, l1_write_energy);
  dl2 = new mtcache(l2sets, l2assoc, bsize, taggran, tagsize, rdalloc, l2delay, MEMDELAY, l2_read_energy, l2_write_energy);
  mp = new mem_map(mapon, pagesize, bsize, 16, taggran, tagsize); // added enable (0-off,1-on)

  dl1->set_nl(dl2);
  dl2->set_mem(sp);
  dl2->set_map(mp);
  dl1->set_name("CL1");
  dl2->set_name("CL2");

  curr_ic = 0;
  warm_ic = 0;
  warm_ck = 0;
  warm_accs = 0;
  memset(&curr_req, 0, sizeof(sim_req));
  temp_req = 0;
}

i32 coredrv::clock(){
  if ((curr_req.valid == 1) && (curr_req.inprogress == 1) && (curr_req.fill_cycle < curr_ck)){
    curr_req.valid = 0;
  }
  // model 1 cycle queue delay
  if (fr != 0 && temp_req != 0 && temp_req->valid == 1){	
    qp->push(temp_req);
    temp_req = 0; // monitor will handle dealloc
  }

  // if request not valid or previous request was fulfilled, read the next request
  if (curr_req.valid == 0){
    cache_req* req = NULL;
    if (fr != 0){ // core reads from trace
      req = fr->fread(); //read next line from trace
      if (req == NULL){ // termination condition for core
	clocks = curr_ck; // no more entries in trace
	return 1;
      }else{
	curr_req.icount = req->icount;
	curr_req.addr = req->addr;
	curr_req.isRead = req->isRead;
	curr_req.value = req->value;
	curr_req.valid = 1;
	curr_req.inprogress = 0;
	free(req);
	// count the gap (for understanding cycle count)
	if ((req->icount - lastic) > 1){
	  totaligap += ((req->icount)-lastic)-1;
	}
	lastic = req->icount;
	// end count the gap
      }
    }
    else if (qp != 0){ // monitor reads from instruction queue
      if (!qp->empty()){
	req = qp->front();
        if (req == NULL){
	  printf("Instruction read from queue was NULL\n");
	  assert(0);
	}
	curr_req.icount = req->icount;
	curr_req.addr = req->addr;
	curr_req.isRead = req->isRead;
	curr_req.value = req->value;
	curr_req.valid = req->ismem;
	curr_req.inprogress = 0;
	qp->pop();
	free(req);
      }else{
	if (done == 1){ // termination condition for monitor
	  return 1; // queue is empty and core is done queueing
	}
      }
    }else{
      printf("Error: Simulation driver sees no trace and no instruction queue\n");
      assert(0);
    }

    // clear stats collected during warmup
    if ((fr == 0 && curr_req.valid == 1) || (fr != 0)){
      accesses++;
    }
    if (accesses == skip){
      mem_energy = 0;
      dl1->clearstats();
      dl2->clearstats();
      if (mp != 0){
	mp->clearstats();
      }
      if (fr != 0){
	printf("%s warmup completed at %u accesses and %llu instructions\n", name, accesses, curr_ic);
	warm_ic = curr_ic;
	warm_ck = curr_ck;
	warm_accs = accesses;
	qstallcyc = 0;
	totaligap = 0;
	totaldelay = 0;
	nbupdates = 0;
      }else{
	printf("%s warmup completed at %u accesses\n", name, accesses);
      }
    }
  }

  if (curr_req.valid == 1){
    if (fr != 0){
      if (qp->size() < QSIZE){ // prepare new queue packet
	temp_req = new cache_req();
	temp_req->ismem = 0;
	temp_req->valid = 1;
      }else{
	qstallcyc++; // count when queue is full
	return 0;
      }
      if ((curr_req.icount > curr_ic) && !(curr_req.icount == curr_ic+1 && curr_req.inprogress == 0)){
	curr_ic++;
	return 0;
      }
    }
      
    if (curr_req.inprogress == 0){
      i64 sval;
      i32 zero;
      i32 delay = 1;
      
#ifdef CDBG
      if (curr_req.addr == DBG_ADDR && fr == 0){
	printf("Access (%u): (%s), addr (%x), value (%llx)\n", accesses, curr_req.isRead?"Read":"Write", curr_req.addr, curr_req.value);
	fflush(stdout);
      }
#endif

      // reference the cache hierarchy
      if (mp != 0){
	zero = mp->lookup(curr_req.addr & pmask);
      }
      dl1->set_anum(accesses);
      dl2->set_anum(accesses);
      
      if (curr_req.isRead == 0){
	sval = 0;
	// check the map first
	if ((rdalloc == 0 && zero == 1) || rdalloc == 1){
	    crdata rdata = dl1->read(curr_req.addr);
	    sval = rdata.value;
	    delay = rdata.delay;
	    if (rdalloc == 0 && zero == 1){
	      delay++; // additional delay when NDM-E bit misses
	    }
	}
	if (sval != curr_req.value && fr == 0){
          //printf("Access(%u): Store and trace unmatched for addr (%X): s(%llX), t(%llX)\n", accesses, curr_req.addr, sval, curr_req.value);
	  //assert(0);
	  // fix up to prevent later mismatches
	  if ((sval == 0) && (rdalloc == 0)){
	    if (mp != 0 && mp->get_enabled()){
	      mp->update_block(curr_req.addr, 1);
	    }
	    dl1->allocate(curr_req.addr);
	    dl1->write(curr_req.addr, curr_req.value);
	    dl1->set_hits(dl1->get_hits()-1);
	    dl1->set_accs(dl1->get_accs()-1);
	  }else{    
	    dl1->write(curr_req.addr, curr_req.value);
	    dl1->set_hits(dl1->get_hits()-1);
	    dl1->set_accs(dl1->get_accs()-1);
	  }
	  mismatches++;
	}else{
	  if (sval == 0 && zero == 0 && mp != 0 && mp->get_enabled()){
	    mp->get_tlb()->zeros++;
	  }
	}
	if (zero == 0 && curr_req.value != 0){
	  mismaps++;
	}
      }else{
	if (zero == 0 && mp != 0 && mp->get_enabled() && rdalloc == 0){
	  mp->update_block(curr_req.addr, 1);
	  //printf("Calling cache_allocate for %08X\n", addr);
	  dl1->allocate(curr_req.addr); // special function to allocate a cache line with all zero
	  nbupdates++;
	}
	dl1->write(curr_req.addr, curr_req.value); // this needs to return a delay?
	delay = CL1DELAY;
      }

      totaldelay += delay;
      curr_req.fill_cycle = curr_ck + delay - 1;
      curr_req.inprogress = 1;

      // finish preparation of core->monitor queue packet
      if (fr != 0){
	curr_ic = curr_req.icount; // don't insert stall between consecutive instructions      
	temp_req->ismem = 1;
	temp_req->isRead = curr_req.isRead;
	temp_req->addr = curr_req.addr;
	temp_req->value = curr_req.value;
      }
    }
  }

  /*if (fr != 0){
    printf("Stall for %s when curr_req.valid is %d, curr_ic is %llu, fill_cycle is %llu, curr_ck is %llu, queue entries is %lu\n", name, curr_req.valid, curr_ic, curr_req.fill_cycle, curr_ck, qp->size());
    }*/

  return 0;
}

void coredrv::stats(){
  printf("---------- %s performance data ----------\n", name);  
    
  if (mp != 0){
    mp->stats();
  }
  if (dl1 != 0){
    dl1->stats();
  }
  if (dl2 != 0){
    dl2->stats();
  }
  
  printf("%llu initialization mismatches encountered\n", mismatches);
  printf("%llu mapping mismatches encountered\n", mismaps);
  printf("%llu Total memory stall cycles: %f average memory delay\n", totaldelay, (float)(totaldelay)/(accesses-warm_accs));
  printf("%u Null Bit updates on L1 write\n", nbupdates);
  if (fr != 0){
    printf("%llu instructions commmited in %llu cycles\n", curr_ic-warm_ic, clocks-warm_ck);
    printf("Simulated IPC: %1.3f\n", (float)(curr_ic-warm_ic)/(clocks-warm_ck));
    printf("%llu Queue full stall cycles\n", qstallcyc);
    printf("%llu Non-memory instructions in traces\n", totaligap);
  }
}

i32 coredrv::get_accs(){
  return accesses;
}

i64 coredrv::get_ic(){
  return curr_ic;
}

void coredrv::set_done(){
  done = 1;
}
