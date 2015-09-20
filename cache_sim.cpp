#include "utils.h"
#include "store.h"
#include "freader.h"
#include "coredrv.h"
using namespace std;

#define RANGE 1<<28

int main(int argc, char** argv){
    if (argc != 10){
      printf( "usage: %s (taggran) (tagsize) (mapon) (associativity) (sets) (bsize) (skip) (dir) filename\n", argv[0]);
      exit(1);
    }
    
    queue<cache_req*> iq;
    core_args* cas = new core_args();
    core_args* mas = new core_args();
    i32 cret = 0;
    i32 mret = 0;
    i64 curr_ck = 0;
    char* names[2] = {"CORE", "MONITOR"};
    freader* fr = new freader(atoi(argv[2]), argv[8], argv[9]);

    cas->taggran = atoi(argv[1]);
    cas->tagsize = cas->taggran; //atoi(argv[2]);
    cas->mapon = 0; //atoi(argv[3]);
    cas->assoc = 16; //atoi(argv[4]);
    cas->pagesize = 4096;
    cas->sets = 1024; //atoi(argv[5]);
    cas->bsize = 64; //atoi(argv[6]);
    cas->skip = atoi(argv[7]) * 1000000;
    cas->dir = argv[8];
    cas->bench = argv[9];
    cas->qp = &iq;
    cas->name = names[0];
    cas->fr = fr;

    tmemory* sp = new tmemory(cas->taggran, cas->tagsize);
    cas->sp = sp;

    mas->taggran = atoi(argv[1]);
    mas->tagsize = atoi(argv[2]);
    mas->mapon = atoi(argv[3]);
    mas->assoc = atoi(argv[4]);
    cas->pagesize = 4096;
    mas->sets = atoi(argv[5]);
    mas->bsize = atoi(argv[6]);
    mas->skip = atoi(argv[7]) * 1000000;
    mas->qp = &iq;
    mas->sp = sp;
    mas->name = names[1];
    mas->fr = 0;

    coredrv* core = new coredrv(cas);
    coredrv* monitor = new coredrv(mas);

    while (cret == 0 || mret == 0){
      if (cret == 0){
	cret = core->clock(curr_ck);
      }
      if (cret == 1){
	core->stats();
      }
      if (mret == 0){
	mret = monitor->clock(curr_ck);
      }
      if (mret == 1){
	monitor->stats();
      }
      curr_ck++;
    }

    printf("Core Simulation complete after %u core accesses and %u monitor accesses\n", core->get_accs(), monitor->get_accs());
    
    // cleanup
    free(cas);
    free(mas);
    return 0;
}
