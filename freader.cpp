#include "freader.h"

freader::freader(i32 ts, char* dir, char* bench){    
    DIR *dp;
    struct dirent *dirp;

    fcnt = 0;
    if((dp  = opendir(dir)) == NULL) {
      fprintf(stderr, "Error (%d) unable to find directory %s\n", errno, dir);
      perror("Invalid directory");
    }
    while ((dirp = readdir(dp)) != NULL) {
      if ((strstr(dirp->d_name, bench) != NULL) && (strstr(dirp->d_name, "log") != NULL)){
	fcnt++;
      }
    }
    closedir(dp);
    
    // hack to simplify debugging
    // fcnt = 1;
    
    if (fcnt == 0){
      fprintf(stderr, "No valid trace files of name %s found\n", bench);
      exit(1);
    }else{
      printf("Traces: %u %s files found in directory %s\n", fcnt, bench, dir);
    }
    // start at 0
    this->dir = dir;
    this->bench = bench;

    // saved local variables
    cf = 0;
    in = 0;
    tagsize = ts;
}

cache_req* freader::read_trace(){  
  char buf[64];
  char buf1[64];
  char buf2[64];
  char buf3[64];
  cache_req* req = NULL;
  
  if (fgets(buf, 64, in)){
    //printf("socket (%d, %d): %s\n", bytes, errno, buf);
    req = new cache_req();
    req->ismem = 1;
    sscanf(buf, "%s %s %x %s", buf3, buf1, &(req->addr), buf2);
    req->icount = strtoull(buf3, NULL, 16); 
    req->value = strtoull(buf2, NULL, 16);
    if (tagsize == 32){
      if (req->value != 0){
	// convert 32b-32b value into 16b-16b value
	char vstr[10];
	i64 origval = req->value;
	i64 vmask = (1ULL << 32) - 1;
	assert((req->value & vmask) < (1<<16));
	assert(((req->value >> 32) & vmask) < (1<<16));
	i32 low = req->value & vmask;
	i32 high = (req->value >> 32) & ((1ULL << 32) - 1);	  
	sprintf(vstr, "%04X%04X", high, low);
	req->value = strtoull(vstr, NULL, 16);
	//printf("%llX converted to %lx - high(%X) low(%X)\n", origval, value, high, low);
      }
    }
    else if (tagsize == 1){
      if (req->value != 0){
	req->value = 1;
      }
    }
    // else do nothing

    //printf("%s\t%08X\t%llX\n", buf1, addr, value);
    req->isRead = strncmp(buf1, "read", 4);
  }else{
    return NULL;
  }

  return req;
}

cache_req* freader::fread(){
  char file[512];
  cache_req* req = (in == 0) ? NULL : read_trace();

  // if read returns nothing (EOF), open the next trace and read from it
  if (req == NULL && cf < fcnt){
    sprintf(file, "%s/%s%d.log", dir, bench, cf);
    in = fopen(file, "r");
    fprintf(stderr, "Reading from file %s\n", file);
    if (in == NULL){
      perror("Invalid file");
    }
    req = read_trace();
    cf++;
  }
  // else that was the last trace, it's over

  return req;
}
