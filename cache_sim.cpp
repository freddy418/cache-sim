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

pthread_mutex_t qlock;
pthread_mutex_t cdlock;
pthread_barrier_t bar;

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
  i32 taggran, tagsize, mapon, sets, bsize, assoc, pmask, skip, done;
  unsigned long mismatches = 0;
  unsigned long mismaps = 0;
  unsigned long pagesize = 4096;
  char* bench;
  char* dir;
  queue<cache_req*>* qp;
  tmemory* sp;

  i32 m1cyc = 0;
  i32 m20cyc = 0;
  i32 m200cyc = 0;
  i32 qstallcyc = 0;
  i32 totaligap = 0;
  i32 lastic = 0;
  coreaccs = 0;
  done = 0;
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
  cache_req* temp = 0;
  
  while (true){
    // model 1 cycle queue delay
    if (temp != 0 && temp->valid == 1){	
      pthread_mutex_lock (&qlock);  // grab the qlock
      qp->push(temp);
      pthread_mutex_unlock (&qlock);  // let it go
      temp = 0; // monitor will handle dealloc
    }

    // if request not valid or previous request was fulfilled, read the next request
    if (curr_req.valid == 0){
      if (done == 1){
	printf("Core is done, core thread exiting\n");
	break; //safely exit
      }
      cache_req* req = fr->fread(); //read next line from trace
      if (req == NULL){ // if EOF, end simulation
	done = 1;
      }else{
	curr_req.icount = req->icount;
	curr_req.addr = req->addr;
	curr_req.isRead = req->isRead;
	curr_req.value = req->value;
	curr_req.inprogress = 0;
	curr_req.valid = 1;
	free(req);

	// count the gap (for understanding cycle count)
	if ((req->icount - lastic) > 1){
	  totaligap += ((req->icount)-lastic)-1;
	}
	lastic = req->icount;
	// end count the gap
	
	// clear stats collected during warmup
	coreaccs++;
	/*if (coreaccs % 10000000 == 0){
	  fprintf(stderr, "Now at %u accesses in the monitored application\n", coreaccs);
	  }*/
	if (coreaccs == skip){
	  printf("Core warmup completed at %u accesses and %llu instructions\n", coreaccs, curr_ic);
	  dl1->clearstats();
	  dl2->clearstats();
	  if (mp != 0){
	    mp->clearstats();
	  }
	  warm_ic = curr_ic;
	  warm_ck = curr_ck;
	  m1cyc = 0;
	  m20cyc = 0;
	  m200cyc = 0;
	  qstallcyc = 0;
	  totaligap = 0;
	}
      }
    }
    
    if (curr_req.valid == 1){
      pthread_mutex_lock (&qlock);  // grab the qlock
      i32 queue_available = (qp->size() < QSIZE) ? 1 : 0;
      pthread_mutex_unlock (&qlock);  // let it go
      // if queue isn't full, process the next instruction
      if ((queue_available == 1) && (curr_req.valid == 1)){
	temp = new cache_req();
	temp->ismem = 0;
	temp->valid = 1;

	if (curr_req.icount == curr_ic || (curr_req.icount == curr_ic+1 && curr_req.inprogress == 0)){
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
	  curr_ic = curr_req.icount; // don't insert stall between consecutive instructions

	  temp->ismem = 1;
	  temp->isRead = curr_req.isRead;
	  temp->addr = curr_req.addr;
	  temp->value = curr_req.value;
	}else{
	  curr_ic++;
	}
      }
      if ((queue_available == 0) && (curr_req.valid == 1)){
	qstallcyc++; // count when queue is full
      }
    }

    curr_ck++;
    if ((curr_req.valid == 1) && (curr_req.inprogress == 1) && (curr_req.fill_cycle <= curr_ck)){
      curr_req.valid = 0;
    }

    if (done == 1){
      printf("Core is done, now waiting at barrier\n");
      pthread_mutex_lock (&cdlock);
      printf("Core past barrier, setting coredone\n");
      coredone = 1;
      pthread_mutex_unlock (&cdlock);
    }
    pthread_barrier_wait (&bar);
    if (done == 1){
      printf("Core is done, got past barrier\n");
    }
  }

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
  printf("%u Total memory stall cycles: %u 1cycle, %u 20cycles, %u 200cycles\n", m1cyc+m20cyc+m200cyc, m1cyc, m20cyc, m200cyc);
  printf("%u Queue full stall cycles\n", qstallcyc);
  printf("%u Non-memory instructions in traces\n", totaligap);
}

