#include "SJZCollector.h"
#include "SJZAim.h"
#include "TCIISkeletonLayout.h"
#include "sjzesp.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    uintptr_t fail=0, mutate=0;
    int actorReads=0;
    bool changing=false;
    int headerMutation=0;
    uintptr_t failOnce=0;
    uintptr_t flipParent=0, flipParentTo=0;
    std::map<uintptr_t,int> readCounts;
    void raw(uintptr_t p,const void* input,size_t size) {
        auto in=static_cast<const unsigned char*>(input);
        for(size_t i=0;i<size;++i) bytes[p+i]=in[i];
    }
    template<class T> void put(uintptr_t p,T value) { raw(p,&value,sizeof(value)); }
    void zero(uintptr_t p,size_t n) { for(size_t i=0;i<n;++i) bytes[p+i]=0; }
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& m=*static_cast<Memory*>(context);
        ++m.readCounts[address];
        if (address==m.fail) return false;
        if (address==m.failOnce) { m.failOnce=0; return false; }
        const bool secondHeader=address==m.mutate && ++m.actorReads==2;
        if (secondHeader && m.changing) return false;
        auto out=static_cast<unsigned char*>(output);
        for(size_t i=0;i<size;++i) {
            auto found=m.bytes.find(address+i);
            if(found==m.bytes.end()) return false;
            out[i]=found->second;
        }
        if (secondHeader && m.headerMutation && size==sizeof(OwnArrayHeader)) {
            OwnArrayHeader header{};
            std::memcpy(&header,output,sizeof(header));
            if (m.headerMutation==SJZ_ACTOR_RECHECK_DATA_CHANGED) header.data+=0x10000;
            if (m.headerMutation==SJZ_ACTOR_RECHECK_COUNT_CHANGED) ++header.count;
            std::memcpy(output,&header,sizeof(header));
        }
        if (address==m.flipParent) {
            m.put(address,m.flipParentTo);
            m.flipParent=0;
        }
        return true;
    }
};
constexpr uintptr_t base=0x100000000,world=0x200000000,driver=world+0x10000,
    connection=driver+0x10000,controller=connection+0x10000,level=controller+0x10000,
    pawn=level+0x10000,camera=pawn+0x10000,actor=camera+0x10000,
    list=actor+0x10000,teamData=list+0x10000,healthData=teamData+0x10000,
    ability=healthData+0x10000,component=ability+0x10000,state=component+0x10000,
    nameBlock=state+0x10000,klass=nameBlock+0x10000,mesh=klass+0x10000,
    bones=mesh+0x10000,pickup=bones+0x10000,pickupClass=pickup+0x10000,
    pickupComponent=pickupClass+0x10000,configData=pickupComponent+0x10000,
    weapon=configData+0x10000,board=weapon+0x10000;
Memory* activeMemory=nullptr;
bool transportReady=true,partial=false;
extern "C" bool sjz_transport_ready(void) { return transportReady; }
extern "C" uint64_t sjz_session_generation(void) { return 1; }
extern "C" uint64_t sjz_find_image_base(const uint8_t uuid[16],uint32_t type) {
    static const uint8_t mainUUID[16]={0xe6,0x4c,0x78,0x9b,0x63,0xad,0x34,0xc9,
                                       0xb2,0x2e,0xc2,0xbf,0xc3,0x69,0x3e,0x52};
    return type==2 && !std::memcmp(uuid,mainUUID,16) ? base:0;
}
extern "C" long sjz_read(uint64_t address,void* out,size_t size) {
    if (!Memory::read(activeMemory,address,out,size)) return -1;
    return static_cast<long>(partial ? size-1 : size);
}
extern "C" long sjz_read_fresh_root(uint64_t address,void* out,size_t size,uint64_t*) {
    return sjz_read(address,out,size);
}

