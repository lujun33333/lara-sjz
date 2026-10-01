#include "sjz/SJZMenuPolicy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

static void near(float actual, float expected, float tolerance=0.0001f) {
    assert(std::isfinite(actual));
    assert(std::fabs(actual-expected)<=tolerance);
}

int main() {
    // Dragging from either end must use the same 50..300 domain as readback.
    near(SJZMenuValueAtRatio(0,0.f),50.f);
    near(SJZMenuValueAtRatio(0,.25f),112.5f);
    near(SJZMenuValueAtRatio(0,.5f),175.f);
    near(SJZMenuValueAtRatio(0,1.f),300.f);
    near(SJZMenuValueAtRatio(0,-.2f),50.f);
    near(SJZMenuValueAtRatio(0,1.2f),300.f);
    float previous=49.f;
    for (int step=0;step<=1000;++step) {
        const float ratio=step/1000.f;
        const float dragged=SJZMenuValueAtRatio(0,ratio);
        assert(dragged>previous);
        near(SJZMenuClamp(0,dragged),dragged);
        near(SJZMenuRatio(0,dragged),ratio);
        previous=dragged;
    }
    near(SJZMenuClamp(0,25.f),50.f);
    near(SJZMenuClamp(0,1000.f),300.f);

    const float invalid[]={std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for (float value:invalid) {
        near(SJZMenuClamp(0,value),300.f);
        near(SJZMenuValueAtRatio(0,value),300.f);
        near(SJZMenuRatio(0,value),1.f);
        near(SJZMenuClamp(1,value),0.f);
        near(SJZMenuClamp(2,value),0.f);
        near(SJZMenuClamp(3,value),0.f);
    }

    // A floating threshold between grades 2 and 3 must retain its filtering meaning.
    const float threshold=SJZMenuValueAtRatio(1,.4f);
    near(threshold,2.4f);
    assert(2.f<threshold && 3.f>=threshold);
    near(SJZMenuClamp(1,threshold),2.4f);
    near(SJZMenuRatio(1,threshold),.4f);
    near(SJZMenuValueAtRatio(2,0.f),0.f);
    near(SJZMenuValueAtRatio(2,1.f),1.f);
    near(SJZMenuValueAtRatio(3,0.f),0.f);
    near(SJZMenuValueAtRatio(3,1.f),1000.f);

    // Existing persisted chest/head values keep their meanings after UI reordering.
    assert(SJZMenuStoredPartToUi(0)==1);
    assert(SJZMenuStoredPartToUi(1)==0);
    assert(SJZMenuStoredPartToUi(2)==2);
    assert(SJZMenuUiPartToStored(0)==1);
    assert(SJZMenuUiPartToStored(1)==0);
    for (int stored=0;stored<3;++stored)
        assert(SJZMenuUiPartToStored(SJZMenuStoredPartToUi(stored))==stored);
    assert(SJZMenuStoredPartToUi(-1)==-1);
    assert(SJZMenuUiPartToStored(3)==-1);
    near(SJZMenuClamp(-1,100.f),0.f);
    near(SJZMenuValueAtRatio(4,.5f),0.f);
    near(SJZMenuRatio(4,100.f),0.f);

    // Expected cn ABGR palette, including ungraded loot and container override.
    assert(SJZMenuLootColor(0,false)==0xffffffffu);
    assert(SJZMenuLootColor(1,false)==0xffffffffu);
    assert(SJZMenuLootColor(2,false)==0xff50ff50u);
    assert(SJZMenuLootColor(3,false)==0xffff7828u);
    assert(SJZMenuLootColor(4,false)==0xffff3c8cu);
    assert(SJZMenuLootColor(5,false)==0xff28dcffu);
    assert(SJZMenuLootColor(6,false)==0xff3c3cffu);
    for (int level=0;level<=6;++level)
        assert(SJZMenuLootColor(level,true)==0xffffffffu);
    std::cout<<"PASS: menu drag continuity, finite fallback, fractional quality, stored target compatibility and loot colors\n";
}
