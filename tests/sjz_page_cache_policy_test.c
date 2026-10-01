#include "sjz/SJZPageCachePolicy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
int main(void) {
    bool used[4]={true,true,true,true};
    int identity[4]={10,20,30,40}; size_t cursor=1;
    // Repeated fresh root reads must preserve all unrelated hot aliases.
    for(int frame=0;frame<1000;++frame) {
        used[2]=false;
        size_t empty=4;
        for(size_t i=0;i<4;++i) if(!used[i]) {empty=i;break;}
        size_t slot=sjz_page_cache_replacement(4,&cursor,empty);
        assert(slot==2 && cursor==1);
        used[slot]=true;identity[slot]=30;
        assert(identity[0]==10 && identity[1]==20 && identity[3]==40);
    }
    // Actual capacity pressure still uses bounded FIFO, including wraparound.
    for(size_t i=0;i<8;++i) assert(sjz_page_cache_replacement(4,&cursor,4)==(i+1)%4);
    puts("PASS: repeated root refresh preserves hot pages, full-cache FIFO wraparound");
}