void name(Memory& m,uintptr_t object,uint32_t index,const std::string& text) {
    m.put(object+0x1c,index);
    auto entry=nameBlock+size_t(index)*2;
    m.put(entry,uint16_t(text.size()<<6));
    for(size_t i=0;i<text.size();++i) m.put(entry+2+i,uint8_t(~text[i]));
}
void cloneObject(Memory& m,uintptr_t source,uintptr_t destination) {
    std::vector<std::pair<uintptr_t,unsigned char>> copy;
    for(auto it=m.bytes.lower_bound(source);it!=m.bytes.end() && it->first<source+0x2000;++it)
        copy.push_back(*it);
    for(const auto& [address,byte]:copy) m.bytes[destination+(address-source)]=byte;
}
Memory fixture() {
    Memory m;
    m.put(base+0x178b44e0,world); m.put(world+0x30,driver);
    m.put(driver+0x98,connection); m.put(connection+0x30,controller);
    m.put(world+0xf8,level); m.put(controller+0x3a0,pawn); m.put(controller+0x408,camera);
    m.put(base+0x173776c0+0xc8,nameBlock);
    struct CameraBlock { OwnVector3 p; float pad; OwnRotation r; float fov; } cam{{},0,{},90};
    m.put(camera+0x1890,cam);
    m.put(pawn+0x180,component+0x1000); m.put(component+0x1148,OwnVector3{});
    m.put(pawn+0x10c0,teamData+0x1000); m.put(teamData+0x1108,int32_t(1)); m.put(teamData+0x110c,int32_t(1));
    m.put(level+0x98,OwnArrayHeader{list,1,1}); m.put(list,actor);
    m.put(actor+8,klass); m.put(actor+0x18,uint32_t(0));
    m.put(actor+0x1c,uint32_t(101));m.put(actor+0x24,int32_t(100));
    m.put(klass+0x40,uintptr_t(0)); name(m,klass,100,"GPCharacterBase");
    m.put(actor+0x180,component); m.put(component+0x148,OwnVector3{1000,0,0});
    m.put(actor+0x10c0,teamData); m.put(teamData+0x108,int32_t(2)); m.put(teamData+0x10c,int32_t(1));
    m.put(actor+0x10b8,ability); m.put(ability+0x280,healthData);
    constexpr size_t fields[]={0x3c,0x54,0x114,0x124,0x74,0x8c,0x9c,0xb4};
    for(auto f:fields) m.put(healthData+f,100.f);
    m.put(actor+0x390,state); m.zero(actor+0xe5f,3);
    m.put(state+0x470,state+0x1000);
    m.zero(state+0x1000,28);
    const char16_t text[]=u"测试玩家"; m.raw(state+0x1000,text,sizeof(text));
    m.put(actor+0x3d0,mesh); m.put(mesh+0x750,bones);
    struct Transform { float q[4]; OwnVector3 p,s; } transform{{0,0,0,1},{1000,0,0},{1,1,1}};
    m.put(mesh+0x210,transform);
    for(auto index:kTCIIHumanDisplayMeshIndices) m.put(bones+size_t(index)*0x30+0x10,OwnVector3{0,0,float(index)});
    m.mutate=level+0x98;
    return m;
}

