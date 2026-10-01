#include "SJZCollector.h"
#include "SJZAim.h"
#include "CnSkeletonLayout.h"
#include "sjzesp.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <string>

struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    std::map<std::pair<uint32_t,std::string>,uint32_t> nameIndexes;
    std::map<uint32_t,std::string> usedNameIndexes;
    uint32_t nextNameIndex=4096;
    uintptr_t fail=0, mutate=0, deathFlip=0;
    int actorReads=0,deathReads=0;
    bool changing=false;
    void raw(uintptr_t p,const void* input,size_t size) {
        auto in=static_cast<const unsigned char*>(input);
        for(size_t i=0;i<size;++i) bytes[p+i]=in[i];
    }
    template<class T> void put(uintptr_t p,T value) { raw(p,&value,sizeof(value)); }
    void zero(uintptr_t p,size_t n) { for(size_t i=0;i<n;++i) bytes[p+i]=0; }
    static bool read(void* context,uintptr_t address,void* output,size_t size) {
        auto& m=*static_cast<Memory*>(context);
        if (address==m.fail) return false;
        if (address==m.mutate && m.changing && ++m.actorReads==2) return false;
        if (address==m.deathFlip && ++m.deathReads==2) m.put(address,uint32_t(1));
        auto out=static_cast<unsigned char*>(output);
        for(size_t i=0;i<size;++i) {
            auto found=m.bytes.find(address+i);
            if(found==m.bytes.end()) return false;
            out[i]=found->second;
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
bool transportWrite=false;
bool failWrite=false;
int writeCalls=0;
extern "C" bool sjz_transport_ready(void) { return transportReady; }
extern "C" bool sjz_transport_can_write(void) { return transportWrite; }
extern "C" void sjz_invalidate_read_cache(void) {}
extern "C" long sjz_write(uint64_t address,const void* input,size_t size) {
    ++writeCalls;
    if (!transportWrite || failWrite) return -1;
    activeMemory->raw(address,input,size);
    return static_cast<long>(size);
}
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
    const auto key=std::make_pair(index,text);
    auto found=m.nameIndexes.find(key);
    if(found!=m.nameIndexes.end()) index=found->second;
    else {
        if(m.usedNameIndexes.count(index)) { index=m.nextNameIndex; m.nextNameIndex+=128; }
        m.nameIndexes[key]=index; m.usedNameIndexes[index]=text;
    }
    m.put(object+0x1c,index);
    auto entry=nameBlock+size_t(index)*2;
    m.put(entry,uint16_t(text.size()<<6));
    for(size_t i=0;i<text.size();++i) m.put(entry+2+i,uint8_t(~text[i]));
}
Memory fixture() {
    Memory m;
    m.put(base+0x178b44e0,world); m.put(world+0x30,driver);
    m.put(driver+0x98,connection); m.put(connection+0x30,controller);
    m.put(world+0xf8,level); m.put(controller+0x3a0,pawn); m.put(controller+0x408,camera);
    m.put(controller+0x3d8,OwnRotation{});
    m.put(base+0x173776c0+0xc8,nameBlock);
    struct CameraBlock { OwnVector3 p; float pad; OwnRotation r; float fov; } cam{{},0,{},90};
    m.put(camera+0x1890,cam);
    m.put(pawn+0x180,component+0x1000); m.put(component+0x1168,OwnVector3{});
    m.put(component+0x1148,OwnVector3{-7000,-8000,-9000}); // Retired offset poison.
    m.put(pawn+0x10c0,teamData+0x1000); m.put(teamData+0x1108,int32_t(1)); m.put(teamData+0x110c,int32_t(1));
    m.put(level+0x98,OwnArrayHeader{list,1,1}); m.put(list,actor);
    m.put(actor+8,klass); m.put(actor+0x18,uint32_t(0));
    m.put(actor+0x1c,uint32_t(101));m.put(actor+0x24,int32_t(100));
    m.put(actor+0xdc8,uint32_t(0));
    m.put(klass+0x40,uintptr_t(0)); name(m,klass,100,"GPCharacterBase");
    m.put(actor+0x180,component); m.put(component+0x168,OwnVector3{1000,0,0});
    m.put(component+0x148,OwnVector3{-7000,-8000,-9000});
    m.put(actor+0x10c0,teamData); m.put(teamData+0x108,int32_t(2)); m.put(teamData+0x10c,int32_t(1));
    m.put(actor+0x10b8,ability); m.put(ability+0x280,healthData);
    constexpr size_t fields[]={0x3c,0x54,0x114,0x124,0x74,0x8c,0x9c,0xb4};
    for(auto f:fields) m.put(healthData+f,100.f);
    m.put(actor+0x390,state); m.put(state+0x39e,uint8_t(1));
    m.put(state+0x470,state+0x1000);
    m.zero(state+0x1000,28);
    const char16_t text[]=u"测试玩家"; m.raw(state+0x1000,text,sizeof(text));
    m.put(state+0x478,int32_t(sizeof(text)/sizeof(char16_t)));
    m.put(actor+0x3d0,mesh); m.put(mesh+0xa18,bones);
    m.put(mesh+0x898,uint8_t(1));m.put(mesh+0x8b8,uint8_t(0));
    struct Transform { float q[4]; OwnVector3 p,s; } transform{{0,0,0,1},{1000,0,0},{1,1,1}};
    m.put(mesh+0x210,transform);
    for(auto index:kCnHumanMeshIndices) m.put(bones+size_t(index)*0x30+0x10,OwnVector3{0,0,float(index)});
    m.mutate=level+0x98;
    return m;
}

int main() {
    auto m=fixture(); activeMemory=&m;
    sjzesp_config_t config{SJZ_DEFAULT_FLAGS,300,3,1.5f,13,.5f,180.f,2,0};
    sjzesp_item_t output[4]{};
    uint64_t previousFrame=0;
    auto collect=[&] {
        const int count=sjzesp_tick(base,1000,500,&config,output,4);
        if(count>0) {
            assert(output[0].frameId>previousFrame);
            previousFrame=output[0].frameId;
            for(int i=1;i<count;++i) assert(output[i].frameId==output[0].frameId);
        }
        return count;
    };
    assert(collect()==1);
    assert(sjzesp_stats().status==SJZ_STATUS_READY);
    m.put(actor+0xdc8,uint32_t(1));assert(collect()==0); // Health is still 100.
    m.put(actor+0xdc8,uint32_t(0x101));assert(collect()==1); // Literal word, not bit 0.
    m.put(actor+0xdc8,uint32_t(2));assert(collect()==1);
    m.put(actor+0xdc8,uint32_t(0));m.fail=actor+0xdc8;assert(collect()==0);m.fail=0;
    m.deathFlip=actor+0xdc8;m.deathReads=0;assert(collect()==0); // Changes during collection.
    m.deathFlip=0;m.put(actor+0xdc8,uint32_t(0));assert(collect()==1);
    assert(std::string(output[0].name)=="[2] 测试玩家");
    assert(std::abs(output[0].x-500)<.01f && std::abs(output[0].distance-10)<.01f);
    for (OwnVector3 root:{OwnVector3{1000,1000,0},OwnVector3{1000,-1000,0},
                         OwnVector3{1000,0,500},OwnVector3{1000,0,-500}}) {
        m.put(component+0x168,root);
        assert(collect()==0); // Root projection gates all scoped output, even with bones still inside.
    }
    m.put(component+0x168,OwnVector3{});assert(collect()==1); // Zero is not the -1 sentinel.
    struct ShiftedCamera { OwnVector3 p;float pad;OwnRotation r;float fov; };
    m.put(camera+0x1890,ShiftedCamera{{-1001,-1,-1},0,{},90});
    m.put(component+0x168,OwnVector3{-1,-1,-1});assert(collect()==0);
    m.put(camera+0x1890,ShiftedCamera{{},0,{},90});
    m.put(component+0x168,OwnVector3{29901,0,0});config.maxDistance=299.5f;
    assert(collect()==0); // Original rounds metres before Max Dist comparison.
    config.maxDistance=300;assert(collect()==1 && output[0].distance==300.f);
    m.put(component+0x168,OwnVector3{1000,0,0});assert(collect()==1);
    assert(output[0].boneMask==0x3ffff && output[0].health==100 && output[0].boneMesh==mesh);
    assert(output[0].visibilitySource==1 && output[0].visible==1 && output[0].visibilityMesh==mesh);
    // cn armor uses the equipped ID list, not the retired fixed-slot layout.
    constexpr uintptr_t armorComponent=board+0x10000,armorArray=armorComponent+0x10000;
    m.put(actor+0x24e0,armorComponent);
    m.put(armorComponent+0x1f8,armorArray);m.put(armorComponent+0x200,int32_t(2));
    m.put(armorArray+8,uint64_t(11010002000));m.put(armorArray+32,uint64_t(11050006000));
    m.put(healthData+0x8c,83.f);m.put(healthData+0xb4,47.f);
    assert(collect()==1 && std::string(output[0].armor)=="头盔 2·47  护甲 6·83");
    m.put(armorArray+8,uint64_t(11010000000));
    assert(collect()==1 && std::string(output[0].armor)=="护甲 6·83");
    m.put(armorArray+8,uint64_t(11010002000));m.put(armorArray+32,uint64_t(11050000000));
    assert(collect()==1 && std::string(output[0].armor)=="头盔 2·47  ");
    m.put(armorArray+8,uint64_t(11010000000));
    assert(collect()==1 && output[0].armor[0]==0);
    m.put(armorArray+8,uint64_t(11010002000));m.put(armorArray+32,uint64_t(11050006000));
    m.put(state+0x39e,uint8_t(0));config.flags|=SJZ_SHOW_AI;
    assert(collect()==1 && output[0].bot && output[0].armor[0]==0);
    m.put(state+0x39e,uint8_t(1));config.flags=SJZ_DEFAULT_FLAGS;
    m.fail=mesh+0x898;
    assert(collect()==1 && output[0].visibilitySource==0);
    assert(std::string(output[0].name)=="[2] 测试玩家" && output[0].boneMask==0x3ffff);
    m.fail=0;
    m.put(mesh+0x898,uint8_t(0));m.put(mesh+0x8b8,uint8_t(1));
    assert(collect()==1 && output[0].visibilitySource==1 && output[0].visible==0);
    m.put(mesh+0x898,uint8_t(1));m.put(mesh+0x8b8,uint8_t(0));
    config.flags=SJZ_SHOW_ESP|SJZ_SHOW_HEAD;
    assert(collect()==1 && (output[0].boneMask&0x3u)==0x3u);
    config.flags=SJZ_SHOW_HEAD;
    assert(collect()==0); // ESP master switch suppresses display-only collection.
    config.flags=SJZ_DEFAULT_FLAGS;
    config.flags|=SJZ_SHOW_WEAPON;
    m.put(actor+0x1788,weapon);
    m.put(weapon+0x838,uint32_t(18010000006ULL));
    for (unsigned i=0;i<4;++i) m.bytes.erase(weapon+0x83c+i);
    assert(collect()==1 && std::string(output[0].weapon)=="AKM");
    m.put(weapon+0x83c,uint32_t(0xdeadbeef));
    assert(collect()==1 && std::string(output[0].weapon)=="AKM");
    m.put(weapon+0x838,uint32_t(0xffffffff));
    assert(collect()==1 && std::string(output[0].weapon)=="-1");
    m.put(weapon+0x838,uint32_t(0));
    assert(collect()==1 && std::string(output[0].weapon)=="0");
    m.put(actor+0x1788,uintptr_t(0));
    assert(collect()==1 && output[0].weapon[0]==0);
    config.flags&=~SJZ_SHOW_WEAPON;
    m.put(pawn+0x1788,weapon);m.put(weapon+0x838,uint64_t(18010000006ULL));
    m.put(pawn+0x1020,board);m.zero(board+0x63b,0x1e);m.put(board+0x63c,uint8_t(1));
    m.put(component+0x190,OwnVector3{10,0,0});
    config.flags|=SJZ_AIM_ENABLED;
    assert(collect()==1 && output[0].velocityValid && output[0].objectIndex==100);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：当前传输不具备角度写能力，已停用");
    assert(writeCalls==0);
    transportWrite=true;
    assert(collect()==1 && writeCalls==1);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：控制角度已写入并读回");
    failWrite=true;
    assert(collect()==1 && writeCalls==2);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：角度写入或读回失败，关闭后重新开启");
    assert(collect()==1 && writeCalls==2); // Failure is latched; no automatic retry.
    config.flags&=~SJZ_AIM_ENABLED;
    assert(collect()==1);
    config.flags|=SJZ_AIM_ENABLED;failWrite=false;
    assert(collect()==1 && writeCalls==3);
    failWrite=true;assert(collect()==1 && writeCalls==4);
    m.fail=base+0x178b44e0;config.flags&=~SJZ_AIM_ENABLED;
    assert(collect()==0); // A failed world read must still observe the disable action.
    m.fail=0;config.flags|=SJZ_AIM_ENABLED;failWrite=false;
    assert(collect()==1 && writeCalls==5);
    transportWrite=false;
    config.flags|=SJZ_AIM_VISIBLE_ONLY;
    assert(collect()==1);
    assert(std::string(sjzesp_last_aim_status())=="自瞄：真实视线查询不可用");
    config.flags&=~SJZ_AIM_VISIBLE_ONLY;
    config.flags&=~SJZ_AIM_ENABLED;
    const char16_t boundedName[]=u"abcdefghijklmnGARBAGE";
    m.raw(state+0x1000,boundedName,sizeof(boundedName));
    m.put(state+0x478,int32_t(sizeof(boundedName)/sizeof(char16_t)));
    assert(collect()==1 && std::string(output[0].name)=="[2] abcdefghijklmnGARBAGE");
    for (unsigned i=0;i<128;++i) m.bytes.erase(state+0x1000+i);
    m.put(state+0x1000,char16_t('x'));m.put(state+0x478,int32_t(2));
    assert(collect()==1 && std::string(output[0].name)=="[2] x"); // Exactly one allocated unit.
    const std::u16string longEnglish(63,u'a');
    m.raw(state+0x1000,longEnglish.data(),longEnglish.size()*2);m.put(state+0x478,int32_t(64));
    assert(collect()==1 && std::string(output[0].name)=="[2] "+std::string(35,'a'));
    const std::u16string longChinese=u"一二三四五六七八九十一二三suffix";
    m.raw(state+0x1000,longChinese.data(),longChinese.size()*2);
    m.put(state+0x478,int32_t(longChinese.size()+1));
    const std::string rawName="[2] 一二三四五六七八九十一二三suffix";
    assert(collect()==1 && std::string(output[0].name)==rawName.substr(0,39));
    for (int32_t count:{-1,0,1,65}) {
        m.put(state+0x478,count);
        assert(collect()==1 && std::string(output[0].name)=="[2] ");
    }
    m.zero(state+0x1000,28);
    const char16_t restoredName[]=u"测试玩家";
    m.raw(state+0x1000,restoredName,sizeof(restoredName));
    m.put(state+0x478,int32_t(sizeof(restoredName)/sizeof(char16_t)));
    m.put(teamData+0x108,int32_t(1)); assert(collect()==0);
    m.put(teamData+0x10c,int32_t(2));assert(collect()==0); // OP excludes same team even with different squad.
    m.put(teamData+0x10c,int32_t(1));
    m.put(teamData+0x108,int32_t(2));
    config.maxDistance=5; assert(collect()==0); config.maxDistance=300;
    m.put(state+0x39e,uint8_t(0));
    assert(collect()==1 && output[0].bot && output[0].boneMask==0x3ffff);
    // AI follows the cn fixed acquisition algorithm only when all reads succeed.
    m.fail=bones+5*0x30+0x10;
    assert(collect()==1 && output[0].bot && output[0].boneMask==0);
    m.fail=0;
    m.put(mesh+0xa18,uintptr_t(0));m.put(mesh+0x730,bones);
    assert(collect()==1 && output[0].bot && output[0].boneMask==0x3ffff);
    m.put(mesh+0xa18,bones);
    const uintptr_t botClass=klass+0x5000;
    m.put(botClass+0x40,klass);
    name(m,botClass,400,"NC_BP_DFMCharacter_AI_DT_RPG_C");
    m.put(actor+8,botClass);
    name(m,actor,600,"prefix_NC_BP_DFMCharacter_AI_DT_RPG_C_suffix");
    assert(collect()==1 && std::string(output[0].name)=="Ashara RPG");
    name(m,actor,600,"UnknownAI");assert(collect()==1 && output[0].name[0]==0);
    m.put(actor+0x1c,uint32_t(101));
    m.put(actor+8,klass);
    config.flags&=~SJZ_SHOW_AI; assert(collect()==0); config.flags|=SJZ_SHOW_AI;
    m.put(state+0x39e,uint8_t(1));
    m.put(healthData+0x3c,0.f); assert(collect()==1 && output[0].knocked);
    m.put(healthData+0x114,0.f); assert(collect()==0);
    m.put(healthData+0x3c,100.f); m.put(healthData+0x114,100.f);
    m.fail=camera+0x1890; assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_CAMERA); m.fail=0;
    m.changing=true; m.actorReads=0;
    assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_SCENE_CHANGED);
    m.changing=false;
    partial=true; assert(collect()==0 && sjzesp_stats().readFailures>0); partial=false;
    transportReady=false; assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_TRANSPORT); transportReady=true;
    config.flags=0; assert(collect()==0); config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT;
    config.maxDistance=std::numeric_limits<float>::quiet_NaN(); assert(collect()==0); config.maxDistance=300;
    m.put(level+0x98,OwnArrayHeader{list,2,2}); m.put(list+8,pickup);
    m.put(pickup+8,pickupClass); m.put(pickup+0x18,uint32_t(0));
    m.put(pickupClass+0x40,uintptr_t(0)); name(m,pickupClass,200,"SomeUnrelatedClass");
    name(m,pickup,500,"InventoryPickup_001");
    m.put(pickup+0x180,pickupComponent); m.put(pickupComponent+0x168,OwnVector3{1200,20,0});
    m.put(pickupComponent+0x148,OwnVector3{-7000,-8000,-9000});
    m.put(pickup+0x1200,configData);
    m.put(configData+0x68,int32_t(4)); m.put(configData+0xdc,int32_t(12345));
    assert(collect()==2 && output[1].category==SJZ_CATEGORY_LOOT && output[1].level==4);
    assert(output[1].name[0]==0 && output[1].price==12345);
    name(m,pickup,500,"ItemPickup_001"); assert(collect()==2);
    name(m,pickup,500,"inventorypickup_001"); assert(collect()==1); // Ordinary matching is case sensitive.
    name(m,pickup,500,"UnrelatedObject_001"); assert(collect()==1);
    name(m,pickup,500,"InventoryPickup_001");
    m.put(pickup+0x18,uint32_t(0x18000)); assert(collect()==1);
    m.put(pickup+0x18,uint32_t(0));
    m.put(pickup+0x1c,uint32_t(30000));
    m.put(nameBlock+size_t(30000)*2,uint16_t((19<<6)|1)); assert(collect()==1);
    name(m,pickup,500,"InventoryPickup_001");
    m.put(configData+0xdc,int32_t(-1));
    assert(collect()==2 && output[1].price==-1); // Nonpositive price still displays distance.
    m.put(configData+0xdc,int32_t(12345));
    config.lootLevel=4.f; assert(collect()==2); // Inclusive integer boundary.
    config.lootLevel=4.01f; assert(collect()==1); // Keep cn's float comparison.
    config.lootLevel=3.99f; assert(collect()==2);
    config.lootLevel=0.f; assert(collect()==2);
    m.put(configData+0x68,int32_t(7));
    assert(collect()==2 && output[1].level==0); // cn normalizes unsupported grades.
    config.lootLevel=.01f; assert(collect()==1);
    m.put(configData+0x68,int32_t(4));
    config.lootLevel=6.f; assert(collect()==1);
    config.lootLevel=std::numeric_limits<float>::quiet_NaN(); assert(collect()==1);
    config.lootLevel=3.f;
    config.maxDistance=50.f;
    m.put(pickupComponent+0x168,OwnVector3{15000,0,0});
    assert(collect()==2 && output[1].distance==150.f); // Loot range is independent.
    m.put(pickupComponent+0x168,OwnVector3{20000,0,0});
    assert(collect()==2 && output[1].distance==200.f);
    m.put(pickupComponent+0x168,OwnVector3{20001,0,0});
    assert(collect()==1); // ceil(200.01m) exceeds the fixed 200m range.
    m.put(pickupComponent+0x168,OwnVector3{1200,20,0});
    assert(collect()==2 && output[1].distance==13.f);
    config.maxDistance=300.f;
    const uintptr_t deadClass=pickupClass+0x5000;
    m.put(deadClass+0x40,pickupClass);
    name(m,deadClass,300,"SomeUnrelatedClass");
    m.put(pickup+8,deadClass);
    name(m,pickup,500,"BP_DeAdBoDy_001"); m.put(pickup+0x150,1.f);
    config.flags=SJZ_DEFAULT_FLAGS&~SJZ_SHOW_LOOT;
    assert(collect()==1); // Death Box requires the parent Loot switch.
    config.flags=SJZ_DEFAULT_FLAGS;
    assert(collect()==2 && output[1].category==SJZ_CATEGORY_CONTAINER);
    assert(std::string(output[1].name)=="死亡盒");
    name(m,pickup,500,"BP_CARRYBODY_001"); assert(collect()==2 && output[1].category==SJZ_CATEGORY_CONTAINER);
    name(m,pickup,500,"BP_GroundBox_001"); assert(collect()==2 && output[1].category==SJZ_CATEGORY_CONTAINER);
    m.put(pickup+0x150,0.f); assert(collect()==1);
    m.put(pickup+0x150,2.f); assert(collect()==1);
    m.put(pickup+0x150,1.5f); assert(collect()==2);
    name(m,pickup,500,"Inventory_GroundBox_001"); m.put(pickup+0x150,0.f);
    assert(collect()==2 && output[1].category==SJZ_CATEGORY_CONTAINER); // Ordinary candidate then body rendering.
    name(m,pickup,500,"BP_DeAdBoDy_001"); m.put(pickup+0x150,1.f);
    m.put(pickupComponent+0x168,OwnVector3{20000,0,0});
    assert(collect()==2 && output[1].distance==200.f);
    m.put(pickupComponent+0x168,OwnVector3{20001,0,0});
    assert(collect()==1);
    m.put(pickupComponent+0x168,OwnVector3{1200,20,0});
    config.lootLevel=6.f;
    assert(collect()==2); // Min Level filters normal loot, never Death Box.
    config.lootLevel=3.f;
    config.flags=SJZ_DEFAULT_FLAGS&~SJZ_SHOW_CONTAINER;
    assert(collect()==1); // Death Box can be disabled independently.
    m.put(pickup+8,pickupClass);
    name(m,pickup,500,"InventoryPickup_001");
    config.flags=SJZ_DEFAULT_FLAGS|SJZ_SHOW_LOOT;
    assert(sjzesp_tick(base,1000,500,&config,output,1)==1);
    m.put(level+0x98,OwnArrayHeader{0,0,0}); assert(collect()==0 && sjzesp_stats().status==SJZ_STATUS_READY);
    sjzesp_reset(); assert(sjzesp_stats().publishedCount==0);
    std::cout<<"PASS: real collector/transport adapter, projection, UTF-8, filtering, bones, loot, partial read, scene change, empty scene\n";
}
