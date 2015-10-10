#ifndef STORE_H
#define STORE_H

#include "utils.h"

typedef struct mem_page {
  i64 data[512];
} tpage;

class tmemory {
  tpage** pages;
  i32 pmask;
  i32 fmask;
  i32 os;
  i32 pshift;
  i32 ishift;
  i64 nextfree;
 public:
  tmemory(i32 tg, i32 ts);
  i64 read(i32 addr);
  void write(i32 addr, i64 data);
  i64 load();
};

#endif /* STORE_H */
