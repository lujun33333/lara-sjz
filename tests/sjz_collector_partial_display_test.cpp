#define SJZ_LIVE_FIXTURE_ONLY
#include "sjz_collector_live_test.cpp"

struct SlowIdentityMemory {
    LiveMemory target;
    std::unordered_set<uintptr_t> actors;
    std::vector<uintptr_t> identities;
    double identityCost=0;
    uintptr_t blockedIdentity=0;
    int identitiesBeforeHot=0;
    bool hotSeen=false;
    static double clock(void* context) {
        return static_cast<SlowIdentityMemory*>(context)->target.now;
    }
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& self=*static_cast<SlowIdentityMemory*>(context);
        if(size==0x3d8-8 && self.actors.count(address-8)) self.hotSeen=true;
        if(size==32 && self.actors.count(address-8)) {
            self.identities.push_back(address-8);
            if(!self.hotSeen) ++self.identitiesBeforeHot;
            self.target.now+=address-8==self.blockedIdentity ? 160:self.identityCost;
        }
        return LiveMemory::read(&self.target,address,output,size);
    }
};

struct LateAssemblyMemory {
    LiveMemory target;
    int pawnReads=0,afterEndCalls=0;
    bool enabled=false,jumped=false;
    double assemblyCost=0;
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& self=*static_cast<LateAssemblyMemory*>(context);
        if(address==controller+0x3a0) ++self.pawnReads;
        return LiveMemory::read(&self.target,address,output,size);
    }
    static double clock(void* context) {
        auto& self=*static_cast<LateAssemblyMemory*>(context);
        // Four pawn pointer reads close the two root transactions. Their last
        // read's timing, over-age check and pre-assembly emission precede the
        // fourth clock observation, which models projection/assembly cost.
        if(self.enabled && self.pawnReads==4 && ++self.afterEndCalls==4) {
            self.target.now+=self.assemblyCost;self.jumped=true;
        }
        return self.target.now;
    }
    void reset() {target.reset();pawnReads=afterEndCalls=0;jumped=false;}
};

struct RecheckFailureMemory {
    LiveMemory target;
    bool failVerification=false;
    static double clock(void* context) {
        return static_cast<RecheckFailureMemory*>(context)->target.now;
    }
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& self=*static_cast<RecheckFailureMemory*>(context);
        const bool read=LiveMemory::read(&self.target,address,output,size);
        // Initial header, changed header, readMembers header, final verification.
        if(self.failVerification && address==level+0x98 && self.target.headers==4) return false;
        return read;
    }
};

