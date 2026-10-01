// Reuse the existing simulated target layout; none of these reads target a device.
#define main cn_alignment_fixture_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "sjz_cn_alignment_collector_test.cpp"
#pragma GCC diagnostic pop
#undef main

struct BudgetReader {
    Memory& memory;
    size_t calls=0,nameReads=0,bulkHeaders=0;
    uintptr_t classEntry=0;
    bool rejectClass=false;
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& self=*static_cast<BudgetReader*>(context);
        ++self.calls;
        if(size==24 && address>=0x300000000ULL) ++self.bulkHeaders;
        if(address>=nameBlock && address<nameBlock+0x10000) ++self.nameReads;
        if(self.rejectClass && address==self.classEntry) return false;
        return Memory::read(&self.memory,address,output,size);
    }
    void reset() { calls=nameReads=bulkHeaders=0; }
};

int main() {
    auto m=fixture();
    constexpr uintptr_t genericClass=0x310000000ULL,first=0x300000000ULL;
    constexpr int count=128;
    m.put(genericClass+0x40,uintptr_t(0));
    name(m,genericClass,1000,"UnrelatedActorClass");
    name(m,first,1200,"InventoryPickup_Valid");
    name(m,first+0x4000,1100,"UnrelatedObject");
    for(int i=0;i<count;++i) {
        const uintptr_t object=first+uintptr_t(i)*0x4000;
        m.zero(object+8,24);
        m.put(object+8,genericClass);
        m.put(object+0x1c,uint32_t(i ? 1100:1200));
        m.put(list+size_t(i)*8,object);
    }
    m.put(first+0x180,pickupComponent);
    m.put(pickupComponent+0x168,OwnVector3{1000,0,0});
    m.put(first+0x1200,configData);
    m.put(configData+0x68,int32_t(6));
    m.put(configData+0xdc,int32_t(42));
    m.put(level+0x98,OwnArrayHeader{list,count,count});
    BudgetReader budget{m};
    SJZCollector collector({&budget,BudgetReader::read},base);
    sjzesp_config_t config{};config.flags=SJZ_SHOW_LOOT;config.maxDistance=300;config.lootLevel=6;
    sjzesp_item_t items[4]{};
    auto collect=[&] { return collector.collect(config,1000,500,items,4); };
    assert(collect()==1 && items[0].identity==first && items[0].price==42);
    assert(collector.stats.candidateLoot==count && budget.bulkHeaders==count);
    const auto coldNames=budget.nameReads;
    assert(coldNames>0 && budget.calls<220);
    budget.reset();
    assert(collect()==1);
    assert(budget.bulkHeaders==33 && budget.nameReads==0 && budget.calls<85);
    assert(collector.stats.classCacheHits==33);
    std::cout<<"PASS: 128 actor warm budget="<<budget.calls<<", FName reads="<<budget.nameReads<<"\n";

    // A changed actor FName index must use the new immutable entry immediately.
    name(m,first,1300,"UnrelatedReplacement");budget.reset();
    assert(collect()==0 && budget.nameReads>0);

    // Reusing a negative actor address as a valid pickup must be discovered
    // within ceil(128/32) fresh frames, without publishing any old entity.
    const uintptr_t reused=first+127*0x4000;
    m.put(reused+0x1c,uint32_t(1200));
    m.put(reused+0x180,pickupComponent);m.put(reused+0x1200,configData);
    bool discovered=false;
    for(int i=0;i<4;++i) {
        // Deliberately reorder the actor list. Probe order belongs to the cold
        // cache rather than the changing actor-array index.
        for(int j=0;j<count;++j)
            m.put(list+size_t(j)*8,first+uintptr_t((j+i*29)%count)*0x4000);
        const int n=collect();
        if(n) { assert(n==1 && items[0].identity==reused);discovered=true;break; }
    }
    assert(discovered);
    assert(collect()==1 && items[0].identity==reused);
    // A valid entity is read and removed immediately when its FName changes.
    m.put(reused+0x1c,uint32_t(1300));assert(collect()==0);

    config.flags|=SJZ_SHOW_ESP;budget.reset();assert(collect()==0);
    assert(budget.bulkHeaders==count); // Enabling a feature invalidates negatives.
    config.flags=SJZ_SHOW_LOOT;budget.reset();assert(collect()==0);
    assert(budget.bulkHeaders==count);
    const uintptr_t nextList=list+0x3000;
    for(int i=0;i<count;++i)
        m.put(nextList+size_t(i)*8,first+uintptr_t(i)*0x4000);
    m.put(level+0x98,OwnArrayHeader{nextList,count,count});
    budget.reset();assert(collect()==0 && budget.bulkHeaders==count);

    // Switching world drops successful name and class caches, even if actor
    // pointers happen to be the same in the new scene.
    const uintptr_t nextWorld=world+0x800000;
    m.put(nextWorld+0x30,driver);m.put(nextWorld+0xf8,level);
    m.put(base+0x178b44e0,nextWorld);
    budget.reset();assert(collect()==0 && budget.nameReads>0);

    // Explicit invalidation is also the session reset boundary for this reader.
    collector.invalidateClassificationCache();
    budget.classEntry=nameBlock+1000*2;budget.rejectClass=true;
    budget.reset();assert(collect()==0);
    assert(collector.stats.classCacheMisses==count);
    budget.rejectClass=false;budget.reset();assert(collect()==0);
    assert(collector.stats.classCacheMisses==count && budget.nameReads>0);
    budget.reset();assert(collect()==0 && collector.stats.classCacheHits==32);

    // Preserve the end-of-collection actor array failure guard.
    m.changing=true;m.actorReads=0;assert(collect()==0);
    assert(collector.stats.status==SJZ_STATUS_SCENE_CHANGED);
    std::cout<<"PASS: name/address reuse, flags/list/world/session invalidation, failed decode retry and actor guard\n";
}
