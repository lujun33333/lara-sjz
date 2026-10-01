#include "sjz/SJZAim.h"
#include "sjz/CnSkeletonLayout.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace {
constexpr std::uintptr_t image=0x100000000,world=0x200000000,driver=0x200100000,
    connection=0x200200000,controller=0x200300000,level=0x200400000,
    pawn=0x200500000,camera=0x200600000,weapon=0x200700000,
    board=0x200800000,actor=0x200900000,klass=0x200a00000,
    ability=0x200b00000,health=0x200c00000,ownTeam=0x200d00000,
    enemyTeam=0x200e00000,component=0x200f00000,
    mesh=0x201000000,bones=0x201100000;

struct Fixture {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    int visibleCalls=0;
    int readCalls=0;
    bool visible=true;
    template<class T> void put(std::uintptr_t address,const T& value) {
        const auto* raw=reinterpret_cast<const std::uint8_t*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=raw[i];
    }
    static bool read(void* ctx,std::uintptr_t address,void* out,std::size_t size) {
        auto& self=*static_cast<Fixture*>(ctx);
        ++self.readCalls;
        auto* raw=static_cast<std::uint8_t*>(out);
        for(std::size_t i=0;i<size;++i) {
            auto found=self.bytes.find(address+i);
            if(found==self.bytes.end()) return false;
            raw[i]=found->second;
        }
        return true;
    }
    static bool los(void* ctx,std::uintptr_t receiver,std::uintptr_t target,
                    OwnVector3 point,bool& visible) {
        auto& self=*static_cast<Fixture*>(ctx);
        assert(receiver==controller && target==actor && std::isfinite(point.x));
        ++self.visibleCalls;visible=self.visible;return true;
    }
    Fixture() {
        put(image+0x178b44e0,world);put(world+0x30,driver);
        put(driver+0x98,connection);put(connection+0x30,controller);
        put(world+0xf8,level);put(controller+0x3a0,pawn);
        put(controller+0x408,camera);put(pawn+0x1788,weapon);
        put(controller+0x3d8,OwnRotation{});
        put(pawn+0x1020,board);
        struct CameraBlock { OwnVector3 location; float pad; OwnRotation rotation; float fov; };
        static_assert(sizeof(CameraBlock)==32,"camera fixture ABI");
        put(camera+0x1890,CameraBlock{{},0,{},90});
        put(weapon+0x838,std::uint64_t(18010000006ULL));
        put(board+0x5ac,std::uint8_t(0));
        put(actor+0x24,std::int32_t(12));put(actor+8,klass);
        put(actor+0x1c,std::uint32_t(34));
        put(actor+0x180,component);put(component+0x168,OwnVector3{1000,0,0});
        put(component+0x148,OwnVector3{-7000,-8000,-9000});
        put(actor+0x3d0,mesh);put(mesh+0xa18,bones);
        struct RawTransform {
            std::array<float,4> rotation;OwnVector3 translation,scale;
        };
        put(mesh+0x210,RawTransform{{0,0,0,1},{},{1,1,1}});
        for(auto index:kCnHumanMeshIndices)
            put(bones+std::uintptr_t(index)*0x30+0x10,OwnVector3{1000,0,0});
        put(bones+31*0x30+0x10,OwnVector3{1000,40,80});
        put(bones+59*0x30+0x10,OwnVector3{1000,-40,-80});
        put(actor+0x10b8,ability);put(ability+0x280,health);
        put(actor+0xdc8,std::uint32_t(0));
        for(auto offset: {0x3c,0x54,0x114,0x124,0x74,0x8c,0x9c,0xb4})
            put(health+offset,100.f);
        put(pawn+0x10c0,ownTeam);put(actor+0x10c0,enemyTeam);
        put(ownTeam+0x108,std::int32_t(1));put(ownTeam+0x10c,std::int32_t(1));
        put(enemyTeam+0x108,std::int32_t(2));put(enemyTeam+0x10c,std::int32_t(1));
    }
    SJZAimAccess access(bool withLos=false) {
        return {{this,read},this,withLos?los:nullptr};
    }
};

sjzesp_item_t target() {
    sjzesp_item_t item{};
    item.identity=actor;item.identityClass=klass;item.identityName=34;
    item.objectIndex=12;item.category=SJZ_CATEGORY_PLAYER;
    item.distance=10;item.velocityValid=1;
    // Target planning reads current cn mesh points, not cached display points.
    item.boneMask=0;
    return item;
}
sjzesp_config_t config() {
    return {SJZ_AIM_ENABLED,300,0,1,14,1,300,0,0};
}
SJZAimResult run(Fixture& f,sjzesp_config_t c,const sjzesp_item_t& item,
                 SJZAimPlan& plan,bool withLos=false) {
    return SJZPlanAim(f.access(withLos),image,c,&item,1,1000,500,plan);
}
void near(float actual,float expected,float epsilon=.03f) {
    assert(std::fabs(actual-expected)<epsilon);
}
}