int main() {
    LiveMemory target;
    constexpr uintptr_t a=0x370000000ULL,b=a+0x4000;
    for(auto object:{a,b}) {
        target.memory.zero(object,0x2000);
        for(const auto& byte:target.memory.bytes)
            if(byte.first>=actor && byte.first<actor+0x2000)
                target.memory.put(object+byte.first-actor,byte.second);
        target.memory.put(object+0x24,int32_t(object==a ? 100:101));
        target.costlyReads.insert(object+0xdc8);
    }
    target.memory.put(list,a);target.memory.put(list+8,b);
    target.memory.put(level+0x98,OwnArrayHeader{list,2,2});
    target.entityCost=70; // Each core samples two death words: one entity per slice.
    SJZCollector collector({&target,LiveMemory::read},base);
    collector.setLiveClock(LiveMemory::clock,&target);collector.setLiveEpoch(1);
    auto config=sjzesp_config_t{SJZ_DEFAULT_FLAGS,300,3,1.5f,13,.5f,180.f,0,0};
    sjzesp_item_t output[8]{};
    auto tick=[&] {target.reset();return collector.collectLive(config,1000,500,output,8);};
    assert(tick()==0);
    assert(tick()==1);
    const auto firstIdentity=output[0].identity,firstFrame=output[0].frameId;
    assert(tick()==2); // Sampling B cannot remove the still-fresh A.
    bool retained=false,fresh=false;
    double retainedAge=0;
    for(int i=0;i<2;++i) {
        assert(output[i].publicationFrame==collector.stats.sampleGeneration && output[i].sampleAgeMs<=450);
        if(output[i].identity==firstIdentity) {
            retained=true;retainedAge=output[i].sampleAgeMs;assert(output[i].frameId==firstFrame);
        } else {fresh=true;assert(output[i].frameId==collector.stats.sampleGeneration);}
    }
    assert(retained && fresh && retainedAge>100);
    assert(tick()==2 && tick()==2); // Alternating partial slices keep the union.

    // A cached pose cannot survive an observed identity change, even if budget
    // would otherwise defer its full sampling transaction.
    target.memory.put(a+0x24,int32_t(999));
    const int changed=tick();
    for(int i=0;i<changed;++i) assert(output[i].identity!=a);

    // Membership removal is a tombstone. B's sample cannot keep A alive.
    target.memory.put(list,b);target.memory.put(level+0x98,OwnArrayHeader{list,1,1});
    target.entityCost=0;
    assert(tick()==1 && output[0].identity==b);
    target.failed=b+8;assert(tick()==0);target.failed=0; // Real identity read failure removes B immediately.
    assert(tick()==0 && tick()==1); // Rediscover the failed identity before sampling again.
    collector.setLiveEpoch(2);assert(tick()==0); // No cache or index survives the epoch.
    assert(tick()==1);
    target.memory.put(base+0x178b44e0,uintptr_t(0));assert(tick()==0);
    assert(collector.liveMetrics().indexedCount==0);

    LiveMemory boundary;boundary.cost=boundary.nameCost=0;
    SJZCollector expiry({&boundary,LiveMemory::read},base);
    expiry.setLiveClock(LiveMemory::clock,&boundary);
    auto ageTick=[&] {boundary.reset();return expiry.collectLive(config,1000,500,output,8);};
    assert(ageTick()==0 && ageTick()==1);
    const auto originalFrame=output[0].frameId;
    // Cheap identity completes at 139ms. The position read advances to the
    // 150ms hot deadline; the next core read yields without any real failure.
    boundary.startupCost=139;boundary.extraRead=component+0x168;boundary.extraReadCost=11;
    float previousX=output[0].x;
    for(int i=1;i<=3;++i) {
        struct CameraBlock {OwnVector3 p;float pad;OwnRotation r;float fov;};
        boundary.memory.put(camera+0x1890,CameraBlock{{},0,{0,float(i*5),0},90});
        assert(ageTick()==1 && output[0].frameId==originalFrame && output[0].sampleAgeMs==150*i);
        assert(output[0].publicationFrame==expiry.stats.sampleGeneration);
        assert(output[0].worldAnchorX==1000 && output[0].x!=previousX);
        previousX=output[0].x; // Camera reprojection updates pixels, never pose time.
    }
    assert(output[0].sampleAgeMs==450); // Exact original deadline; it never renewed.
    boundary.now+=.001;
    assert(ageTick()==0);
    boundary.startupCost=0;boundary.extraReadCost=0;
    assert(ageTick()==1 && output[0].frameId==expiry.stats.sampleGeneration);
    boundary.memory.put(actor+0xdc8,uint32_t(1));assert(ageTick()==0); // Observed death is an immediate tombstone.
    boundary.memory.put(actor+0xdc8,uint32_t(0));assert(ageTick()==1);
    config.maxDistance=1;assert(ageTick()==0); // A changed display filter cannot keep an old cached pose.

    // Cache verification must neither monopolize the hot phase nor always
    // revisit the same unordered-map prefix when identity reads become slow.
    SlowIdentityMemory slow;
    constexpr uintptr_t players=0x380000000ULL;
    constexpr int playerCount=8;
    for(int i=0;i<playerCount;++i) {
        const uintptr_t object=players+uintptr_t(i)*0x4000;
        slow.target.memory.zero(object,0x2000);
        for(const auto& byte:slow.target.memory.bytes)
            if(byte.first>=actor && byte.first<actor+0x2000)
                slow.target.memory.put(object+byte.first-actor,byte.second);
        slow.target.memory.put(object+0x24,int32_t(100+i));
        slow.target.memory.put(list+size_t(i)*8,object);
        slow.actors.insert(object);
    }
    slow.target.memory.put(level+0x98,OwnArrayHeader{list,playerCount,playerCount});
    SJZCollector fair({&slow,SlowIdentityMemory::read},base);
    fair.setLiveClock(SlowIdentityMemory::clock,&slow);
    config.maxDistance=300;
    auto fairTick=[&] {
        slow.target.reset();slow.identities.clear();
        return fair.collectLive(config,1000,500,output,8);
    };
    assert(fairTick()==0);
    int warmed=0;
    for(int i=0;i<8 && warmed<playerCount;++i) warmed=fairTick();
    assert(warmed==playerCount); // Index/call slices complete before the slowdown.
    slow.identityCost=20;
    std::unordered_set<uintptr_t> verifiedFirst,freshEntities;
    for(int frame=0;frame<playerCount;++frame) {
        // Input-table shuffling must not reset either scheduling order.
        for(int i=0;i<playerCount;++i)
            slow.target.memory.put(list+size_t(i)*8,players+uintptr_t((i+frame*3)%playerCount)*0x4000);
        const int n=fairTick();
        assert(!slow.identities.empty());
        verifiedFirst.insert(slow.identities.front());
        int freshCount=0;
        for(int i=0;i<n;++i) {
            assert(output[i].publicationFrame==fair.stats.sampleGeneration && output[i].sampleAgeMs<=450);
            if(output[i].frameId==fair.stats.sampleGeneration) {
                ++freshCount;freshEntities.insert(output[i].identity);
            }
        }
        assert(freshCount>0 && fair.stats.status==SJZ_STATUS_READY);
        assert(fair.liveMetrics().elapsedMs<200);
    }
    assert(verifiedFirst.size()==playerCount && freshEntities.size()==playerCount);

    SlowIdentityMemory cheap;
    cheap.target.cost=cheap.target.nameCost=0;
    constexpr int cheapCount=41;
    for(int i=0;i<cheapCount;++i) {
        const uintptr_t object=players+uintptr_t(i)*0x4000;
        cheap.target.memory.zero(object,0x2000);
        for(const auto& byte:cheap.target.memory.bytes)
            if(byte.first>=actor && byte.first<actor+0x2000)
                cheap.target.memory.put(object+byte.first-actor,byte.second);
        cheap.target.memory.put(object+0x24,int32_t(100+i));
        cheap.target.memory.put(list+size_t(i)*8,object);
        cheap.actors.insert(object);
    }
    cheap.target.memory.put(level+0x98,OwnArrayHeader{list,cheapCount,cheapCount});
    SJZCollector cheapCollector({&cheap,SlowIdentityMemory::read},base);
    cheapCollector.setLiveClock(SlowIdentityMemory::clock,&cheap);
    sjzesp_item_t many[64]{};
    std::unordered_set<uintptr_t> cheapFresh;
    for(int frame=0;frame<80;++frame) {
        cheap.target.reset();cheap.identities.clear();cheap.hotSeen=false;cheap.identitiesBeforeHot=0;
        const int n=cheapCollector.collectLive(config,1000,500,many,64);
        int freshCount=0;
        for(int i=0;i<n;++i) if(many[i].frameId==cheapCollector.stats.sampleGeneration) {
            ++freshCount;cheapFresh.insert(many[i].identity);
        }
        // At most 32 cache reads plus one fresh candidate identity precede
        // the first hot prefix, even when wall-clock reads cost nothing.
        assert(cheap.identitiesBeforeHot<=33);
        if(frame>10) assert(freshCount>0);
    }
    assert(cheapFresh.size()==cheapCount);

    // One identity that always takes longer than a full hot budget must not
    // keep the remaining candidate from completing on subsequent slices.
    SlowIdentityMemory blocked;
    blocked.target.memory.zero(b,0x2000);
    for(const auto& byte:blocked.target.memory.bytes)
        if(byte.first>=actor && byte.first<actor+0x2000)
            blocked.target.memory.put(b+byte.first-actor,byte.second);
    blocked.target.memory.put(b+0x24,int32_t(101));
    blocked.target.memory.put(list,actor);blocked.target.memory.put(list+8,b);
    blocked.target.memory.put(level+0x98,OwnArrayHeader{list,2,2});
    blocked.actors={actor,b};blocked.blockedIdentity=actor;
    SJZCollector skipBlocked({&blocked,SlowIdentityMemory::read},base);
    skipBlocked.setLiveClock(SlowIdentityMemory::clock,&blocked);
    bool healthyFresh=false;
    for(int frame=0;frame<8;++frame) {
        blocked.target.reset();
        const int n=skipBlocked.collectLive(config,1000,500,output,8);
        for(int i=0;i<n;++i) {
            assert(output[i].identity!=actor);
            if(output[i].identity==b && output[i].frameId==skipBlocked.stats.sampleGeneration)
                healthyFresh=true;
        }
    }
    assert(healthyFresh);

    LateAssemblyMemory late;
    late.target.cost=late.target.nameCost=0;
    SJZCollector finalAge({&late,LateAssemblyMemory::read},base);
    finalAge.setLiveClock(LateAssemblyMemory::clock,&late);
    auto lateTick=[&] {late.reset();return finalAge.collectLive(config,1000,500,output,8);};
    assert(lateTick()==0 && lateTick()==1);
    late.target.startupCost=139;late.target.extraRead=component+0x168;late.target.extraReadCost=11;
    late.enabled=true;late.assemblyCost=10;
    assert(lateTick()==1 && late.jumped && output[0].sampleAgeMs==160);
    late.assemblyCost=301;
    assert(lateTick()==0 && late.jumped); // Final assembly pushes the retained sample past 450ms.
    assert(finalAge.stats.publishedCount==0 && finalAge.stats.playerCount==0 && finalAge.stats.lootCount==0);
    assert(finalAge.stats.firstTargetIdentity==0 && !(finalAge.stats.sampleMask&SJZ_SAMPLE_TARGET));

    RecheckFailureMemory recheck;
    recheck.target.memory.put(list+8,pickup);
    SJZCollector noRevival({&recheck,RecheckFailureMemory::read},base);
    noRevival.setLiveClock(RecheckFailureMemory::clock,&recheck);
    auto recheckTick=[&](int mutation) {
        recheck.target.reset(mutation);return noRevival.collectLive(config,1000,500,output,8);
    };
    assert(recheckTick(0)==0 && recheckTick(0)==1);
    recheck.failVerification=true;
    assert(recheckTick(1)==0 && recheck.target.headers==4);
    assert(noRevival.stats.status==SJZ_STATUS_ACTORS && noRevival.stats.stage==SJZ_STAGE_ACTOR_RECHECK);
    assert(noRevival.liveMetrics().indexedCount==0);
    recheck.failVerification=false;
    recheck.target.startupCost=139;recheck.target.extraRead=component+0x168;recheck.target.extraReadCost=11;
    assert(recheckTick(0)==0 && noRevival.stats.publishedCount==0);
    // A fresh identity check with deferred core sampling cannot revive the
    // cached pose discarded by the previous membership verification failure.
    std::cout<<"PASS: partial union, original pose/frame age, identity/member/failure tombstones, budget defer/expiry and epoch/root clear\n";
}
