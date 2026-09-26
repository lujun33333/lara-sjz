#include "sjz/SJZTouchAimPolicy.h"
#include <cassert>

int main() {
    SJZTouchAimPolicy policy;
    SJZTouchObservation o{true,false,1,0x200000000,0x300000000,
                          1000,500,500,650,250,0,0,.5f};
    assert(policy.tick(o).phase==SJZTouchPhase::WaitingReady);
    o.ready=true;
    auto d=policy.tick(o);
    assert(d.action==SJZTouchAction::Begin && d.phase==SJZTouchPhase::BeginPending);
    policy.complete(true);
    d=policy.tick(o);
    assert(d.action==SJZTouchAction::Move && d.phase==SJZTouchPhase::ProbeXPending);
    policy.complete(true);
    o.yaw=.6f;
    d=policy.tick(o);
    assert(d.action==SJZTouchAction::Move && d.phase==SJZTouchPhase::ProbeYPending);
    policy.complete(true);
    o.pitch=.6f;
    d=policy.tick(o);
    assert(d.action==SJZTouchAction::Move && d.phase==SJZTouchPhase::MovePending);
    policy.complete(true);
    o.yaw+=.6f;
    d=policy.tick(o);
    assert(d.phase==SJZTouchPhase::Active||d.phase==SJZTouchPhase::MovePending);

    o.target++;
    d=policy.tick(o);
    assert(d.phase==SJZTouchPhase::Failed && d.action==SJZTouchAction::End);
    policy.released();
    assert(policy.tick(o).action==SJZTouchAction::None);
    o.enabled=false;
    assert(policy.tick(o).phase==SJZTouchPhase::Off);

    o.enabled=true;o.target--;o.yaw=o.pitch=0;
    d=policy.tick(o);assert(d.action==SJZTouchAction::Begin);
    policy.complete(true);d=policy.tick(o);assert(d.action==SJZTouchAction::Move);
    policy.complete(true);
    for(int i=0;i<8;++i)
        assert(policy.tick(o).phase==SJZTouchPhase::ProbeXObserve);
    d=policy.tick(o);
    assert(d.phase==SJZTouchPhase::Failed && d.action==SJZTouchAction::End);

    policy.reset();o.ready=false;
    for(int i=0;i<120;++i)
        assert(policy.tick(o).phase==SJZTouchPhase::WaitingReady);
    assert(policy.tick(o).phase==SJZTouchPhase::Failed);
}
