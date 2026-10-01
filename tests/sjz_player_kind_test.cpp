#include "sjz/CnPlayerKind.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

struct Fixture {
    std::map<std::uintptr_t,std::uint8_t> bytes;
    std::map<std::uintptr_t,int> reads;
    std::uintptr_t fail=0, mutate=0;
    std::vector<std::uint8_t> replacement;
    template<class T> void put(std::uintptr_t address,const T& value) {
        const auto* data=reinterpret_cast<const std::uint8_t*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i) bytes[address+i]=data[i];
    }
    template<class T> void changeAtSecondRead(std::uintptr_t address,const T& value) {
        mutate=address;
        const auto* data=reinterpret_cast<const std::uint8_t*>(&value);
        replacement.assign(data,data+sizeof(value));
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) {
        auto& fixture=*static_cast<Fixture*>(context);
        const int count=++fixture.reads[address];
        if(address==fixture.fail) return false;
        if(address==fixture.mutate && count==2)
            for(std::size_t i=0;i<fixture.replacement.size();++i)
                fixture.bytes[address+i]=fixture.replacement[i];
        auto* data=static_cast<std::uint8_t*>(output);
        for(std::size_t i=0;i<size;++i) {
            const auto value=fixture.bytes.find(address+i);
            if(value==fixture.bytes.end()) return false;
            data[i]=value->second;
        }
        return true;
    }
};
constexpr std::uintptr_t actor=0x200000000,state=actor+0x10000,klass=state+0x10000;
static Fixture validFixture() {
    Fixture fixture;
    fixture.put(actor+8,klass);
    fixture.put(actor+0x1c,std::uint32_t(77));
    fixture.put(actor+0x24,std::int32_t(123));
    fixture.put(actor+0x18,std::uint32_t(0));
    fixture.put(actor+0x390,state);
    fixture.put(state+0x39e,std::uint8_t(1));
    return fixture;
}
static CnPlayerKindSample sample(Fixture& fixture) {
    return CnReadBotDisplayFlag({&fixture,Fixture::read},actor);
}
int main() {
    for(std::uint8_t raw:{0,1,2,8,0x20,0x80,0xff}) {
        auto fixture=validFixture();fixture.put(state+0x39e,raw);
        const auto result=sample(fixture);
        assert(result.source==CnPlayerKindSource::StateFlagsByte && result.state==state);
        assert(result.rawByte==raw && result.botDisplayFlag==(raw==0));
        assert(result.actor==actor && result.actorClass==klass && result.actorName==77 && result.objectIndex==123);
    }
    auto fixture=validFixture();fixture.put(actor+0x390,std::uintptr_t(0));
    auto result=sample(fixture);
    assert(result.source==CnPlayerKindSource::NoPlayerState && result.state==0 && result.botDisplayFlag);
    assert(fixture.reads[0x39e]==0);
    fixture=validFixture();fixture.fail=actor+0x390;
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.fail=state+0x39e;
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.put(actor+0x390,std::uintptr_t(0x123));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(actor+0x390,state+0x1000);
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.put(actor+0x390,std::uintptr_t(0));
    fixture.changeAtSecondRead(actor+0x390,state);
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(actor+8,klass+0x1000);
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(actor+0x1c,std::uint32_t(78));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(actor+0x24,std::int32_t(124));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(actor+0x18,std::uint32_t(0x100));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.changeAtSecondRead(state+0x39e,std::uint8_t(8));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    fixture=validFixture();fixture.put(actor+0x18,std::uint32_t(0x18000));
    assert(sample(fixture).source==CnPlayerKindSource::Unavailable);
    assert(CnReadBotDisplayFlag({},actor).source==CnPlayerKindSource::Unavailable);
    assert(CnReadBotDisplayFlag({},0).source==CnPlayerKindSource::Unavailable);
    std::cout<<"PASS: cn literal whole-byte display flag, absent-state provenance and stale/read failure rejection\n";
}
