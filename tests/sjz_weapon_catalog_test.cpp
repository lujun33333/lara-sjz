#include "sjz/OwnWeaponCatalog.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    // Reference expectations from cn's map, spanning every gun category and aliases.
    assert(OwnKnownWeaponName(830130847)=="CAR-15");
    assert(OwnKnownWeaponName(830130856)=="K437");
    assert(OwnKnownWeaponName(830130854)=="Teng Dragon");
    assert(OwnKnownWeaponName(830130853)=="ASVAl");
    assert(OwnKnownWeaponName(830130817)=="M416");
    assert(OwnKnownWeaponName(830130829)=="M416");
    assert(OwnKnownWeaponName(840130827)=="QCQ171");
    assert(OwnKnownWeaponName(840130825)=="Warrior");
    assert(OwnKnownWeaponName(840130821)=="Bison");
    assert(OwnKnownWeaponName(850130821)=="725");
    assert(OwnKnownWeaponName(860130824)=="LMG");
    assert(OwnKnownWeaponName(900130823)=="Rocket");
    assert(OwnKnownWeaponName(870130847)=="PSG-1");
    assert(OwnKnownWeaponName(880130827)=="AWM");
    assert(OwnKnownWeaponName(890130849)=="m1911");
    for(std::uint32_t id:{930130816u,920130842u,920130837u,920130825u,920130841u,
        920130836u,920130860u,920130839u,830130865u})
        assert(OwnKnownWeaponName(id)=="Dao");
    // Reading only the original 4 bytes must tolerate existing 64-bit fixtures.
    assert(OwnKnownWeaponName(18010000006ULL)=="AKM");
    assert(OwnKnownWeaponName((std::uint64_t(0xabcdef)<<32)|830130826u)=="AKS-74u");
    assert(OwnKnownWeaponName(0)=="0");
    assert(OwnKnownWeaponName(830130846)=="830130846");
    assert(OwnKnownWeaponName(std::numeric_limits<std::uint32_t>::max())=="-1");
    assert(OwnKnownWeaponName(0x80000000u)=="-2147483648");
    std::cout<<"PASS: cn weapon categories, map aliases, low-32 input and signed unknown fallback\n";
}
