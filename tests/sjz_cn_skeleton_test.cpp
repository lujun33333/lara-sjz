#include "sjz/CnSkeletonLayout.h"
#include "sjz/OwnGameData.h"
#include "sjz/SJZTypes.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>

struct Fixture {
    std::map<uintptr_t,unsigned char> bytes;
    uintptr_t fail=0, changeSlot=0, replacement=0;
    int changeReads=0;
    template<class T> void put(uintptr_t address,const T& value) {
        const auto* input=reinterpret_cast<const unsigned char*>(&value);
        for (size_t i=0;i<sizeof(value);++i) bytes[address+i]=input[i];
    }
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& fixture=*static_cast<Fixture*>(context);
        if(address==fixture.fail) return false;
        if(address==fixture.changeSlot && ++fixture.changeReads==2)
            fixture.put(fixture.changeSlot,fixture.replacement);
        auto* target=static_cast<unsigned char*>(output);
        for(size_t i=0;i<size;++i) {
            auto found=fixture.bytes.find(address+i);
            if(found==fixture.bytes.end()) return false;
            target[i]=found->second;
        }
        return true;
    }
};

int main() {
    constexpr uintptr_t actor=0x200000000,mesh=actor+0x10000,bones=mesh+0x10000;
    Fixture fixture;
    fixture.put(actor+0x3d0,mesh);
    fixture.put(mesh+0xa18,bones);
    struct Transform { std::array<float,4> rotation; OwnVector3 translation,scale; };
    const Transform identity{{0,0,0,1},{100,200,300},{1,1,1}};
    fixture.put(mesh+0x210,identity);
    // Use independent cn indices rather than reproducing the implementation loop.
    const int recoveredIndices[]={31,30,5,3,2,1,6,7,8,34,35,36,58,59,60,62,63,64};
    for (int index:recoveredIndices)
        fixture.put(bones+index*0x30+0x10,OwnVector3{float(index),float(index+1),float(index+2)});
    OwnMemoryReader reader{&fixture,Fixture::read};
    OwnWorldSkeleton result;
    assert(OwnReadCnSkeleton(reader,actor,result));
    assert(result.points.size()==18 && SJZ_BONE_POINTS==18 && result.mesh==mesh);
    for (size_t i=0;i<18;++i) {
        assert(result.valid[i]);
        assert(result.points[i].x==recoveredIndices[i]+100);
        assert(result.points[i].y==recoveredIndices[i]+201);
        assert(result.points[i].z==recoveredIndices[i]+302);
    }
    // Former 16-bit publication must not discard right-leg knee and foot.
    sjzesp_item_t item{};
    item.boneMask=(1u<<16)|(1u<<17);
    assert(item.boneMask==(1u<<16)+(1u<<17));
    assert(kCnHumanMeshIndices[kCnHeadPoint]==31);
    assert(kCnHumanMeshIndices[kCnNeckPoint]==30);
    assert(kCnHumanMeshIndices[kCnLeftFootPoint]==60);
    assert(kCnHumanMeshIndices[kCnRightFootPoint]==64);
    // Validate edges against recovered mesh pairs, including torso and both feet.
    const int expectedPairs[][2]={{31,30},{30,1},{30,6},{6,7},{7,8},{30,34},{34,35},
        {35,36},{1,58},{58,59},{59,60},{1,62},{62,63},{63,64}};
    for (size_t i=0;i<14;++i) {
        assert(kCnHumanMeshIndices[kCnDisplayBoneEdges[i][0]]==expectedPairs[i][0]);
        assert(kCnHumanMeshIndices[kCnDisplayBoneEdges[i][1]]==expectedPairs[i][1]);
    }
    fixture.fail=bones+5*0x30+0x10; // newly restored spine sample is required.
    assert(!OwnReadCnSkeleton(reader,actor,result));
    for(bool valid:result.valid) assert(!valid);
    fixture.fail=0;
    constexpr uintptr_t fallback=bones+0x10000;
    for(int index:recoveredIndices)
        fixture.put(fallback+index*0x30+0x10,OwnVector3{float(index+400),float(index+401),float(index+402)});
    fixture.put(mesh+0x730,fallback);
    fixture.put(mesh+0x750,uintptr_t(0xdeadbeef)); // old offset must not be consulted.
    assert(OwnReadCnSkeleton(reader,actor,result));
    assert(result.points[0].x==131); // primary wins over a different valid fallback.
    fixture.put(mesh+0xa18,uintptr_t(0));
    assert(OwnReadCnSkeleton(reader,actor,result));
    assert(result.points[0].x==531);
    fixture.put(mesh+0xa18,uintptr_t(0x123)); // invalid primary also selects fallback.
    assert(OwnReadCnSkeleton(reader,actor,result));
    fixture.put(mesh+0x730,uintptr_t(0x123));
    assert(!OwnReadCnSkeleton(reader,actor,result));
    for(bool valid:result.valid) assert(!valid);
    fixture.put(mesh+0xa18,bones);
    fixture.put(mesh+0x730,fallback);
    fixture.changeSlot=mesh+0xa18;fixture.replacement=fallback;fixture.changeReads=0;
    assert(!OwnReadCnSkeleton(reader,actor,result)); // array changes during sampling.
    for(bool valid:result.valid) assert(!valid);
    fixture.put(mesh+0xa18,bones);
    fixture.changeSlot=actor+0x3d0;fixture.replacement=mesh+0x40000;fixture.changeReads=0;
    assert(!OwnReadCnSkeleton(reader,actor,result)); // mesh changes during sampling.
    for(bool valid:result.valid) assert(!valid);
    fixture.put(actor+0x3d0,mesh);
    fixture.changeSlot=0;
    // cn compares the transformed slot 0/5 points in single precision.
    fixture.put(bones+1*0x30+0x10,OwnVector3{0,0,0});
    fixture.put(bones+31*0x30+0x10,OwnVector3{499,0,0});
    assert(OwnReadCnSkeleton(reader,actor,result));
    fixture.put(bones+31*0x30+0x10,OwnVector3{500,0,0});
    assert(!OwnReadCnSkeleton(reader,actor,result)); // squared distance equals 250000.
    for(bool valid:result.valid) assert(!valid);
    fixture.put(bones+31*0x30+0x10,OwnVector3{-100,-200,-300});
    assert(!OwnReadCnSkeleton(reader,actor,result)); // world head equals zero vector.
    fixture.put(bones+31*0x30+0x10,OwnVector3{31,32,33});
    fixture.put(bones+1*0x30+0x10,OwnVector3{-100,-200,-300});
    assert(!OwnReadCnSkeleton(reader,actor,result)); // world pelvis equals zero vector.
    fixture.put(bones+1*0x30+0x10,OwnVector3{9999899,0,0});
    fixture.put(bones+31*0x30+0x10,OwnVector3{9999900,0,0});
    assert(!OwnReadCnSkeleton(reader,actor,result)); // X equals 10000000; endpoint gap is only 1.
    fixture.put(bones+31*0x30+0x10,OwnVector3{9999898,0,0});
    assert(OwnReadCnSkeleton(reader,actor,result)); // both X values strictly within the bound.
    fixture.put(bones+31*0x30+0x10,OwnVector3{std::numeric_limits<float>::quiet_NaN(),0,0});
    assert(!OwnReadCnSkeleton(reader,actor,result));
    for(bool valid:result.valid) assert(!valid);
    fixture.put(bones+31*0x30+0x10,OwnVector3{31,32,33});
    fixture.put(bones+1*0x30+0x10,OwnVector3{1,2,3});
    assert(OwnReadCnSkeleton(reader,actor,result)); // unchanged original valid fixture.
    // A rolled camera makes head X differ from the old head/feet midpoint.
    // Expected cn +88/-88 projection at 90-degree FOV, not a mirrored loop.
    OwnCamera camera;
    assert(OwnBuildCamera({0,0,0},{0,0,45},90,{1000,500},camera));
    OwnProjectedBox box;
    assert(OwnProjectBox(camera,{1000,0,0},box));
    // Positive roll gives right-axis Z=-sin(45 degrees) in this convention.
    assert(std::fabs(box.centerX-468.8873f)<.02f);
    assert(std::fabs(box.top-218.8873f)<.02f);
    assert(std::fabs(box.height-62.2254f)<.03f);
    assert(OwnBuildCamera({0,0,0},{0,0,0},90,{1000,500},camera));
    ImVec2 screen;
    // cn uses depth=1 for all nearer points, including the rear half-space.
    for (float depth:{.5f,0.f,-1.f}) {
        assert(OwnProjectWorld(camera,{depth,.25f,.25f},screen));
        assert(std::fabs(screen.x-625.f)<.01f && std::fabs(screen.y-125.f)<.01f);
    }
    assert(OwnProjectWorld(camera,{2.f,.25f,.25f},screen));
    assert(std::fabs(screen.x-562.5f)<.01f && std::fabs(screen.y-187.5f)<.01f);
    std::cout<<"PASS: cn 18-point acquisition, transform, recovered edges and full mask\n";
}
