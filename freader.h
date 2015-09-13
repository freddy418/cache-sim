#ifndef FREADER_H
#define FREADER_H

#include "utils.h"

class freader{
  i32 fcnt;
  i32 cf;
  i32 tagsize;
  FILE *in;
  char* bench;
  char* dir;
 public:
  freader(i32 ts, char* dir, char* bench);
  cache_req* read_trace();
  cache_req* fread();
};

#endif /* FREADER_H */
