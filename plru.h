#ifndef PLRU_H
#define PLRU_H

#include "utils.h"

class plru{
  char * bits;
  i32 levels;
 public:
  plru(i32 ways);
  i32 get_lru();
  void update_lru(i32 way);
};

#endif
