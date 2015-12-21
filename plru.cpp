#include "plru.h"

plru::plru(i32 ways){
  levels = log2(ways);
  bits = new char[ways-1];
  for (int i=0;i<ways-1;i++){
    bits[i] = 0;
  }
}

i32 plru::get_lru(){
  i32 index = 0;
  i32 entry = bits[index];

  // To find a pseudo-LRU element
  // traverse the tree according to the values of the flags. 
  for (int i=levels-1;i>0;i--){
    // find next entry in tree
    if (bits[index] == 0){
      index += 1;
    }else{
      index += (bits[index]) << i;
    }

    //printf("Index now at %d for level %d\n", index, i);

    entry = entry << 1;
    entry |= bits[index];
  }

  // return the plru entry
  return entry;
}

void plru::update_lru(i32 way){
  i32 dir;
  i32 index = 0;

  // To update the tree with an access to an item N
  for (int i=levels-1;i>=0;i--){
  
    // set the node flags to denote the direction that is opposite to the direction taken.
    dir = (way >> i) & 1;
    bits[index] = (dir==1)?0:1;

    // continue traversing the tree to find N
    if (dir == 0){
      index += 1;
    }else{
      index += dir << i;
    }
  }
}
