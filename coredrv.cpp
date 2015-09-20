#include "coredrv.h"

coredrv::coredrv(core_args* args){
  // read input arguments
  taggran = ((core_args*)args)->taggran;
  tagsize = ((core_args*)args)->tagsize;
  mapon = ((core_args*)args)->mapon;
  assoc = ((core_args*)args)->assoc;
  pagesize = ((core_args*)args)->pagesize;
  sets = ((core_args*)args)->sets;
  bsize = ((core_args*)args)->bsize;
  skip = ((core_args*)args)->skip;
  //dir = ((core_args*)args)->dir;
  //bench = ((core_args*)args)->bench;
  qp = ((core_args*)args)->qp;
  sp = ((core_args*)args)->sp;
  name = ((core_args*)args)->name;
  fr = ((core_args*)args)->fr;
  pmask = ~((1 << (int)(log2(pagesize) - (6-log2(taggran))-(6-log2(tagsize)))) - 1);
  done = 0;
  
  accesses = 0;
  // cache delay counters
  m1cyc = 0;
  m20cyc = 0;
  m200cyc = 0;
  // misc stall counters
  qstallcyc = 0;
  totaligap = 0;
  // track instruction count at last access
  lastic = 0;

  // "I'm alive print out"
  printf("MDCacheSim (last updated 3-14-15):\n");
  printf("Tag Size and Granularity: %u-bit tag per %s\n", tagsize, (taggran==8)?"byte":(taggran==32)?"word":"doubleword");
  printf("Optimizations: Locality %s\n", (mapon==1)?"on":"off");
  printf("Page Size=%u and Page Mask=%x\n", pagesize, pmask); 

  // initialize cache and local variables;
  dl1 = new tcache(64, 4, bsize, taggran, tagsize, L1DELAY, L2DELAY);
  dl2 = new tcache(sets, assoc, bsize, taggran, tagsize, L2DELAY, MEMDELAY);
  mp = new mem_map(mapon, pagesize, bsize, 32, taggran, tagsize); // added enable (0-off,1-on)

  dl1->set_nl(dl2);
  dl2->set_mem(sp);
  dl2->set_map(mp);
  dl1->set_name("L1");
  dl2->set_name("L2");

  curr_ic = 0;
  warm_ic = 0;
  warm_ck = 0;
  memset(&curr_req, 0, sizeof(sim_req));
  temp_req = 0;
}

i32 coredrv::clock(i64 curr_ck){
  if ((curr_req.valid == 1) && (curr_req.inprogress == 1) && (curr_req.fill_cycle <= curr_ck)){
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
    if (fr!=0 && accesses % 1000000 == 0){
      fprintf(stderr, "Now at %u accesses in the monitored application\n", accesses);
    }
    if (accesses == skip){
      dl1->clearstats();
      dl2->clearstats();
      if (mp != 0){
	mp->clearstats();
      }
      if (fr != 0){
	printf("%s warmup completed at %u accesses and %llu instructions\n", name, accesses, curr_ic);
	warm_ic = curr_ic;
	warm_ck = curr_ck;
	m1cyc = 0;
	m20cyc = 0;
	m200cyc = 0;
	qstallcyc = 0;
	totaligap = 0;
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
      
      // reference the cache hierarchy
      if (mp != 0){
	zero = mp->lookup(curr_req.addr & pmask);
      }
      //printf("result of lookup for address %08X in memmap: %d, is read?: %d\n", addr, zero,       strncmp(buf1, "write", 5));
      dl1->set_anum(accesses);
      dl2->set_anum(accesses);
      
      if (curr_req.isRead == 0){
	// check the map first
	if (zero == 1){
	  crdata rdata = dl1->read(curr_req.addr);
	  sval = rdata.value;
	  delay = rdata.delay;
	  if (fr != 0){
	    if (delay == 1){
	      m1cyc++;
	    }	else if (delay == 20){
	      m20cyc++;
	    } else if (delay == 200){
	      m200cyc++;
	    } else {
	      printf("Unexpected access delay of %u on memory request\n", delay);
	      assert(0);
	    }
	  }  
	}else{
	  sval = 0;
	}
	if (sval != curr_req.value){
	  //printf("Access(%u): Store and trace unmatched for addr (%X): s(%llX), t(%llX)\n", coreaccs, addr, sval, value);
	  //assert(0);
	  mismatches++;
	  // fix up to prevent later mismatches
	  if (sval == 0){
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
	}else{
	  if (sval == 0 && zero == 0 && mp != 0 && mp->get_enabled()){
	    mp->get_tlb()->zeros++;
	  }
	}
	if (zero == 0 && curr_req.value != 0){
	  mismaps++;
	}
      }else{
	if (zero == 0 && mp != 0 && mp->get_enabled()){
	  mp->update_block(curr_req.addr, 1);
	  //printf("Calling cache_allocate for %08X\n", addr);
	  dl1->allocate(curr_req.addr); // special function to allocate a cache line with all zero
	}
	dl1->write(curr_req.addr, curr_req.value); // this needs to return a delay?
      }
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

  if (fr != 0){
    printf("Stall for %s when curr_req.valid is %d, curr_ic is %llu, fill_cycle is %llu, curr_ck is %llu, queue entries is %lu\n", name, curr_req.valid, curr_ic, curr_req.fill_cycle, curr_ck, qp->size());
  }
  // is this legal?
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
  if (fr != 0){
    printf("%llu instructions commmited in %llu cycles\n", curr_ic-warm_ic, clocks-warm_ck);
    printf("Simulated IPC: %1.3f\n", (float)(curr_ic-warm_ic)/(clocks-warm_ck));
    printf("%u Total memory stall cycles: %u 1cycle, %u 20cycles, %u 200cycles\n", m1cyc+m20cyc+m200cyc, m1cyc, m20cyc, m200cyc);
    printf("%llu Queue full stall cycles\n", qstallcyc);
    printf("%llu Non-memory instructions in traces\n", totaligap);
  }
}

i32 coredrv::get_accs(){
  return accesses;
}

void coredrv::set_done(){
  done = 1;
}
