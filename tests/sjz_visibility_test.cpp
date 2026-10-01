#include "sjz/CnVisibility.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>

struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t fail=0, mutateAt=0, mutateTarget=0;
    template<class T> void put(std::uintptr_t address, const T& value) {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=data[i];
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) {
        auto& self=*static_cast<Memory*>(context);
        if (address==self.fail) return false;
        auto* data=static_cast<unsigned char*>(output);
        for (std::size_t i=0;i<size;++i) {
            const auto found=self.bytes.find(address+i);
            if (found==self.bytes.end()) return false;
            data[i]=found->second;
        }
        if (address==self.mutateAt) self.bytes[self.mutateTarget]^=1;
        return true;
    }
};

int main() {
    assert(CnBoxColor(false,false)==0xff0000ffu);
    assert(CnBoxColor(true,false)==0xff00ff00u);
    assert(CnBoxColor(false,true)==0xffff9600u);
    assert(CnBoxColor(true,true)==0xffff9600u);
    assert(CnLineColor(false,false)==0x960000ffu);
    assert(CnLineColor(true,false)==0x9600ff00u);
    assert(CnLineColor(false,true)==0x96ff9600u);
    constexpr std::uintptr_t actor=0x100000, mesh=0x200000, klass=0x300000;
    Memory m;
    m.put(actor+8,klass); m.put(actor+0x18,std::uint32_t(0));
    m.put(actor+0x1c,std::uint32_t(123)); m.put(actor+0x24,std::int32_t(456));
    m.put(actor+0x3d0,mesh);
    m.put(mesh+0x898,std::uint8_t(1)); m.put(mesh+0x8b8,std::uint8_t(0));
    OwnMemoryReader reader{&m,Memory::read};
    auto sample=CnReadOriginalMeshVisibility(reader,actor,1);
    assert(sample.source==CnVisibilitySource::OriginalRawMeshBit && sample.visible);
    assert(CnVisibilityBoundTo(sample,actor,mesh,1));
    auto invalidSource=sample;
    invalidSource.source=static_cast<CnVisibilitySource>(99);
    std::uint32_t invalidColor=0x12345678u;
    assert(!CnVisibilityBoundTo(invalidSource,actor,mesh,1));
    assert(!CnHeadSkeletonColor(invalidSource,false,actor,mesh,1,invalidColor));
    assert(invalidColor==0x12345678u);
    assert(!CnVisibilityBoundTo(sample,actor+8,mesh,1));
    assert(!CnVisibilityBoundTo(sample,actor,mesh+8,1));
    assert(!CnVisibilityBoundTo(sample,actor,mesh,2));
    std::uint32_t color=0x12345678u;
    assert(CnHeadSkeletonColor(sample,false,actor,mesh,1,color) && color==0xff00ff00u);
    assert(!CnHeadSkeletonColor(sample,false,actor,mesh,2,color) && color==0xff00ff00u);
    m.put(mesh+0x898,std::uint8_t(2)); m.put(mesh+0x8b8,std::uint8_t(1));
    sample=CnReadOriginalMeshVisibility(reader,actor,2);
    assert(sample.source==CnVisibilitySource::OriginalRawMeshBit && !sample.visible);
    assert(CnHeadSkeletonColor(sample,false,actor,mesh,2,color) && color==0xff0000ffu);
    assert(CnHeadSkeletonColor({},true,actor,mesh,2,color) && color==0xffffffffu);
    m.fail=mesh+0x898;
    sample=CnReadOriginalMeshVisibility(reader,actor,3);
    assert(sample.source==CnVisibilitySource::Unavailable && !sample.actor && !sample.frame);
    color=0x12345678u;
    assert(!CnHeadSkeletonColor(sample,false,actor,mesh,3,color) && color==0x12345678u);
    m.fail=0;
    for (auto changed:{actor+8,actor+0x1c,actor+0x24,actor+0x3d0}) {
        m.mutateAt=mesh+0x898; m.mutateTarget=changed;
        assert(CnReadOriginalMeshVisibility(reader,actor,4).source==CnVisibilitySource::Unavailable);
        m.bytes[changed]^=1;
    }
    m.mutateAt=0;
    m.put(actor+0x18,std::uint32_t(0x18000));
    assert(CnReadOriginalMeshVisibility(reader,actor,5).source==CnVisibilitySource::Unavailable);
    assert(CnReadOriginalMeshVisibility(reader,actor,0).source==CnVisibilitySource::Unavailable);
    assert(CnReadOriginalMeshVisibility(reader,actor+1,5).source==CnVisibilitySource::Unavailable);
    assert(CnReadOriginalMeshVisibility({},actor,5).source==CnVisibilitySource::Unavailable);
    std::cout<<"PASS: original mesh bit source, frame and identity binding, unknown failures, cn Head/Skeleton colours\n";
}
