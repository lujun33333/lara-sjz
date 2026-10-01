#include "sjz/OwnArmor.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t fail=0, change=0;
    int reads=0;
    template<class T> void put(std::uintptr_t p,T value) {
        const auto* b=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i) bytes[p+i]=b[i];
    }
    static bool read(void* context,std::uintptr_t p,void* out,std::size_t n) {
        auto& m=*static_cast<Memory*>(context);
        if(p==m.fail || (p==m.change && ++m.reads==2)) return false;
        auto* b=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<n;++i) {
            auto it=m.bytes.find(p+i); if(it==m.bytes.end()) return false;
            b[i]=it->second;
        }
        return true;
    }
};
constexpr std::uintptr_t actor=0x200000000,component=actor+0x10000,array=actor+0x20000;
Memory fixture(const std::vector<std::uint64_t>& ids) {
    Memory m;
    m.put(actor+0x24e0,component);m.put(component+0x1f8,array);
    m.put(component+0x200,std::int32_t(ids.size()));
    // Only the consumed ID bytes are present: no capacity or full record read.
    for(std::size_t i=0;i<ids.size();++i) m.put(array+i*24+8,ids[i]);
    return m;
}
int main() {
    auto check=[](std::vector<std::uint64_t> ids,int helmet,int body) {
        auto m=fixture(ids);OwnArmorValues out;
        assert(OwnReadArmor({&m,Memory::read},actor,out));
        assert(out.helmet==helmet&&out.body==body);
    };
    check({11010001000,11050006000},1,6);
    check({11010006000,11010001000,11050002000,11050005000},1,5);
    check({11010001000,11010000000},0,0);
    check({11009999999,11020000000,11049999999,11060000000,1000895},0,0);
    check({11019999999,11059999999},9,9);
    check({11010895000,11050009999},5,9);
    check({},0,0);
    assert(OwnArmorGrade(11010001001)==1);
    assert(OwnArmorGrade(11010011000)==1);
    assert(OwnArmorGrade(11010099000)==9);
    for(auto p:{actor+0x24e0,component+0x1f8,component+0x200,array+8,array+32}) {
        auto m=fixture({11010001000,11050006000});m.fail=p;OwnArmorValues out;
        assert(!OwnReadArmor({&m,Memory::read},actor,out));assert(out.helmet==-1&&out.body==-1);
    }
    for(auto p:{actor+0x24e0,component+0x1f8,component+0x200}) {
        auto m=fixture({11010001000});m.change=p;OwnArmorValues out;
        assert(!OwnReadArmor({&m,Memory::read},actor,out));assert(out.helmet==-1);
    }
    for(auto count:{-1,4097}) {
        auto m=fixture({});m.put(component+0x200,std::int32_t(count));OwnArmorValues out;
        assert(!OwnReadArmor({&m,Memory::read},actor,out));
    }
    auto m=fixture({11010001000});m.put(component+0x1f8,UINT64_MAX);OwnArmorValues out;
    assert(!OwnReadArmor({&m,Memory::read},actor,out));
    std::cout<<"PASS: cn armor families, 24-byte ID records, full-width grade, order and stable reads\n";
}
