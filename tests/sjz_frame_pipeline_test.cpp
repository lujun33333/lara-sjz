// Reuse the exact-version fake memory fixture and fake transport. Its original
// suite is compiled but not invoked; no process/device memory is accessed.
#define main cn_alignment_fixture_unused_main
#define sjz_read fixture_sjz_read
#define sjz_read_phase_fresh fixture_sjz_read_phase_fresh
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "sjz_cn_alignment_collector_test.cpp"
#pragma GCC diagnostic pop
#undef sjz_read
#undef sjz_read_phase_fresh
#undef main

#include "sjz/SJZHUDSourceAgePolicy.h"
#include <chrono>
#include <thread>
#include <array>

using PipelineClock = std::chrono::steady_clock;
static bool slowNextRead=false, cancelNextRead=false;
static int aimReadCalls=0;
static uint64_t epoch=42;

extern "C" long sjz_read(uint64_t address,void* out,size_t size) {
    ++aimReadCalls;
    if(cancelNextRead) {
        cancelNextRead=false;
        sjzesp_cancel_epoch(++epoch);
    }
    if(slowNextRead) {
        slowNextRead=false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return fixture_sjz_read(address,out,size);
}
extern "C" long sjz_read_phase_fresh(uint64_t address,void* out,size_t size,
                                      uint64_t*,size_t*,size_t) {
    return sjz_read(address,out,size);
}
static double seconds() {
    return std::chrono::duration<double>(PipelineClock::now().time_since_epoch()).count();
}

int main() {
    auto memory=fixture();activeMemory=&memory;
    // Local inputs needed by the actual planner/writer, kept entirely simulated.
    memory.put(pawn+0x1788,weapon);
    memory.put(weapon+0x838,uint64_t(18010000006));
    memory.put(actor+0x180,component);
    memory.put(pawn+0x180,component+0x1000);
    memory.put(component+0x1168,OwnVector3{});
    memory.put(controller+0x3d8,OwnRotation{});
    sjzesp_config_t config{SJZ_DEFAULT_FLAGS|SJZ_AIM_ENABLED,300,0,1.5f,13,.5f,1000.f,0,0};
    std::array<sjzesp_item_t,4> output{};
    transportWrite=true;sjzesp_reset();sjzesp_cancel_epoch(epoch);
    auto collect=[&] {
        return sjzesp_collect(base,1000,500,&config,output.data(),int(output.size()),epoch);
    };
    // Production live discovery is bounded and may warm up before publishing.
    int count=0;
    for(int i=0;i<8 && !count;++i) count=collect();
    assert(count==1);
    // Prove the fake scene can reach a successful writer; later no-write checks
    // cannot pass merely because every plan had an unusable target.
    const int baselineWrites=writeCalls;
    sjzesp_aim(base,1000,500,&config,output.data(),count,epoch,sjzesp_stats().sampleGeneration);
    assert(writeCalls>baselineWrites && sjzesp_stats().aimWriteStatusCode==4);

    // Display projection eligibility is independent of the complete world pose.
    // Clear a screen-only point while retaining all real collected world data.
    count=collect();assert(count==1);
    constexpr uint32_t fullWorldMask=(uint32_t(1u)<<SJZ_BONE_POINTS)-1u;
    assert(output[0].worldBoneMask==fullWorldMask);
    output[0].boneMask&=~uint32_t(1u<<17);
    assert(output[0].boneMask!=fullWorldMask);
    const int partialDisplayWrites=writeCalls;
    sjzesp_aim(base,1000,500,&config,output.data(),count,epoch,sjzesp_stats().sampleGeneration);
    assert(writeCalls>partialDisplayWrites && sjzesp_stats().aimWriteStatusCode==4);

    // Conversely, complete screen coordinates must not hide an incomplete
    // world pose. The production published-frame selector must reject it.
    count=collect();assert(count==1 && output[0].worldBoneMask==fullWorldMask);
    output[0].worldBoneMask&=~uint32_t(1u<<17);
    output[0].boneMask=fullWorldMask;
    const int incompleteWorldWrites=writeCalls;
    sjzesp_aim(base,1000,500,&config,output.data(),count,epoch,sjzesp_stats().sampleGeneration);
    assert(writeCalls==incompleteWorldWrites);
    assert(sjzesp_stats().aimStatusCode==static_cast<int32_t>(SJZAimStatus::NoTarget));
    assert(sjzesp_stats().aimBoneReject==1);

    const double started=seconds();
    count=collect();
    const double finished=seconds();
    assert(count==1 && finished-started<.45);
    const uint64_t sample=sjzesp_stats().sampleGeneration;
    const auto published=output; // Own immutable copy at collection completion.
    const double publishedFinished=finished;
    assert(sjzhud_source_fresh(started,finished,finished,finished,.45));
    const int writesBefore=writeCalls;
    aimReadCalls=0;slowNextRead=true;
    const auto aimStarted=PipelineClock::now();
    sjzesp_aim(base,1000,500,&config,output.data(),count,epoch,sample);
    const double aimElapsed=std::chrono::duration<double>(PipelineClock::now()-aimStarted).count();
    assert(!slowNextRead && aimElapsed>=.9); // Slow fake read actually ran.
    assert(aimReadCalls<=2); // Deadline stops further remote work after that read.
    assert(writeCalls==writesBefore);
    assert(std::memcmp(published.data(),output.data(),sizeof(output))==0);
    assert(finished==publishedFinished); // Aim must not rewrite the source end.
    assert(!sjzhud_source_fresh(started,finished,seconds(),seconds(),.45));
    assert(sjzesp_stats().aimWriteStatusCode!=4); // Never report Applied on timeout.
    assert(sjzesp_stats().aimStatusCode==static_cast<int32_t>(SJZAimStatus::TimedOut));
    assert(sjzesp_stats().aimMaxReadMs>=900);
    assert(sjzesp_stats().aimBudgetOvershootMs>=800);

    // Consumed samples cannot be replayed.
    const int readsBefore=aimReadCalls;
    sjzesp_aim(base,1000,500,&config,output.data(),count,epoch,sample);
    assert(aimReadCalls==readsBefore && writeCalls==writesBefore);
    // The blocking read cannot be interrupted, but the budget prevents a whole
    // sequence of slow reads. The next collection gets a fresh token, not replay.
    const double nextSourceStarted=seconds();
    count=collect();
    const double nextSourceFinished=seconds();
    assert(count==1 && nextSourceFinished-nextSourceStarted<.45);
    assert(sjzesp_stats().sampleGeneration!=sample);
    assert(nextSourceFinished-finished>=.9);
    assert(nextSourceFinished-finished<2.0);

    auto cancelledWithoutWrite=[&](sjzesp_config_t invalid,bool readonly,bool detach) {
        config.flags|=SJZ_AIM_ENABLED;
        const int n=collect();assert(n==1);
        const uint64_t token=sjzesp_stats().sampleGeneration;
        const int before=writeCalls;
        transportWrite=!readonly;
        if(detach) sjzesp_cancel_epoch(++epoch);
        sjzesp_aim(base,1000,500,&invalid,output.data(),n,detach?epoch-1:epoch,token);
        assert(writeCalls==before);
        transportWrite=true;
    };
    auto zero=config;zero.aimSpeed=0;cancelledWithoutWrite(zero,false,false);
    zero=config;zero.aimRadius=0;cancelledWithoutWrite(zero,false,false);
    auto off=config;off.flags&=~SJZ_AIM_ENABLED;cancelledWithoutWrite(off,false,false);
    cancelledWithoutWrite(config,true,false);
    cancelledWithoutWrite(config,false,true);

    count=collect();assert(count==1);
    const uint64_t detachToken=sjzesp_stats().sampleGeneration;
    const uint64_t oldEpoch=epoch;
    const int beforeDetachWrite=writeCalls;
    cancelNextRead=true;
    sjzesp_aim(base,1000,500,&config,output.data(),count,oldEpoch,detachToken);
    assert(!cancelNextRead && writeCalls==beforeDetachWrite);

    // A collection itself taking >450ms is rejected even with immediate delivery.
    assert(!sjzhud_source_fresh(10,10.501,10.501,10.501,.45));
    std::cout << "PASS: split collect, immutable publication, slow Aim budget, epoch/config cancellation\n";
}
