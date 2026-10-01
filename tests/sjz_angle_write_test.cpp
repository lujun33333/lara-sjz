#include "sjz/CnAngleWrite.h"
#include <cassert>
#include <iostream>
#include <map>

struct Fixture {
    static constexpr std::uintptr_t image=0x100000000,world=0x200000,driver=0x210000,
        connection=0x220000,controller=0x230000,pawn=0x240000,weapon=0x250000,
        target=0x260000,klass=0x270000,level=0x280000,camera=0x290000,
        localTeam=0x300000,targetTeam=0x310000,ability=0x320000,health=0x330000;
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uint64_t generation=7;
    long firstWrite=8;
    int writes=0;
    bool externalChange=false, sceneChange=false, identityChange=false, failReadback=false, sessionChange=false;
    template<class T> void put(std::uintptr_t address,const T& value) {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=data[i];
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) {
        auto& f=*static_cast<Fixture*>(context);
        if (f.failReadback && f.writes && address==controller+0x3d8) return false;
        auto* data=static_cast<unsigned char*>(output);
        for (std::size_t i=0;i<size;++i) {
            const auto found=f.bytes.find(address+i);
            if (found==f.bytes.end()) return false;
            data[i]=found->second;
        }
        return true;
    }
    static long write(void* context,std::uintptr_t address,const void* input,std::size_t size) {
        auto& f=*static_cast<Fixture*>(context);
        assert(address==controller+0x3d8 && size==8);
        const long result=f.writes++ ? 8:f.firstWrite;
        const auto* data=static_cast<const unsigned char*>(input);
        if (result>0 && result<=long(size))
            for (long i=0;i<result;++i) f.bytes[address+std::size_t(i)]=data[i];
        if (f.externalChange) f.put(address+4,37.f);
        if (f.sceneChange) f.put(image+0x178b44e0,world+8);
        if (f.identityChange) f.put(target+0x1c,std::uint32_t(99));
        if (f.sessionChange) ++f.generation;
        return result;
    }
    static std::uint64_t session(void* context) { return static_cast<Fixture*>(context)->generation; }
    Fixture() {
        put(image+0x178b44e0,world);put(world+0x30,driver);put(driver+0x98,connection);
        put(connection+0x30,controller);put(world+0xf8,level);
        put(controller+0x3a0,pawn);put(controller+0x408,camera);put(pawn+0x1788,weapon);
        put(target+8,klass);put(target+0x1c,std::uint32_t(123));put(target+0x24,std::int32_t(456));
        put(target+0x18,std::uint32_t(0));put(pawn+0x10c0,localTeam);put(target+0x10c0,targetTeam);
        put(target+0xdc8,std::uint32_t(0));
        put(localTeam+0x108,std::int32_t(1));put(localTeam+0x10c,std::int32_t(1));
        put(targetTeam+0x108,std::int32_t(2));put(targetTeam+0x10c,std::int32_t(1));
        put(target+0x10b8,ability);put(ability+0x280,health);
        for (auto offset:{0x3c,0x54,0x114,0x124,0x74,0x8c,0x9c,0xb4}) put(health+offset,100.f);
        put(controller+0x3d8,OwnRotation{10,20,30});
    }
    CnAngleWriteAccess access() { return {{this,read},this,write,session,true,true}; }
    CnAngleWritePlan plan() { return {image,world,controller,pawn,weapon,target,klass,7,123,456,{10,20,30},{11,22,30}}; }
    OwnRotation rotation() {
        OwnRotation result;
        assert(read(this,controller+0x3d8,&result,sizeof(result)));return result;
    }
};

int main() {
    for (int mode=0;mode<4;++mode) {
        Fixture f;auto a=f.access();auto p=f.plan();
        if (mode==0) a.canWrite=false;
        if (mode==1) a.versionVerified=false;
        if (mode==2) a.write=nullptr;
        if (mode==3) a.memory.read=nullptr;
        assert(CnApplyAngleWrite(a,p)==CnAngleWriteStatus::Unavailable && !f.writes);
    }
    for (int mode=0;mode<12;++mode) {
        Fixture f;auto p=f.plan();
        if (mode==0) f.generation=8;
        if (mode==1) f.put(Fixture::target+0x1c,std::uint32_t(99));
        if (mode==2) f.put(Fixture::target+0x24,std::int32_t(999));
        if (mode==3) f.put(Fixture::target+8,Fixture::klass+8);
        if (mode==4) f.put(Fixture::target+0x18,std::uint32_t(0x18000));
        if (mode==5) f.put(Fixture::image+0x178b44e0,Fixture::world+8);
        if (mode==6) f.put(Fixture::pawn+0x1788,Fixture::weapon+8);
        if (mode==7) f.put(Fixture::targetTeam+0x108,std::int32_t(1));
        if (mode==8) {f.put(Fixture::health+0x3c,0.f);f.put(Fixture::health+0x114,0.f);}
        if (mode==9) f.put(Fixture::controller+0x3d8,OwnRotation{10,21,30});
        if (mode==10) f.put(Fixture::target+0xdc8,std::uint32_t(1));
        if (mode==11) f.bytes.erase(Fixture::target+0xdc8);
        assert(CnApplyAngleWrite(f.access(),p)==CnAngleWriteStatus::Stale && !f.writes);
    }
    {Fixture f;auto p=f.plan();p.after.roll=31;
        assert(CnApplyAngleWrite(f.access(),p)==CnAngleWriteStatus::Invalid && !f.writes);}
    {Fixture f;auto p=f.plan();p.session=0;
        assert(CnApplyAngleWrite(f.access(),p)==CnAngleWriteStatus::Invalid && !f.writes);}
    {Fixture f;assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Applied);
        auto r=f.rotation();assert(f.writes==1 && r.pitch==11 && r.yaw==22 && r.roll==30);}
    {Fixture f;f.put(Fixture::target+0xdc8,std::uint32_t(0x101));
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Applied && f.writes==1);}
    {Fixture f;auto p=f.plan();p.weapon=0;f.put(Fixture::pawn+0x1788,std::uintptr_t(0));
        assert(CnApplyAngleWrite(f.access(),p)==CnAngleWriteStatus::Applied && f.writes==1);}
    {Fixture f;f.put(Fixture::health+0x3c,0.f);f.put(Fixture::health+0x114,100.f);
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Stale && !f.writes);}
    {Fixture f;f.firstWrite=4;
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Failed);
        auto r=f.rotation();assert(f.writes==2 && r.pitch==10 && r.yaw==20 && r.roll==30);}
    {Fixture f;f.firstWrite=4;f.externalChange=true;
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Failed);
        auto r=f.rotation();assert(f.writes==1 && r.pitch==11 && r.yaw==37 && r.roll==30);}
    for (int mode=0;mode<4;++mode) {
        Fixture f;f.firstWrite=4;
        if (mode==0) f.sceneChange=true;
        if (mode==1) f.identityChange=true;
        if (mode==2) f.failReadback=true;
        if (mode==3) f.sessionChange=true;
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Failed && f.writes==1);
    }
    {Fixture f;f.sceneChange=true;
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Stale && f.writes==1);}
    for (long result:{0L,-1L,9L}) {
        Fixture f;f.firstWrite=result;
        assert(CnApplyAngleWrite(f.access(),f.plan())==CnAngleWriteStatus::Failed && f.writes==1);
    }
    std::cout<<"PASS: capability/version/session gates, fresh target/scene, exact 8-byte write, roll preservation, owned partial recovery\n";
}
