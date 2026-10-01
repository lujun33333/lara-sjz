#define main cn_alignment_fixture_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "sjz_cn_alignment_collector_test.cpp"
#pragma GCC diagnostic pop
#undef main

struct LiveMemory {
    Memory memory=fixture();
    double now=0,cost=.02,nameCost=.02;
    int headers=0,mutation=0;
    int prefixes=0;
    bool changeMesh=false;
    uintptr_t failed=0;
    size_t calls=0;
    std::unordered_set<uintptr_t> costlyReads;
    double entityCost=0;
    double startupCost=0;
    int rootReads=0;
    static double clock(void* context) {return static_cast<LiveMemory*>(context)->now;}
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& self=*static_cast<LiveMemory*>(context);++self.calls;
        self.now+=address>=nameBlock && address<nameBlock+0x10000 ? self.nameCost:self.cost;
        if(self.costlyReads.count(address)) self.now+=self.entityCost;
        if(address==base+0x178b44e0 && ++self.rootReads<=2) self.now+=self.startupCost/2;
        if(address==actor+8 && size==0x3d8-8 && ++self.prefixes==2 && self.changeMesh)
            self.memory.put(actor+0x3d0,mesh+0x5000);
        if(address==level+0x98 && ++self.headers==2) {
            if(self.mutation==1) self.memory.put(level+0x98,OwnArrayHeader{list,2,2});
            if(self.mutation==2) {
                self.memory.put(list,actor+0x70000);
                self.memory.put(level+0x98,OwnArrayHeader{list,1,1});
            }
            if(self.mutation==3) {
                self.memory.put(list+0x2000,actor);
                self.memory.put(level+0x98,OwnArrayHeader{list+0x2000,1,1});
            }
            if(self.mutation==4) self.memory.put(base+0x178b44e0,world+0x800000);
        }
        if(address==self.failed) return false;
        return Memory::read(&self.memory,address,output,size);
    }
    void reset(int change=0) {headers=0;prefixes=0;rootReads=0;mutation=change;calls=0;}
};

