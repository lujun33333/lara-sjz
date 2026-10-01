#include "sjz/OwnGameData.h"
#include "sjz/CnSkeletonLayout.h"
#include <cassert>
#include <map>
#include <limits>
#include <iostream>
struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    int calls=0; uintptr_t fail=0,change=0,replacement=0; int changeReads=0;
    template<class T> void put(uintptr_t p,const T& v) {
        auto in=reinterpret_cast<const unsigned char*>(&v);
        for(size_t i=0;i<sizeof(v);++i) bytes[p+i]=in[i];
    }
    void zero(uintptr_t p,size_t n) { for(size_t i=0;i<n;++i) bytes[p+i]=0; }
    static bool read(void* ctx,uintptr_t p,void* out,size_t n) {
        auto& m=*static_cast<Memory*>(ctx); ++m.calls;
        if(m.fail>=p && m.fail-p<n) return false;
        if(p==m.change && ++m.changeReads==2) m.put(p,m.replacement);
        auto dst=static_cast<unsigned char*>(out);
        for(size_t i=0;i<n;++i) {auto it=m.bytes.find(p+i);if(it==m.bytes.end()) return false;dst[i]=it->second;}
        return true;
    }
};
int main() {
    constexpr uintptr_t actor=0x200000,ability=0x210000,health=0x220000,mesh=0x230000,bones=0x240000;
    Memory m; OwnMemoryReader r{&m,Memory::read};
    m.put(actor+0x10b8,ability);m.put(ability+0x280,health);m.zero(health+0x3c,0xec);
    const int offsets[]={0x3c,0x54,0x114,0x124,0x74,0x8c,0x9c,0xb4};
    for(int i=0;i<8;++i) m.put(health+offsets[i],float(80+i));
    OwnHealthValues h; assert(OwnReadHealth(r,actor,h));assert(m.calls==4);
    for(int i=0;i<8;++i) assert(h.values[i]==80+i);
    m.put(health+0x124,std::numeric_limits<float>::quiet_NaN());assert(!OwnReadHealth(r,actor,h));
    m.put(health+0x124,83.f);m.change=ability+0x280;m.replacement=health+0x10000;m.changeReads=0;
    assert(!OwnReadHealth(r,actor,h));m.change=0;m.put(ability+0x280,health);
    m.put(actor+0x3d0,mesh);m.put(mesh+0xa18,bones);m.zero(bones,65*0x30);
    struct Transform {std::array<float,4> rotation;OwnVector3 position,scale;};
    m.put(mesh+0x210,Transform{{0,0,0,1},{100,200,300},{1,1,1}});
    for(auto i:kCnHumanMeshIndices) m.put(bones+i*0x30+0x10,OwnVector3{float(i),float(i+1),float(i+2)});
    OwnWorldSkeleton s;m.calls=0;assert(OwnReadCnSkeleton(r,actor,s));assert(m.calls==6);
    for(size_t i=0;i<18;++i) assert(s.valid[i] && s.points[i].x==100+kCnHumanMeshIndices[i]);
    m.fail=bones+5*0x30+0x10;assert(!OwnReadCnSkeleton(r,actor,s));m.fail=0;
    m.change=mesh+0xa18;m.replacement=bones+0x10000;m.changeReads=0;assert(!OwnReadCnSkeleton(r,actor,s));
    std::cout<<"PASS: bulk health/bone read budgets, finite values, missing points and pointer changes\n";
}