void* monitor_sim(void *args){
  i32 taggran, tagsize, mapon, sets, bsize, assoc, addr, zero, isRead, ismem, pmask, skip;
  i32 skip_bar = 0;
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
  tcache* dl1 = new tcache(32, 2, bsize, taggran, tagsize, L1DELAY, L2DELAY);
  tcache* dl2 = new tcache(sets, assoc, bsize, taggran, tagsize, L2DELAY, MEMDELAY);
  mem_map* mp = new mem_map(mapon, pagesize, bsize, 32, taggran, tagsize); // added enable (0-off,1-on)

  dl1->set_nl(dl2);
  dl2->set_mem(sp);
  dl2->set_map(mp);
  dl1->set_name("ML1");
  dl2->set_name("ML2");
  
  // for performance model
  sim_req curr_req;    
  memset(&curr_req, 0, sizeof(sim_req));
  
#ifndef TEST
  while (true){    // if request not valid or previous request was fulfilled, read the next request
    if (curr_req.valid == 0){
      pthread_mutex_lock (&qlock);  // grab the qlock
      i32 qempty = qp->empty() ? 1 : 0;
      pthread_mutex_unlock (&qlock);  // let it go
      if (qempty == 0){
	pthread_mutex_lock (&qlock);  // grab the qlock
	cache_req* req = qp->front(); // read the queue
	assert (req != NULL);
	curr_req.icount = req->icount;
	curr_req.addr = req->addr;
	curr_req.isRead = req->isRead;
	curr_req.value = req->value;
	curr_req.valid = req->ismem;
	qp->pop();
	pthread_mutex_unlock (&qlock);  // release the queue lock
	curr_req.inprogress = 0;
	free(req);

	if (curr_req.valid == 1){
	  monaccs++;
	  if (monaccs == skip){
	    printf("Monitor warmup completed at %u accesses\n", monaccs);
	    dl1->clearstats();
	    dl2->clearstats();
	    if (mp != 0){
	      mp->clearstats();
	    }
	  }
	}
      }else if (skip_bar == 1){
	// all done
	printf("Monitor complete, queue has %lu entries remaining\n", qp->size());
	break;
      }
    }

    if (curr_req.valid == 1){
      if (curr_req.inprogress == 0){ // handle the access
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
	  dl1->write(curr_req.addr, curr_req.value); // this needs to return a delay?
	}
	curr_req.fill_cycle = curr_ck + delay - 1;
	curr_req.inprogress = 1;
      }
      else if (curr_req.fill_cycle <= curr_ck){
	curr_req.valid = 0; // done with metadata access
      }
    }

    if (skip_bar == 0){
      pthread_barrier_wait (&bar); // wait for the clock to tick
      pthread_mutex_lock (&cdlock);
      skip_bar = coredone;
      pthread_mutex_unlock (&cdlock);
      if (skip_bar == 1){
	printf("No more barriers for monitor thread\n");
	sleep(10);
      }
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

    pthread_mutex_init(&qlock, NULL);
    pthread_mutex_init(&cdlock, NULL);
    pthread_barrier_init(&bar, NULL, 2);

    core_args* cas = new core_args();
    cas->taggran = atoi(argv[1]);
    cas->tagsize = cas->taggran; //atoi(argv[2]);
    cas->mapon = 0; //atoi(argv[3]);
    cas->assoc = 16; //atoi(argv[4]);
    cas->sets = 1024; //atoi(argv[5]);
    cas->bsize = 64; //atoi(argv[6]);
    cas->skip = atoi(argv[7]) * 1000000;
    cas->dir = argv[8];
    cas->bench = argv[9];
    cas->qp = &iq;

    core_args* mas = new core_args();
    mas->taggran = atoi(argv[1]);
    mas->tagsize = atoi(argv[2]);
    mas->mapon = atoi(argv[3]);
    mas->assoc = atoi(argv[4]);
    mas->sets = atoi(argv[5]);
    mas->bsize = atoi(argv[6]);
    mas->skip = atoi(argv[7]) * 1000000;
    mas->qp = &iq;

    tmemory* sp = new tmemory(cas->taggran, cas->tagsize);
    cas->sp = sp;
    mas->sp = sp;

    // fork off core and monitor to run independently
    pthread_t coreth, month;
    coredone = 0;
    
#ifndef TEST
    if (pthread_create(&coreth, NULL, core_sim, cas)){
      fprintf(stderr, "Failed to create core thread\n");
      exit(1);
    }
#endif

    if (pthread_create(&month, NULL, monitor_sim, mas)){
      fprintf(stderr, "Failed to create monitor thread\n");
      exit(1);
    }

#ifndef TEST
    if (pthread_join(coreth, NULL)){
      fprintf(stderr, "Failed to rejoin core thread\n");
      exit(1);
    }
#endif

    if (pthread_join(month, NULL)){
      fprintf(stderr, "Failed to rejoin monitor thread\n");
      exit(1);
    }
 
    printf("Core Simulation complete after %u core accesses and %u monitor accesses\n", coreaccs, monaccs);
    
    // cleanup
    pthread_mutex_destroy(&cdlock);
    pthread_mutex_destroy(&qlock);
    pthread_exit(NULL);
    free(cas);
    free(mas);
    return 0;
}