int main() {
    auto m=fixture(); activeMemory=&m;
    sjzesp_config_t config{SJZ_DEFAULT_FLAGS,300,3,1.5f,13,.5f,180.f,2,0};
    sjzesp_item_t output[4]{};
    auto collect=[&] { return sjzesp_tick(base,1000,500,&config,output,4); };
    assert(collect()==1);
    assert(sjzesp_stats().status==SJZ_STATUS_READY);
    assert(std::string(output[0].name)=="测试玩家");
    assert(std::abs(output[0].x-500)<.01f && std::abs(output[0].distance-10)<.01f);
    assert(output[0].boneMask==0x7fff && output[0].health==100);
    const auto firstSample=sjzesp_stats();
    assert(firstSample.stage==SJZ_STAGE_NONE && firstSample.sampleMask==
           (SJZ_SAMPLE_ROOTS|SJZ_SAMPLE_CAMERA|SJZ_SAMPLE_LOCAL|SJZ_SAMPLE_TARGET));
    assert(firstSample.viewportWidth==1000 && firstSample.viewportHeight==500);
    assert(firstSample.worldIdentity==world && firstSample.pawnIdentity==pawn);
    assert(firstSample.cameraFov==90 && firstSample.localX==0);
    assert(firstSample.firstTargetIdentity==actor && firstSample.targetWorldX==1000);
    assert(firstSample.targetScreenX==output[0].x &&
           std::abs(firstSample.targetScreenY-250)<.01f &&
           firstSample.targetScreenY>output[0].top && firstSample.targetDistance==10);
    assert(firstSample.actorHeaderStartData==list && firstSample.actorHeaderEndData==list &&
           firstSample.actorHeaderStartCount==1 && firstSample.actorHeaderEndCount==1 &&
           firstSample.scannedActors==1 && firstSample.candidatePlayers==1 &&
           firstSample.candidateLoot==0 && firstSample.actorRecheckReason==SJZ_ACTOR_RECHECK_NONE);
    m.put(base+0x178b44e0,uintptr_t(0));
    assert(collect()==0 && sjzesp_stats().stage==SJZ_STAGE_ROOTS &&
           sjzesp_stats().rootFailureEdge==SJZ_ROOT_EDGE_WORLD &&
           sjzesp_stats().rootSlotValue==0 && sjzesp_stats().readFailures==0);
    m.put(base+0x178b44e0,world);
    m.put(controller+0x3a0,uintptr_t(0));
    assert(collect()==0 && sjzesp_stats().stage==SJZ_STAGE_ROOTS &&
           sjzesp_stats().rootFailureEdge==SJZ_ROOT_EDGE_PAWN &&
           sjzesp_stats().rootSlotValue==world && sjzesp_stats().readFailures==0);
    m.put(controller+0x3a0,pawn);
    m.put(component+0x1148,OwnVector3{100,0,0});
    assert(collect()==1 && std::abs(sjzesp_stats().targetDistance-9)<.01f);
    assert(sjzesp_stats().localX==100 && sjzesp_stats().sampleGeneration>firstSample.sampleGeneration);
    m.put(component+0x1148,OwnVector3{});
    m.put(component+0x148,OwnVector3{1000,200,0});
    assert(collect()==1 && sjzesp_stats().targetWorldY==200);
    const float shiftedX=sjzesp_stats().targetScreenX;
    assert(shiftedX>500);
    m.put(camera+0x1890+0x1c,60.f);
    assert(collect()==1 && sjzesp_stats().cameraFov==60 && sjzesp_stats().targetScreenX>shiftedX);
    m.put(camera+0x1890+0x1c,90.f);
    m.put(component+0x148,OwnVector3{1000,0,0});
    config.flags=SJZ_SHOW_ESP|SJZ_SHOW_HEAD;
    assert(collect()==1 && (output[0].boneMask&0x3u)==0x3u);
    config.flags=SJZ_SHOW_HEAD;
    assert(collect()==0); // ESP master switch suppresses display-only collection.
    config.flags=SJZ_DEFAULT_FLAGS;
    m.put(pawn+0x1788,weapon);m.put(weapon+0x838,uint64_t(18010000006ULL));
    m.put(pawn+0x1020,board);m.zero(board+0x63b,0x1e);m.put(board+0x63c,uint8_t(1));
    m.put(component+0x190,OwnVector3{10,0,0});
    config.flags|=SJZ_AIM_ENABLED;
    assert(collect()==1 && output[0].velocityValid && output[0].objectIndex==100);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：仅 iOS 触摸后端可用");
    config.flags|=SJZ_AIM_VISIBLE_ONLY;
    assert(collect()==1);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：真实视线查询不可用");
    config.flags&=~SJZ_AIM_VISIBLE_ONLY;
    config.flags&=~SJZ_AIM_ENABLED;
    const char16_t boundedName[]=u"abcdefghijklmnGARBAGE";
    m.raw(state+0x1000,boundedName,sizeof(boundedName));
    assert(collect()==1 && std::string(output[0].name)=="abcdefghijklmn");
    m.zero(state+0x1000,28);
    const char16_t restoredName[]=u"测试玩家";
    m.raw(state+0x1000,restoredName,sizeof(restoredName));
    m.put(teamData+0x108,int32_t(1)); assert(collect()==0); // same team/squad excluded
    m.put(teamData+0x108,int32_t(2));
    config.maxDistance=5; assert(collect()==0); config.maxDistance=300;
    m.put(actor+0xe5f,uint8_t(1));
    assert(collect()==1 && output[0].bot && output[0].boneMask==0);
    const uintptr_t botClass=klass+0x5000;
    m.put(botClass+0x40,klass);
    name(m,botClass,400,"NC_BP_DFMCharacter_AI_DT_RPG_C");
    m.put(actor+8,botClass);
    assert(collect()==1 && std::string(output[0].name)=="Ashara RPG");
    m.put(actor+8,klass);
    config.flags&=~SJZ_SHOW_AI; assert(collect()==0); config.flags|=SJZ_SHOW_AI;
    m.put(actor+0xe5f,uint8_t(0));
    m.put(healthData+0x3c,0.f); assert(collect()==1 && output[0].knocked);
    m.put(healthData+0x114,0.f); assert(collect()==0);
    m.put(healthData+0x3c,100.f); m.put(healthData+0x114,100.f);
    m.fail=camera+0x1890;
    assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_CAMERA &&
           sjzesp_stats().stage==SJZ_STAGE_CAMERA &&
           sjzesp_stats().sampleMask==SJZ_SAMPLE_ROOTS);
    m.fail=0;
    m.changing=true; m.actorReads=0;
    assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_SCENE_CHANGED &&
           sjzesp_stats().stage==SJZ_STAGE_ACTOR_RECHECK &&
           sjzesp_stats().actorRecheckReason==SJZ_ACTOR_RECHECK_READ_FAILED &&
           sjzesp_stats().candidatePlayers==1 && sjzesp_stats().playerCount==0 &&
           !(sjzesp_stats().sampleMask&SJZ_SAMPLE_TARGET) &&
           sjzesp_stats().firstTargetIdentity==0 && sjzesp_stats().targetDistance==0);
    m.changing=false;
    m.actorReads=0; m.headerMutation=SJZ_ACTOR_RECHECK_DATA_CHANGED;
    assert(collect()==0 && sjzesp_stats().stage==SJZ_STAGE_ACTOR_RECHECK &&
           sjzesp_stats().actorRecheckReason==SJZ_ACTOR_RECHECK_DATA_CHANGED &&
           sjzesp_stats().actorHeaderStartData==list &&
           sjzesp_stats().actorHeaderEndData==list+0x10000);
    m.actorReads=0; m.headerMutation=SJZ_ACTOR_RECHECK_COUNT_CHANGED;
    assert(collect()==0 && sjzesp_stats().stage==SJZ_STAGE_ACTOR_RECHECK &&
           sjzesp_stats().actorRecheckReason==SJZ_ACTOR_RECHECK_COUNT_CHANGED &&
           sjzesp_stats().actorHeaderStartCount==1 && sjzesp_stats().actorHeaderEndCount==2);
    m.headerMutation=0;
    partial=true; assert(collect()==0 && sjzesp_stats().readFailures>0); partial=false;
    transportReady=false; assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_TRANSPORT); transportReady=true;
    config.flags=0; assert(collect()==0); config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT;
    config.maxDistance=std::numeric_limits<float>::quiet_NaN(); assert(collect()==0); config.maxDistance=300;
    m.put(level+0x98,OwnArrayHeader{list,2,2}); m.put(list+8,pickup);
    m.put(pickup+8,pickupClass); m.put(pickup+0x18,uint32_t(0));
    m.put(pickupClass+0x40,uintptr_t(0)); name(m,pickupClass,200,"PickupBase");
    m.put(pickup+0x180,pickupComponent); m.put(pickupComponent+0x148,OwnVector3{1200,20,0});
    m.put(pickup+0x1200,configData); m.put(configData+0xdc,int32_t(4));
    m.put(configData+0x18,configData+0x1000); m.put(configData+0x1018,configData+0x2000);
    m.put(configData+0x2028,configData+0x3000); m.put(configData+0x3010,configData+0x4000);
    m.zero(configData+0x4000,28);
    const char16_t loot[]=u"测试物资"; m.raw(configData+0x4000,loot,sizeof(loot));
    assert(collect()==2 && output[1].category==SJZ_CATEGORY_LOOT && output[1].level==4);
    assert(std::string(output[1].name)=="测试物资");
    config.lootLevel=5; assert(collect()==1); config.lootLevel=3;
    const uintptr_t deadClass=pickupClass+0x5000;
    m.put(deadClass+0x40,pickupClass);
    name(m,deadClass,300,"InventoryPickup_DeadBody");
    m.put(pickup+8,deadClass);
    config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_CONTAINER;
    assert(collect()==1); // Death Box requires the parent Loot switch.
    config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT|SJZ_SHOW_CONTAINER;
    assert(collect()==2 && output[1].category==SJZ_CATEGORY_CONTAINER);
    config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT;
    assert(collect()==1); // Death Box can be disabled independently.
    m.put(pickup+8,pickupClass);
    config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT;
    const uintptr_t player2=board+0x20000, pickup2=board+0x30000;
    const uintptr_t unrelated1=board+0x40000, unrelated2=board+0x50000;
    const uintptr_t unrelatedClass=board+0x60000;
    cloneObject(m,actor,player2); m.put(player2+0x24,int32_t(101));
    cloneObject(m,pickup,pickup2); m.put(pickup2+0x24,int32_t(201));
    cloneObject(m,actor,unrelated1); cloneObject(m,actor,unrelated2);
    m.put(unrelated1+0x24,int32_t(301)); m.put(unrelated2+0x24,int32_t(302));
    m.put(unrelated1+8,unrelatedClass); m.put(unrelated2+8,unrelatedClass);
    m.put(unrelatedClass+0x40,uintptr_t(0)); name(m,unrelatedClass,500,"UnrelatedActor");
    m.put(level+0x98,OwnArrayHeader{list,6,6});
    const uintptr_t repeatedActors[]={unrelated1,unrelated2,actor,player2,pickup,pickup2};
    m.raw(list,repeatedActors,sizeof(repeatedActors));
    m.readCounts.clear();
    assert(collect()==4);
    assert(sjzesp_stats().scannedActors==6 && sjzesp_stats().candidatePlayers==2 &&
           sjzesp_stats().candidateLoot==2 && sjzesp_stats().playerCount==2 &&
           sjzesp_stats().lootCount==2);
    assert(m.readCounts[nameBlock+100*2]==1 &&
           m.readCounts[nameBlock+200*2]==1 &&
           m.readCounts[nameBlock+500*2]==1); // One class-chain decode per class per frame.
    m.failOnce=nameBlock+100*2;
    assert(collect()==3 && sjzesp_stats().candidatePlayers==1 &&
           sjzesp_stats().candidateLoot==2); // Failed first lookup was not memoized.
    m.put(unrelatedClass+0x40,uintptr_t(0x1234)); // Nonzero invalid parent is incomplete.
    m.flipParent=unrelatedClass+0x40; m.flipParentTo=klass;
    m.readCounts.clear();
    assert(collect()==4 && output[0].identity==unrelated2 &&
           sjzesp_stats().candidatePlayers==3);
    assert(m.readCounts[nameBlock+500*2]==2); // Second actor must retry class resolution.
    m.put(unrelatedClass+0x40,uintptr_t(0));
    m.put(level+0x98,OwnArrayHeader{list,2,2}); m.put(list,actor); m.put(list+8,pickup);
    assert(sjzesp_tick(base,1000,500,&config,output,1)==1);
    m.put(level+0x98,OwnArrayHeader{0,0,0}); assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_READY);
    sjzesp_reset(); assert(sjzesp_stats().publishedCount==0);
    std::cout<<"PASS: real collector/transport adapter, projection, UTF-8, filtering, bones, loot, partial read, scene change, empty scene\n";
}
