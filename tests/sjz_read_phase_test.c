#include "sjz/SJZReadPhase.h"
#include <assert.h>
#include <stdio.h>
struct target {uint8_t live[4][16],alias[4][16];int fresh[4],reads[4];uint64_t fail;};
static const uint8_t* map(void* context,uint64_t page,bool fresh) {
    struct target* t=context;
    if(page>=64 || page==t->fail) return NULL;
    size_t i=(size_t)(page/16);++t->reads[i];
    if(fresh) {++t->fresh[i];memcpy(t->alias[i],t->live[i],16);}
    return t->alias[i];
}
int main(void) {
    struct target t={0};t.fail=UINT64_MAX;
    for(size_t i=0;i<64;++i) t.live[i/16][i%16]=(uint8_t)i;
    uint64_t pages[4];size_t count=0;uint8_t out[64]={0};
    assert(sjz_phase_read(8,out,32,16,pages,&count,4,&t,map)==32);
    for(size_t i=0;i<32;++i) assert(out[i]==i+8);
    assert(count==3 && t.fresh[0]==1 && t.fresh[1]==1 && t.fresh[2]==1);
    assert(sjz_phase_read(16,out,4,16,pages,&count,4,&t,map)==4 && t.fresh[1]==1);
    // A new phase/readback resolves current target data without refreshing other pages.
    t.live[1][0]=99;count=0;
    assert(sjz_phase_read(16,out,1,16,pages,&count,4,&t,map)==1 && out[0]==99);
    assert(t.fresh[0]==1 && t.fresh[1]==2 && t.fresh[2]==1);
    count=0;t.fail=16;
    assert(sjz_phase_read(8,out,24,16,pages,&count,4,&t,map)==8 && count==1);
    count=0;assert(sjz_phase_read(16,out,4,16,pages,&count,4,&t,map)==-1 && count==0);
    t.fail=UINT64_MAX;count=0;
    assert(sjz_phase_read(8,out,24,16,pages,&count,1,&t,map)==8 && count==1);
    assert(sjz_phase_read(UINT64_MAX-2,out,4,16,pages,&count,4,&t,map)==-1);
    assert(sjz_phase_read(0,out,4,3,pages,&count,4,&t,map)==-1);
    puts("PASS: phase-local refresh, readback remap, cross-page partials and bounded sets");
}
