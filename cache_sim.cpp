#include "utils.h"
#include "store.h"
#include "tcache.h"
#include "memmap.h"
#include "freader.h"
#include <pthread.h>

using namespace std;

/* Define globally accessible variables and mutexes (if any) */

#define RANGE 1<<28
#define QSIZE 16

unsigned int coreaccs;
unsigned int monaccs;
unsigned int coredone;

unsigned long long curr_ck = 0;

pthread_mutex_t cdlock;

typedef struct core_args_t {
  i32 taggran;
  i32 tagsize;
  i32 mapon;
  i32 assoc;
  i32 sets;
  i32 bsize;
  i32 skip;
  char * dir;
  char * bench;
  std::queue<cache_req*>* qp;
  tmemory* sp;
} core_args;

int bin2dec(char *bin)   
{
  int  b, k, m, n;
  int  len, sum = 0;
  len = strlen(bin) - 1;
  for(k = 0; k <= len; k++) 
    {
      n = (bin[k] - '0'); // char to numeric value
      if ((n > 1) || (n < 0)) 
	{
	  puts("\n\n ERROR! BINARY has only 1 and 0!\n");
	  return (0);
	}
      for(b = 1, m = len; m > k; m--) 
	{
	  // 1 2 4 8 16 32 64 ... place-values, reversed here
	  b *= 2;
	}
      // sum it up
      sum = sum + n * b;
      //printf("%d*%d + ",n,b);  // uncomment to show the way this works
    }
  return(sum);
}

