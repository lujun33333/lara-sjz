#include "sjz/CnAimMath.h"
#include <cassert>
#include <cmath>
#include <limits>

void near(float a,float b) { assert(std::fabs(a-b)<.001f); }
int main() {
    // Values from bounded execution of cn 0x1dd67c on synthetic weapon IDs.
    struct Row { std::uint64_t id;float speed; };
    constexpr Row rows[]={
        {18010000001,575},{18010000006,525},{18010000008,575},{18010000010,500},
        {18010000012,450},{18010000013,575},{18010000014,575},{18010000016,630},
        {18010000018,575},{18010000021,630},{18010000023,630},{18010000024,630},
        {18010000031,575},{18010000037,330},{18020000001,450},{18020000002,450},
        {18020000003,500},{18020000004,450},{18020000005,500},{18020000008,330},
        {18020000009,500},{18020000010,450},{18030000001,450},{18030000002,330},
        {18030000003,450},{18040000001,575},{18040000002,630},{18050000002,650},
        {18050000003,330},{18050000004,500},{18050000005,575},{18050000006,575},
        {18050000007,550},{18060000007,650},{18060000008,550},{18060000009,650}
    };
    for(auto row:rows) near(CnBulletSpeed(row.id),row.speed);
    for(auto id:{0ULL,18010000000ULL,18010000002ULL,18030000004ULL,18070000001ULL})
        near(CnBulletSpeed(id),750);
    OwnRotation r;
    assert(CnNormalizeAimDelta({190,-190,370},r));
    near(r.pitch,-89);near(r.yaw,170);near(r.roll,0);
    assert(CnNormalizeAimDelta({-181,181,-540},r));
    near(r.pitch,89);near(r.yaw,-179);
    assert(CnNormalizeAimDelta({720,-720,1080},r));
    near(r.pitch,89);near(r.yaw,-360); // Original single-turn wrap.
    assert(CnNormalizeAimDelta({180,-180,5},r));
    near(r.pitch,89);near(r.yaw,-180);
    assert(CnAimAnglesToTarget({},{1,1,1},r));
    near(r.pitch,35.2643897f);near(r.yaw,45);
    assert(CnAimAnglesToTarget({},{-1,0,0},r));near(r.yaw,180);
    assert(CnBlendAimAngles({80,170,4},{-80,-170,0},.35f,r));
    near(r.pitch,48.85f);near(r.yaw,177);near(r.roll,4);
    assert(CnBlendAimAngles({80,170,4},{-80,-170,0},0,r));
    near(r.pitch,80);near(r.yaw,170);near(r.roll,4);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!CnAimAnglesToTarget({},{},r));
    assert(!CnAimAnglesToTarget({nan,0,0},{},r));
    assert(!CnNormalizeAimDelta({nan,0,0},r));
    assert(!CnBlendAimAngles({},{},nan,r));
    assert(!CnBlendAimAngles({},{},-1,r));
    assert(!CnBlendAimAngles({},{},1.01f,r));
    // Bounded execution of cn 0x22cd30 sorted these conflicting keys.
    const CnAimCandidateKey head{100,0},chest{1,2};
    assert(CnAimCandidateBefore(head,chest,0));
    assert(CnAimCandidateBefore(chest,head,2));
    assert(CnAimCandidateBefore(chest,head,13));
    assert(!CnAimCandidateBefore(head,chest,13));
    assert(!CnAimCandidateBefore(chest,chest,2));
    assert(CnAimCandidateBefore({1,2},{100,2},2));
    assert(CnAimCandidateInRadius({99.99f,13},100));
    assert(!CnAimCandidateInRadius({100,13},100));
    assert(!CnAimCandidateInRadius({0,0},0));
    assert(!CnAimCandidateInRadius({nan,0},100));
    assert(!CnAimCandidateInRadius({1,18},100));
    // Original ordinary output instruction slices, executed on synthetic data.
    struct PredictionRow {
        OwnVector3 source,target,velocity;std::uint64_t id;float pitch,yaw;
    };
    const PredictionRow predictions[]={
        {{},{1000,0,0},{100,100,900},18010000006,0,.1089272127f},
        {{},{333,50,80},{100,-200,900},18020000002,13.3407021f,8.21817207f},
        {{10000,20000,30000},{10333,20050,30080},{100,-200,900},18020000002,
            13.3407021f,8.21817207f},
        {{100000000,100000000,100000000},{100000016,100000032,100000008},
            {75,25,0},18010000006,12.5681171f,63.2649345f},
        {{1,2,3},{-200,500,-700},{-200,50,-900},0,-52.5481987f,112.192619f}
    };
    for(auto row:predictions) {
        assert(CnOrdinaryAimAngles(row.source,row.target,row.velocity,row.id,r));
        near(r.pitch,row.pitch);near(r.yaw,row.yaw);
    }
    OwnRotation sameZ;
    assert(CnOrdinaryAimAngles({},{333,50,80},{100,-200,0},18020000002,sameZ));
    near(sameZ.pitch,predictions[1].pitch); // Ordinary output does not lead Z.
    assert(!CnOrdinaryAimAngles({},{1,1,1},{nan,0,0},0,r));
}
