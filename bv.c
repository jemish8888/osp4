#include <strings.h>
#include "config.h"
#include "bv.h"

static unsigned char bitvector[(PROC_LIMIT/8) + 1];

void bv_init(){
  /* turn all bits to 0 */
  bzero(bitvector, sizeof(bitvector));
}

int bit_test(const int n){
  return (bitvector[n / 8] & (1 << (n % 8)));
}

int bv_index(){
  int i;
  for(i = 0; i < PROC_LIMIT; i++){
    if(bit_test(i) == 0){
      bv_on(i);
      return i;
    }
  }
  return -1;
}

void bv_on(const int n){
  bitvector[n / 8] ^= (1 << (n % 8));
}

void bv_off(const int n){
  bitvector[n / 8] ^= (1 << (n % 8));
}
