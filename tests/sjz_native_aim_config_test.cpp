#include "sjz/SJZNativeAimConfig.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

namespace {
constexpr std::uintptr_t module=0x300000000;
struct Memory {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    int writes=0;
    std::uintptr_t failWrite=0;
    std::uintptr_t failRead=0;
    template<class T> void put(std::uintptr_t p,const T& v) {
        const auto* raw=reinterpret_cast<const std::uint8_t*>(&v);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[p+i]=raw[i];
    }
    template<class T> T get(std::uintptr_t p) {
        T value{};assert(read(this,p,&value,sizeof(value)));return value;
    }
    static bool read(void* ctx,std::uintptr_t p,void* out,std::size_t n) {
        auto& m=*static_cast<Memory*>(ctx);auto* raw=static_cast<std::uint8_t*>(out);
        if(p==m.failRead) return false;
        for(std::size_t i=0;i<n;++i) {
            auto it=m.bytes.find(p+i);
            if(it==m.bytes.end()) return false;
            raw[i]=it->second;
        }
        return true;
    }
    static bool write(void* ctx,std::uintptr_t p,const void* data,std::size_t n) {
        auto& m=*static_cast<Memory*>(ctx);
        if(p==m.failWrite) return false;
        ++m.writes;
        auto* raw=static_cast<const std::uint8_t*>(data);
        for(std::size_t i=0;i<n;++i) m.bytes[p+i]=raw[i];
        return true;
    }
    SJZNativeAimAccess access() {return {{this,read},this,write};}
};
Memory fixture() {
    Memory m;
    const std::uint8_t menu[8]={0x41,0x58,0x00,0xf0,0x21,0xe0,0x2d,0x91};
    const std::uint8_t consumer[8]={0xb6,0x57,0x00,0xd0,0xc8,0xe2,0x6d,0x39};
    m.put(module+0x1f6b94,menu);m.put(module+0x20b2b0,consumer);
    m.put(module+0xd01b78,std::uint8_t(0));
    m.put(module+0xc38390,std::int32_t(1));
    m.put(module+0xc38394,.35f);
    m.put(module+0xc38398,120.f);
    m.put(module+0xd01b7c,std::int32_t(0));
    return m;
}
sjzesp_config_t config() {
    return {SJZ_AIM_ENABLED,300,3,1,14,.5f,180.f,2,0};
}
}

int main() {
    auto m=fixture();auto c=config();SJZNativeAimConfig native;
    auto a=m.access();
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(m.get<std::uint8_t>(module+0xd01b78)==1);
    assert(m.get<std::int32_t>(module+0xc38390)==2);
    assert(m.get<float>(module+0xc38394)==.5f);
    assert(m.get<float>(module+0xc38398)==180.f);
    assert(m.get<std::int32_t>(module+0xd01b7c)==1); // Chest external 0 -> native 1.
    const int writes=m.writes;
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(m.writes==writes); // Per-frame readback avoids needless writes.
    c.aimPart=1;
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(m.get<std::int32_t>(module+0xd01b7c)==0); // Head external 1 -> native 0.
    c.aimPart=2;
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(m.get<std::int32_t>(module+0xd01b7c)==2);
    m.put(module+0xc38394,.25f); // Module config reload changed a value.
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(m.get<float>(module+0xc38394)==.5f);
    assert(native.stop(a,1)==SJZNativeAimState::Restored);
    assert(m.get<std::uint8_t>(module+0xd01b78)==0);
    assert(m.get<float>(module+0xc38394)==.35f);
    assert(m.get<std::int32_t>(module+0xd01b7c)==0);

    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    m.put(module+0xc38398,777.f); // Another writer owns this field now.
    assert(native.stop(a,1)==SJZNativeAimState::RestoreConflict);
    assert(m.get<float>(module+0xc38398)==777.f);
    assert(m.get<std::uint8_t>(module+0xd01b78)==0);

    m.put(module+0x1f6b94,std::uint8_t(0));
    const int beforeMismatch=m.writes;
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::IdentityMismatch);
    assert(m.writes==beforeMismatch);
    m=fixture();a=m.access();
    assert(native.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    assert(native.tick(a,1,0x200000100,module,c)==SJZNativeAimState::Configured);
    assert(m.get<std::uint8_t>(module+0xd01b78)==1);
    assert(native.stop(a,2)==SJZNativeAimState::Idle); // New process: never restore stale address.
    assert(!native.active());

    m=fixture();a=m.access();SJZNativeAimConfig moved;
    constexpr std::uintptr_t module2=module+0x10000000;
    for(const auto& [offset,size]: {std::pair<std::uintptr_t,std::size_t>{0x1f6b94,8},
                                  {0x20b2b0,8},{0xd01b78,1},{0xc38390,4},
                                  {0xc38394,4},{0xc38398,4},{0xd01b7c,4}})
        for(std::size_t i=0;i<size;++i) m.bytes[module2+offset+i]=m.bytes[module+offset+i];
    assert(moved.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    m.failRead=module+0xd01b78;
    assert(moved.tick(a,1,0x200000000,module2,c)==SJZNativeAimState::RestorePending);
    assert(m.get<std::uint8_t>(module2+0xd01b78)==0); // New module left untouched.
    m.failRead=0;
    assert(moved.tick(a,1,0x200000000,module2,c)==SJZNativeAimState::Configured);
    assert(m.get<std::uint8_t>(module+0xd01b78)==0);
    assert(m.get<std::uint8_t>(module2+0xd01b78)==1);

    m=fixture();a=m.access();SJZNativeAimConfig pending;
    assert(pending.tick(a,1,0x200000000,module,c)==SJZNativeAimState::Configured);
    auto unavailable=a;unavailable.write=nullptr;
    assert(pending.stop(unavailable,1)==SJZNativeAimState::RestorePending);
    assert(pending.active() && m.get<std::uint8_t>(module+0xd01b78)==1);
    assert(pending.stop(a,1)==SJZNativeAimState::Restored);
    assert(!pending.active() && m.get<std::uint8_t>(module+0xd01b78)==0);

    m=fixture();a=m.access();SJZNativeAimConfig failed;
    m.failWrite=module+0xc38394;
    assert(failed.tick(a,1,0x200000000,module,c)==SJZNativeAimState::WriteFailed);
    assert(m.get<std::int32_t>(module+0xc38390)==1); // Changed mode was rolled back.
    assert(m.get<std::uint8_t>(module+0xd01b78)==0);
    m.failWrite=0;
    assert(failed.tick(a,1,0x200000000,module,c)==SJZNativeAimState::WriteFailed);
    assert(failed.stop(a,1)==SJZNativeAimState::Idle);
}
