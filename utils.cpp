unsigned int pow2(unsigned int v){
  if ((v > 0) && (v & (v - 1)) == 0){
    return 1;
  }else{
    return 0;
  }
}
