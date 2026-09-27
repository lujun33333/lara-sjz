#include "sjz/SJZAim.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <map>

namespace {
constexpr std::uintptr_t image=0x100000000,world=0x200000000,driver=0x200100000,
    connection=0x200200000,controller=0x200300000,level=0x200400000,
    pawn=0x200500000,camera=0x200600000,weapon=0x200700000,
    board=0x200800000,actor=0x200900000,klass=0x200a00000,
    ability=0x200b00000,health=0x200c00000,ownTeam=0x200d00000,
    enemyTeam=0x200e00000,component=0x200f00000;

struct Fixture {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    int visibleCalls=0;
    bool visible=true;
    template<class T> void put(std::uintptr_t address,const T& value) {
        const auto* raw=reinterpret_cast<const std::uint8_t*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=raw[i];
    }
    static bool read(void* ctx,std::uintptr_t address,void* out,std::size_t size) {
        auto& self=*static_cast<Fixture*>(ctx);
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
        put(pawn+0x1020,board);
        struct CameraBlock { OwnVector3 location; float pad; OwnRotation rotation; float fov; };
        static_assert(sizeof(CameraBlock)==32,"camera fixture ABI");
        put(camera+0x1890,CameraBlock{{},0,{},90});
        put(weapon+0x838,std::uint64_t(18010000006ULL));
        for(std::uintptr_t p: {board+0x63b,board+0x63c,board+0x63d,
                               board+0x63f,board+0x657,board+0x658}) put(p,std::uint8_t(0));
        put(actor+0x24,std::int32_t(12));put(actor+8,klass);
        put(actor+0x1c,std::uint32_t(34));
        put(actor+0x180,component);put(component+0x168,OwnVector3{1000,0,0});
        put(actor+0x10b8,ability);put(ability+0x280,health);
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
    // Official IPA aim must not rely on the old sample's bone index table.
    item.boneMask=0;
    return item;
}
sjzesp_config_t config() {
    return {SJZ_AIM_ENABLED,300,0,1,14,1,300,0,0};
}
SJZAimResult run(Fixture& f,sjzesp_config_t c,const sjzesp_item_t& item,
                 SJZTouchAimPlan& plan,bool withLos=false) {
    return SJZPlanTouchAim(f.access(withLos),image,c,&item,1,1000,500,plan);
}
void near(float actual,float expected,float epsilon=.03f) {
    assert(std::fabs(actual-expected)<epsilon);
}
}

int main() {
    Fixture f;auto c=config();auto item=target();SJZTouchAimPlan plan{};
    c.flags=0;assert(run(f,c,item,plan).status==SJZAimStatus::Disabled);
    c.flags=SJZ_AIM_ENABLED;
    f.bytes.erase(pawn+0x1020); // Always must not depend on unverified input board.
    assert(run(f,c,item,plan).status==SJZAimStatus::TouchPlanReady);
    assert(plan.world==world && plan.target==actor && plan.focal>0);
    near(plan.screenX,500);near(plan.screenY,250);
    f.put(pawn+0x1020,board);
    c.aimTrigger=1;
    assert(run(f,c,item,plan).status==SJZAimStatus::TriggerInactive);
    f.put(board+0x63b,std::uint8_t(1));
    assert(run(f,c,item,plan).status==SJZAimStatus::TouchPlanReady);
    f.put(board+0x63b,std::uint8_t(0));
    c.aimTrigger=2;
    assert(run(f,c,item,plan).status==SJZAimStatus::TriggerInactive);
    f.put(board+0x63c,std::uint8_t(1));
    assert(run(f,c,item,plan).status==SJZAimStatus::TouchPlanReady);
    f.put(board+0x63c,std::uint8_t(0));c.aimTrigger=0;

    c.aimPart=1;
    assert(run(f,c,item,plan).status==SJZAimStatus::UnverifiedAimPart);
    c.aimPart=2;
    assert(run(f,c,item,plan).status==SJZAimStatus::UnverifiedAimPart);
    c.aimPart=0;
    item.velocityY=100;item.velocityZ=100;
    assert(run(f,c,item,plan).status==SJZAimStatus::TouchPlanReady);
    assert(plan.screenX>500 && plan.screenY<250); // Three-axis lead affects the screen point.
    item.velocityValid=0;
    assert(run(f,c,item,plan).status==SJZAimStatus::TouchPlanReady);
    near(plan.screenX,500);near(plan.screenY,250); // Missing velocity uses actor center.
    item.velocityValid=1;

    c.flags|=SJZ_AIM_VISIBLE_ONLY;
    assert(run(f,c,item,plan).status==SJZAimStatus::VisibilityUnavailable);
    f.visible=false;f.visibleCalls=0;
    assert(run(f,c,item,plan,true).status==SJZAimStatus::NoTarget);
    assert(f.visibleCalls==1);
    f.visible=true;f.visibleCalls=0;
    assert(run(f,c,item,plan,true).status==SJZAimStatus::TouchPlanReady);
    assert(f.visibleCalls==2);
    c.flags&=~SJZ_AIM_VISIBLE_ONLY;

    f.put(actor+0x24,std::int32_t(13));
    assert(run(f,c,item,plan).status==SJZAimStatus::StaleTarget);
    f.put(actor+0x24,std::int32_t(12));
    c.aimSpeed=0;
    assert(run(f,c,item,plan).status==SJZAimStatus::InvalidConfig);
}