void *core_sim(void* args){
  i32 taggran, tagsize, mapon, sets, bsize, assoc, pmask, skip;
  unsigned long mismatches = 0;
  unsigned long mismaps = 0;
  unsigned long pagesize = 4096;
  char* bench;
  char* dir;
  queue<cache_req*>* qp;
  tmemory* sp;

  coreaccs = 0;
  // read input arguments
  taggran = ((core_args*)args)->taggran;
  tagsize = ((core_args*)args)->tagsize;
  mapon = ((core_args*)args)->mapon;
  assoc = ((core_args*)args)->assoc;
  sets = ((core_args*)args)->sets;
  bsize = ((core_args*)args)->bsize;
  skip = ((core_args*)args)->skip;
  dir = ((core_args*)args)->dir;
  bench = ((core_args*)args)->bench;
  qp = ((core_args*)args)->qp;
  sp = ((core_args*)args)->sp;
  pmask = ~((1 << (int)(log2(pagesize) - (6-log2(taggran))-(6-log2(tagsize)))) - 1);

  printf("MDCacheSim (last updated 3-14-15):\n");
  printf("Tag Size and Granularity: %u-bit tag per %s\n", tagsize, (taggran==8)?"byte":(taggran==32)?"word":"doubleword");
  printf("Optimizations: Locality %s\n", (mapon==1)?"on":"off");
  printf("Page Size=%lu and Page Mask=%x\n", pagesize, pmask); 

  // initialize cache and local variables;
  tcache* dl1 = new tcache(64, 4, bsize, taggran, tagsize, L1DELAY, L2DELAY);
  tcache* dl2 = new tcache(sets, assoc, bsize, taggran, tagsize, L2DELAY, MEMDELAY);
  mem_map* mp = new mem_map(mapon, pagesize, bsize, 32, taggran, tagsize); // added enable (0-off,1-on)
  freader* fr = new freader(tagsize, dir, bench);

  dl1->set_nl(dl2);
  dl2->set_mem(sp);
  dl2->set_map(mp);
  dl1->set_name("L1");
  dl2->set_name("L2");
  
  // for performance model
  sim_req curr_req;
  unsigned long long curr_ic = 0;
  unsigned long long warm_ic = 0;
  unsigned long long warm_ck = 0;
  memset(&curr_req, 0, sizeof(sim_req));
  
  while (true){
    // if request not valid or previous request was fulfilled, read the next request
    if (curr_req.valid == 0){
      cache_req* req = fr->fread(); //read next line from trace
      if (req == NULL){ // if EOF, end simulation
	break;
      }
      
      curr_req.icount = req->icount;
      curr_req.addr = req->addr;
      curr_req.isRead = req->isRead;
      curr_req.value = req->value;
      curr_req.inprogress = 0;
      curr_req.valid = 1;
      free(req);

      // clear stats collected during warmup
      coreaccs++;
      if (coreaccs == skip){
	printf("Warmup completed at %u accesses and %llu instructions\n", coreaccs, curr_ic);
	dl1->clearstats();
	dl2->clearstats();
	if (mp != 0){
	  mp->clearstats();
	}
	warm_ic = curr_ic;
	warm_ck = curr_ck;
      }
    }

    // if queue isn't full, process the next instruction
    if (qp->size() < QSIZE){
      if ((curr_req.icount == curr_ic) && (qp->size() < QSIZE)){
	i64 sval;
	i32 zero;
	i32 delay = 1;
	
	// reference the cache hierarchy
	if (mp != 0){
	  zero = mp->lookup(curr_req.addr & pmask);
	}
	//printf("result of lookup for address %08X in memmap: %d, is read?: %d\n", addr, zero,       strncmp(buf1, "write", 5));
	dl1->set_anum(coreaccs);
	dl2->set_anum(coreaccs);
	
	if (curr_req.isRead == 0){
	  // check the map first
	  if (zero == 1){
	    crdata rdata = dl1->read(curr_req.addr);
	    sval = rdata.value;
	    delay = rdata.delay;
	  }else{
	    sval = 0;
	  }
	  if (sval != curr_req.value){
	    //printf("Access(%u): Store and trace unmatched for addr (%X): s(%llX), t(%llX)\n", coreaccs, addr, sval, value);
	    //assert(0);
	    mismatches++;
	    // fix up to prevent later mismatches
	    if (sval == 0){
	      if (mp != 0){
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
	    if (sval == 0 && zero == 0 && mp != 0){
	      mp->get_tlb()->zeros++;
	    }
	  }
	  if (zero == 0 && curr_req.value != 0){
	    mismaps++;
	  }
	}else{
	  if (zero == 0 && mp != 0){
	    mp->update_block(curr_req.addr, 1);
	    //printf("Calling cache_allocate for %08X\n", addr);
	    dl1->allocate(curr_req.addr); // special function to allocate a cache line with all zero
	  }
	  delay = dl1->write(curr_req.addr, curr_req.value); // this needs to return a delay
	}
	curr_req.fill_cycle = curr_ck + delay - 1;
	curr_req.inprogress = 1;
	/*cache_req* temp = new cache_req();
	  temp->ismem = true;
	  temp->isRead = curr_req.isRead;
	  temp->addr = curr_req.addr;
	  temp->value = curr_req.value;
	  qp->push(temp);*/
      }else{
	curr_ic++;
      }
    }
    curr_ck++;

    if (curr_req.inprogress == 1 && !(curr_req.fill_cycle > curr_ck)){
      curr_req.valid = 0;
    }
  }

  pthread_mutex_lock (&cdlock);
  coredone = 1;
  pthread_mutex_unlock (&cdlock);

  printf("---------- CORE performance data ----------\n");  
    
  if (mp != 0){
    mp->stats();
  }
  if (dl1 != 0){
    dl1->stats();
  }
  if (dl2 != 0){
    dl2->stats();
  }
  
  printf("%lu initialization mismatches encountered\n", mismatches);
  printf("%lu mapping mismatches encountered\n", mismaps);
  printf("%llu instructions commmited in %llu cycles\n", curr_ic-warm_ic, curr_ck-warm_ck);
  printf("Simulated IPC: %1.3f\n", (float)(curr_ic-warm_ic)/(curr_ck-warm_ck));
}

void* monitor_sim(void *args){
  i32 taggran, tagsize, mapon, sets, bsize, assoc, addr, zero, isRead, ismem, pmask, skip;
  i64 value, sval, icount;
  unsigned long mismatches = 0;
  unsigned long mismaps = 0;
  unsigned long pagesize = 4096;
  char* bench;
  char* dir;
  queue<cache_req*>* qp;
  tmemory* sp;

  monaccs = 0;
  // read input arguments
  taggran = ((core_args*)args)->taggran;
  tagsize = ((core_args*)args)->tagsize;
  mapon = ((core_args*)args)->mapon;
  assoc = ((core_args*)args)->assoc;
  sets = ((core_args*)args)->sets;
  bsize = ((core_args*)args)->bsize;
  skip = ((core_args*)args)->skip;
  qp = ((core_args*)args)->qp;
  sp = ((core_args*)args)->sp;
  pmask = ~((1 << (int)(log2(pagesize) - (6-log2(taggran))-(6-log2(tagsize)))) - 1);

  // initialize cache and local variables;
  tcache* dl1 = new tcache(16, 2, bsize, taggran, tagsize, L1DELAY, L2DELAY);
  tcache* dl2 = new tcache(sets, assoc, bsize, taggran, tagsize, L2DELAY, MEMDELAY);
  mem_map* mp = new mem_map(mapon, pagesize, bsize, 32, taggran, tagsize); // added enable (0-off,1-on)

  dl1->set_nl(dl2);
  dl2->set_mem(sp);
  dl2->set_map(mp);
  dl1->set_name("L1");
  dl2->set_name("L2");
  
  // for performance model
  sim_req curr_req;    
  memset(&curr_req, 0, sizeof(sim_req));
  
#ifndef TEST
  while (true){
    if ((curr_req.valid == 1) && (curr_req.fill_cycle > curr_ck)){ // request being fulfilled or queue is empty
      // do nothing
    }
    else if (qp->empty()){
      if (coredone == 1){
	break; // we're done here
      }
    }
    else{ // read next request
      cache_req* req = qp->front(); // grab next instruction from queue
      i32 delay = 1;
      
      ismem = req->ismem;
      icount = req->icount;
      addr = req->addr;
      isRead = req->isRead;
      value = req->value;
    
      // reference the cache hierarchy
      /*if (ismem == 1){
      if (mp != 0){
	zero = mp->lookup(addr & pmask);
      }
      //printf("result of lookup for address %08X in memmap: %d, is read?: %d\n", addr, zero,       strncmp(buf1, "write", 5));
      dl1->set_anum(monaccs);
      dl2->set_anum(monaccs);
    
      if (isRead == 0){
	// check the map first
	if (zero == 1){
	  crdata rdata = dl1->read(addr);
	  sval = rdata.value;
	  delay = rdata.delay;
	}else{
	  sval = 0;
	}
	if (sval != value){
	  //printf("Access(%u): Store and trace unmatched for addr (%X): s(%llX), t(%llX)\n", monaccs, addr, sval, value);
	  //assert(0);
	  mismatches++;
	  // fix up to prevent later mismatches
	  if (sval == 0){
	    if (mp != 0){
	      mp->update_block(addr, 1);
	    }
	    dl1->allocate(addr);
	    dl1->write(addr, value);
	    dl1->set_hits(dl1->get_hits()-1);
	    dl1->set_accs(dl1->get_accs()-1);
	  }else{    
	    dl1->write(addr, value);
	    dl1->set_hits(dl1->get_hits()-1);
	    dl1->set_accs(dl1->get_accs()-1);
	  }
	}else{
	  if (sval == 0 && zero == 0 && mp != 0){
	    mp->get_tlb()->zeros++;
	  }
	}
	if (zero == 0 && value != 0){
	  mismaps++;
	}
      }else{
	if (zero == 0 && mp != 0){
	  mp->update_block(addr, 1);
	  //printf("Calling cache_allocate for %08X\n", addr);
	  dl1->allocate(addr); // special function to allocate a cache line with all zero
	}
	delay = dl1->write(addr, value); // this needs to return a delay
      }
    
      monaccs++;
    
      // clear stats collected during warmup -- this needs to be global
      if (monaccs == skip){
	dl1->clearstats();
	dl2->clearstats();
	if (mp != 0){
	  mp->clearstats();
	}
      }
      }*/  
    
      curr_req.valid = 1;
      curr_req.fill_cycle = curr_ck + delay - 1;
      qp->pop();
      delete req;
    }
  }

#else
  printf("Starting cache sweep test\n");
  unsigned int count = 0;
  unsigned int matches = 0;
  unsigned int inc = taggran >> 3;

  printf("Setting Memory Values\n");
  for (unsigned int i=0;i<RANGE;i+=inc){
    unsigned long long data = i&((1<<tagsize)-1);
    dl1->write(i, data);
  }
    
  printf("Reading back Memory Values\n");
  for (unsigned int i=0;i<RANGE;i+=inc){
    unsigned long long exp = i&((1<<tagsize)-1);
    unsigned long long act = dl1->read(i);
    if (exp != act){
      printf ("Data mismatch for address (%X), actual(%llX), expected(%llX)\n", i, act, exp);
      count++;
    }else{
      matches++;
    }
      
    if (count > 5){
      printf ("FAILED: too many read errors\n");
      exit(1);
    }
  }
  printf("PASSED: %u accesses matched\n", matches);
    
#endif
  
  printf("---------- MONITOR performance data ----------\n");  
  if (mp != 0){
    mp->stats();
  }
  if (dl1 != 0){
    dl1->stats();
  }
  if (dl2 != 0){
    dl2->stats();
  }
  
  printf("%lu initialization mismatches encountered\n", mismatches);
  printf("%lu mapping mismatches encountered\n", mismaps);
}

int main(int argc, char** argv){
    if (argc != 10){
      printf( "usage: %s (taggran) (tagsize) (mapon) (associativity) (sets) (bsize) (skip) (dir) filename\n", argv[0]);
      exit(1);
    }
    
    queue<cache_req*> iq;
    core_args* cas = new core_args();

    pthread_mutex_init(&cdlock, NULL);
    cas->taggran = atoi(argv[1]);
    cas->tagsize = atoi(argv[2]);
    cas->mapon = atoi(argv[3]);
    cas->assoc = atoi(argv[4]);
    cas->sets = atoi(argv[5]);
    cas->bsize = atoi(argv[6]);
    cas->skip = atoi(argv[7]) * 1000000;
    cas->dir = argv[8];
    cas->bench = argv[9];
    cas->qp = &iq;

    tmemory* sp = new tmemory(cas->taggran, cas->tagsize);
    cas->sp = sp;

    // fork off core and monitor to run independently
    pthread_t coreth, month;
    coredone = 0;
    
#ifndef TEST
    if (pthread_create(&coreth, NULL, core_sim, cas)){
      fprintf(stderr, "Failed to create core thread\n");
      exit(1);
    }
#endif

    /*if (pthread_create(&month, NULL, monitor_sim, cas)){
      fprintf(stderr, "Failed to create monitor thread\n");
      exit(1);
      }*/

#ifndef TEST
    if (pthread_join(coreth, NULL)){
      fprintf(stderr, "Failed to rejoin core thread\n");
      exit(1);
    }
#endif

    /*if (pthread_join(month, NULL)){
      fprintf(stderr, "Failed to rejoin monitor thread\n");
      exit(1);
      }*/

    printf("Core Simulation complete after %u core accesses and %u monitor accesses\n", coreaccs, monaccs);
    
    // cleanup
    pthread_mutex_destroy(&cdlock);
    pthread_exit(NULL);
    free(cas);
    return 0;
}