int main() {
    Fixture f;auto c=config();auto item=target();SJZAimPlan plan{};
    c.flags=0;assert(run(f,c,item,plan).status==SJZAimStatus::Disabled);
    c.flags=SJZ_AIM_ENABLED;
    f.bytes.erase(pawn+0x1020); // Always must not depend on unverified input board.
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    assert(plan.world==world && plan.target==actor && plan.focal>0);
    near(plan.screenX,500);near(plan.screenY,250);
    f.put(pawn+0x1020,board);
    for(int mode=0;mode<3;++mode) for(std::uint8_t raw: {0,1,2,3,128,255}) {
        c.aimTrigger=mode;f.put(board+0x5ac,raw);
        const bool active=mode!=1||(raw&1);
        assert(run(f,c,item,plan).status==(active?SJZAimStatus::PlanReady:
                                                     SJZAimStatus::TriggerInactive));
    }
    // Original Fire == 2 does not read a fire board; it follows Always.
    c.aimTrigger=2;f.bytes.erase(pawn+0x1020);
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    c.aimTrigger=1;
    assert(run(f,c,item,plan).status==SJZAimStatus::InputUnavailable);
    f.put(pawn+0x1020,board);f.bytes.erase(board+0x5ac);
    assert(run(f,c,item,plan).status==SJZAimStatus::InputUnavailable);
    f.put(board+0x5ac,std::uint8_t(0));
    // Poison the former profile fields: none may activate the cn Scope mode.
    for(std::uintptr_t p: {board+0x63b,board+0x63c,board+0x63d,
                           board+0x63f,board+0x657,board+0x658}) f.put(p,std::uint8_t(1));
    assert(run(f,c,item,plan).status==SJZAimStatus::TriggerInactive);
    c.aimTrigger=0;

    c.aimPart=1;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    near(plan.screenX,520);near(plan.screenY,210); // cn slot 0, mesh 31.
    c.aimPart=2;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    near(plan.screenX,480);near(plan.screenY,290); // cn slot 13, mesh 59.
    c.aimPart=0;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    near(plan.screenX,500);near(plan.screenY,250); // cn slot 2, mesh 5.
    for(auto root: {OwnVector3{1000,-1000,0},OwnVector3{1000,1000,0},
                   OwnVector3{1000,0,500},OwnVector3{1000,0,-500},
                   OwnVector3{1000,2000,0}}) {
        f.put(component+0x168,root);
        assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
        assert(plan.target==0); // In-screen bones cannot override origin boundaries.
    }
    f.put(component+0x168,OwnVector3{});
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady); // Zero origin is legal.
    // Place the camera so the sentinel itself would project inside the viewport.
    f.put(camera+0x1890,std::array<float,8>{-100,-1,-1,0,0,0,0,90});
    f.put(component+0x168,OwnVector3{-1,-1,-1});
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    assert(plan.target==0);
    f.put(camera+0x1890,std::array<float,8>{0,0,0,0,0,0,0,90});
    f.bytes.erase(component+0x168);
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    assert(plan.target==0);
    f.put(component+0x168,OwnVector3{1000,0,0});
    f.put(actor+0xdc8,std::uint32_t(1));
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    assert(plan.target==0); // Positive health does not override the original death word.
    f.put(actor+0xdc8,std::uint32_t(0x101));
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady); // Literal == 1, not a mask.
    f.bytes.erase(actor+0xdc8);
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    assert(plan.target==0); // Incomplete qualification reads must stop the plan.
    f.put(actor+0xdc8,std::uint32_t(0));
    item.velocityX=100;item.velocityY=100;item.velocityZ=900;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    near(plan.desiredAngles.pitch,0,.001f);
    near(plan.desiredAngles.yaw,.1089272127f,.001f); // Original ordinary-angle fixture.
    // Display IDs use four bytes, but LocalPlayerData aim IDs use all eight.
    const std::uint64_t pollutedId=(std::uint64_t(0xaabbccdd)<<32)|
        std::uint32_t(18010000006ULL);
    f.put(weapon+0x838,pollutedId);
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    assert(plan.desiredAngles.yaw<.08f); // Full 64-bit lookup now uses default 750.
    f.put(weapon+0x838,std::uint64_t(18010000006ULL));
    item.velocityX=0;
    item.velocityY=100;item.velocityZ=100;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    assert(plan.screenX>500);near(plan.screenY,250); // Ordinary cn output leads X/Y, not Z.
    item.velocityValid=0;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    near(plan.screenX,500);near(plan.screenY,250); // Missing velocity uses selected chest bone.
    item.velocityValid=1;
    for(int part=0;part<3;++part) {
        c.aimPart=part;item.velocityValid=0;
        assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
        const float x=plan.screenX,y=plan.screenY;
        item.velocityValid=1;
        assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
        assert(plan.screenX>x);near(plan.screenY,y); // Each selected bone remains the XY lead origin.
    }
    c.aimPart=0;
    f.bytes.erase(bones+5*0x30+0x10);
    const auto missingBone=run(f,c,item,plan);
    assert(missingBone.status==SJZAimStatus::NoTarget);
    assert(missingBone.records==1 && missingBone.eligible==1 && missingBone.boneReject==1);
    assert(plan.target==0); // Missing mesh point must not fall back to actor/root.
    f.put(bones+5*0x30+0x10,OwnVector3{1000,0,0});
    item.bot=1;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady);
    assert(plan.target==actor); // The original display flag alone must not exclude a valid target.
    item.bot=0;

    c.flags|=SJZ_AIM_VISIBLE_ONLY;
    assert(run(f,c,item,plan).status==SJZAimStatus::VisibilityUnavailable);
    f.visible=false;f.visibleCalls=0;
    assert(run(f,c,item,plan,true).status==SJZAimStatus::NoTarget);
    assert(f.visibleCalls==18); // Each real candidate is rejected; no false visible fallback.
    f.visible=true;f.visibleCalls=0;
    assert(run(f,c,item,plan,true).status==SJZAimStatus::PlanReady);
    assert(f.visibleCalls>=2); // Includes the final selected-bone recheck.
    c.flags&=~SJZ_AIM_VISIBLE_ONLY;

    // Preferred head outside the circle can select another real in-range bone.
    item.velocityValid=0;c.aimPart=1;c.aimRadius=10;
    assert(run(f,c,item,plan).status==SJZAimStatus::PlanReady && plan.boneSlot!=0);
    near(plan.screenX,500);near(plan.screenY,250);
    // Every candidate exactly on the radius is excluded by the cn strict test.
    for(auto index:kCnHumanMeshIndices)
        f.put(bones+std::uintptr_t(index)*0x30+0x10,OwnVector3{1000,20,0});
    assert(run(f,c,item,plan).status==SJZAimStatus::NoTarget);
    for(auto index:kCnHumanMeshIndices)
        f.put(bones+std::uintptr_t(index)*0x30+0x10,OwnVector3{1000,0,0});
    f.put(bones+31*0x30+0x10,OwnVector3{1000,40,80});
    f.put(bones+59*0x30+0x10,OwnVector3{1000,-40,-80});
    c.aimPart=0;c.aimRadius=300;

    f.put(actor+0x24,std::int32_t(13));
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    f.put(actor+0x24,std::int32_t(12));
    c.aimSpeed=0;
    const int reads=f.readCalls;
    plan={world,actor,1,2,3,4,5};
    assert(run(f,c,item,plan).status==SJZAimStatus::Disabled);
    assert(f.readCalls==reads && plan.world==0 && plan.target==0 &&
           plan.screenX==0 && plan.screenY==0 && plan.focal==0 &&
           plan.pitch==0 && plan.yaw==0);
    for(float invalid: {-1.f,std::numeric_limits<float>::quiet_NaN(),1.01f}) {
        c.aimSpeed=invalid;
        assert(run(f,c,item,plan).status==SJZAimStatus::InvalidConfig);
        assert(f.readCalls==reads && plan.target==0);
    }
    c.aimSpeed=1;c.aimRadius=0;
    plan={world,actor,1,2,3,4,5};
    assert(run(f,c,item,plan).status==SJZAimStatus::Disabled);
    assert(f.readCalls==reads && plan.world==0 && plan.target==0 &&
           plan.screenX==0 && plan.screenY==0 && plan.focal==0 &&
           plan.pitch==0 && plan.yaw==0);
    for(float invalid: {-1.f,std::numeric_limits<float>::quiet_NaN(),1000.01f}) {
        c.aimRadius=invalid;
        assert(run(f,c,item,plan).status==SJZAimStatus::InvalidConfig);
        assert(f.readCalls==reads && plan.target==0);
    }
}