int main() {
    LiveMemory target;
    SJZCollector collector({&target,LiveMemory::read},base);
    collector.setLiveClock(LiveMemory::clock,&target);
    auto config=sjzesp_config_t{SJZ_DEFAULT_FLAGS,300,3,1.5f,13,.5f,180.f,0,0};
    sjzesp_item_t output[16]{};
    auto tick=[&] {return collector.collectLive(config,1000,500,output,16);};
    target.reset();assert(tick()==0); // Index discovery does not publish a saved pose.
    target.reset();assert(tick()==1 && output[0].frameId==collector.stats.sampleGeneration);
    const auto firstFrame=output[0].frameId;
    target.memory.put(component+0x168,OwnVector3{2000,0,0});
    target.reset();assert(tick()==1 && output[0].distance==20 && output[0].frameId>firstFrame);
    target.memory.put(bones+7*0x30+0x10,OwnVector3{-1500,1.e37f,1.e37f});
    target.reset();assert(tick()==1);
    assert(output[0].worldBoneMask==((1u<<18)-1) && !(output[0].boneMask&(1u<<7)) &&
           output[0].worldBoneX[7]<0); // Drawing projection does not erase valid world points.
    target.memory.put(bones+7*0x30+0x10,OwnVector3{0,0,7});
    target.changeMesh=true;target.reset();assert(tick()==1 && output[0].boneMask==0 && output[0].worldBoneMask==0);
    OwnCamera currentCamera;OwnProjectedBox positionBox;
    const OwnMemoryReader direct{&target.memory,Memory::read};
    assert(OwnReadCamera(direct,camera,{1000,500},currentCamera));
    assert(OwnProjectBox(currentCamera,{2000,0,0},positionBox));
    assert(output[0].x==positionBox.centerX && output[0].top==positionBox.top &&
           output[0].bottom==positionBox.top+positionBox.height && output[0].width==positionBox.width);
    target.changeMesh=false;target.memory.put(actor+0x3d0,mesh);
    for(auto bone:kCnHumanMeshIndices)
        target.memory.put(bones+size_t(bone)*0x30+0x10,OwnVector3{-1500,0,float(bone)});
    target.failed=mesh+0x898;target.changeMesh=true;target.reset();
    assert(tick()==1 && output[0].boneMask==0 && output[0].worldBoneMask==0);
    target.failed=0;target.changeMesh=false;target.memory.put(actor+0x3d0,mesh);
    for(auto bone:kCnHumanMeshIndices)
        target.memory.put(bones+size_t(bone)*0x30+0x10,OwnVector3{0,0,float(bone)});

    // Appending an unrelated actor during sampling retains the current player.
    const uintptr_t other=actor+0x70000,otherClass=klass+0x70000;
    target.memory.put(other+8,otherClass);target.memory.put(other+0x18,uint32_t(0));
    target.memory.put(other+0x24,int32_t(200));
    target.memory.put(otherClass+0x40,uintptr_t(0));
    name(target.memory,otherClass,1000,"UnrelatedClass");name(target.memory,other,1100,"UnrelatedActor");
    target.memory.put(list+8,other);
    target.reset(1);assert(tick()==1 && collector.stats.status==SJZ_STATUS_READY);
    target.reset();assert(tick()==1);
    target.reset(3);assert(tick()==1 && output[0].identity==actor); // Array reallocation alone is not a scene change.
    target.reset(2);assert(tick()==0 && collector.stats.status==SJZ_STATUS_READY); // Removed player is culled.
    target.memory.put(level+0x98,OwnArrayHeader{list,1,1});target.memory.put(list,actor);
    for(int i=0;i<3;++i) {target.reset();tick();}
    target.reset();assert(tick()==1);

    // Reusing a known positive address changes the identity and forces rediscovery.
    target.memory.put(actor+0x24,int32_t(101));
    target.reset();assert(tick()==0);
    for(int i=0;i<3;++i) {target.reset();tick();}
    target.reset();assert(tick()==1 && output[0].objectIndex==101);
    target.failed=mesh+0xa18;target.memory.put(mesh+0x730,uintptr_t(0));
    target.reset();assert(tick()==1 && output[0].boneMask==0); // Missing bone pages preserve the fresh core/box.
    target.failed=0;
    target.reset(4);assert(tick()==0 && collector.stats.status==SJZ_STATUS_SCENE_CHANGED);
    assert(collector.liveMetrics().indexedCount==0);

    // Incremental class traversal survives a wall budget yield at arbitrary name
    // stages. Readable bytes previously obtained never substitute for pose data.
    LiveMemory slow;
    constexpr uintptr_t chain=0x350000000ULL;
    slow.memory.put(actor+8,chain);
    for(int i=0;i<12;++i) {
        const uintptr_t current=chain+uintptr_t(i)*0x4000;
        name(slow.memory,current,2000+uint32_t(i)*128,i==11 ? "GPCharacterBase":"LongParent"+std::to_string(i));
        slow.memory.put(current+0x40,i==11 ? uintptr_t(0):current+0x4000);
    }
    slow.nameCost=6;
    SJZCollector gradual({&slow,LiveMemory::read},base);
    gradual.setLiveClock(LiveMemory::clock,&slow);
    bool found=false,yielded=false;
    for(int i=0;i<30;++i) {
        slow.reset();const int n=gradual.collectLive(config,1000,500,output,16);
        assert(gradual.liveMetrics().indexCalls<=48 && gradual.liveMetrics().indexActors<=8);
        yielded|=gradual.liveMetrics().budgetYields>0;
        if(n) {found=true;assert(output[0].identityClass==chain);break;}
    }
    assert(found && yielded);
    // A missing class-name page stays retryable, without caching a false negative.
    LiveMemory retry;
    retry.failed=nameBlock+100*2;
    SJZCollector recovering({&retry,LiveMemory::read},base);
    recovering.setLiveClock(LiveMemory::clock,&retry);
    retry.reset();assert(recovering.collectLive(config,1000,500,output,16)==0);
    retry.failed=0;
    for(int i=0;i<3;++i) {retry.reset();recovering.collectLive(config,1000,500,output,16);}
    retry.reset();assert(recovering.collectLive(config,1000,500,output,16)==1);

    LiveMemory blocked;blocked.cost=25;
    SJZCollector bounded({&blocked,LiveMemory::read},base);
    bounded.setLiveClock(LiveMemory::clock,&blocked);
    blocked.reset();assert(bounded.collectLive(config,1000,500,output,16)==0);
    assert(bounded.liveMetrics().singleReadMaxMs>=25 && bounded.liveMetrics().elapsedMs>200);
    assert(bounded.stats.status==SJZ_STATUS_BUDGET_YIELD && bounded.stats.sampleGeneration>0 &&
           bounded.liveMetrics().budgetOvershootMs>0 && bounded.stats.publishedCount==0);

    LiveMemory fair;
    constexpr uintptr_t players=0x370000000ULL;
    constexpr int playerCount=10;
    for(int i=0;i<playerCount;++i) {
        const uintptr_t object=players+uintptr_t(i)*0x4000;
        // Confirmed players have a contiguous object prefix in the real target.
        // Fill it here, while the earlier sparse fixture exercises fallback.
        fair.memory.zero(object,0x2000);
        for(const auto& byte:fair.memory.bytes)
            if(byte.first>=actor && byte.first<actor+0x2000)
                fair.memory.put(object+byte.first-actor,byte.second);
        fair.memory.put(object+0x24,int32_t(100+i));
        fair.memory.put(list+size_t(i)*8,object);
        fair.costlyReads.insert(object+0xdc8);
    }
    fair.memory.put(level+0x98,OwnArrayHeader{list,playerCount,playerCount});
    fair.entityCost=40;
    SJZCollector rotating({&fair,LiveMemory::read},base);
    rotating.setLiveClock(LiveMemory::clock,&fair);
    std::unordered_set<uintptr_t> seen;
    for(int frame=0;frame<15;++frame) {
        // The same membership is shuffled every frame. Hot order remains stable.
        for(int i=0;i<playerCount;++i)
            fair.memory.put(list+size_t(i)*8,players+uintptr_t((i+frame*3)%playerCount)*0x4000);
        fair.reset();const int n=rotating.collectLive(config,1000,500,output,16);
        for(int i=0;i<n;++i) {
            assert(output[i].frameId==rotating.stats.sampleGeneration);
            seen.insert(output[i].identity);
        }
        assert(rotating.liveMetrics().elapsedMs<200);
    }
    assert(seen.size()==playerCount);
    LiveMemory recovered;
    recovered.costlyReads.insert(actor+0xdc8);recovered.entityCost=90;
    SJZCollector adaptation({&recovered,LiveMemory::read},base);
    adaptation.setLiveClock(LiveMemory::clock,&recovered);
    recovered.reset();assert(adaptation.collectLive(config,1000,500,output,16)==0);
    recovered.reset();adaptation.collectLive(config,1000,500,output,16); // Push hot estimate to its cap.
    recovered.entityCost=0;recovered.startupCost=50;
    recovered.reset();assert(adaptation.collectLive(config,1000,500,output,16)==1);
    assert(adaptation.liveMetrics().elapsedMs<200); // No cache reset is needed to recover.
    std::cout<<"PASS: live hot/index split, fresh pose, membership changes, root/identity invalidation, budget resume and missing-page retry\n";
}
