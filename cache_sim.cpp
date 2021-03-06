#include "utils.h"
#include "store.h"
#include "freader.h"
#include "coredrv.h"
#include "mondrv.h"
using namespace std;

#define RANGE 1<<28

i64 curr_ck;
float mem_energy;

int main(int argc, char** argv){
    if (argc != 11){
      printf("usage: %s (taggran) (tagsize) (readalloc) (mapon) (associativity) (sets) (bsize) (skip) (dir) filename\n", argv[0]);
      exit(1);
    }

    // (taggran)=1 (tagsize)=2 (readalloc)=3 (mapon)=4 (associativity)=5 (sets)=6 (bsize)=7 (skip)=8 (dir)=9 filename=10
    
    queue<cache_req*> iq;
    core_args* cas = new core_args();
    core_args* mas = new core_args();
    i32 cret = 0;
    i32 mret = 0;
    char* names[2] = {"CORE", "MONITOR"};
    freader* fr = new freader(atoi(argv[2]), argv[9], argv[10]);
    bool cp = false;
    curr_ck = 0;
    mem_energy = 0;

    cas->taggran = atoi(argv[1]);
    cas->tagsize = cas->taggran; //atoi(argv[2]);
    cas->rdalloc = 1;
    cas->mapon = 0; //atoi(argv[4]);
    cas->pagesize = 4096;
    cas->l1assoc = 6;
    cas->l1sets = 64;
    cas->l2assoc = 8; //atoi(argv[5]);
    cas->l2sets = 1024; //atoi(argv[6]);
    cas->bsize = 64; //atoi(argv[7]);
    cas->skip = atoi(argv[8]) * 1000000;
    cas->dir = argv[9];
    cas->bench = argv[10];
    cas->qp = &iq;
    cas->name = names[0];
    cas->fr = fr;
    cas->l1delay = CL1DELAY;
    cas->l2delay = CL2DELAY;
    cas->l1_read_energy = CL1RE;
    cas->l1_write_energy = CL1WE;
    cas->l2_read_energy = CL2RE;
    cas->l2_write_energy = CL2WE;

    tmemory* sp = new tmemory(atoi(argv[1]), atoi(argv[2]), MEMDELAY);
    cas->sp = sp;

    mas->taggran = atoi(argv[1]);
    mas->tagsize = atoi(argv[2]);
    mas->rdalloc = atoi(argv[3]);
    mas->mapon = atoi(argv[4]);
    mas->pagesize = 4096;
    mas->l1assoc = 2;
    mas->l1sets = 32;
    mas->l2assoc = atoi(argv[5]);
    mas->l2sets = atoi(argv[6]);
    mas->bsize = atoi(argv[7]);
    mas->skip = atoi(argv[8]) * 1000000;
    mas->qp = &iq;
    mas->sp = sp;
    mas->name = names[1];
    mas->fr = 0;
    mas->l1delay = ML1DELAY;
    mas->l2delay = ML2DELAY;
    mas->l1_read_energy = ML1RE;
    mas->l1_write_energy = ML1WE;
    mas->l2_read_energy = ML2RE;
    mas->l2_write_energy = ML2WE;

    coredrv* core = new coredrv(cas);
    mondrv* monitor = new mondrv(mas);

    while (cret == 0 || mret == 0){
      if (cret == 0){
	cret = core->clock();
      }
      if (cret == 1 && cp == false){
	core->stats();
	monitor->set_done();
	cp = true;
      }
      if (mret == 0){
	mret = monitor->clock();
      }
      if (mret == 1){
	monitor->stats();
      }
      curr_ck++;
    }

    printf("Core Simulation complete after %llu core instructions, %u core accesses and %u monitor accesses and %llu simulated cycles\n", core->get_ic(), core->get_accs(), monitor->get_accs(), curr_ck);
    
    // cleanup
    free(cas);
    free(mas);
    return 0;
}
