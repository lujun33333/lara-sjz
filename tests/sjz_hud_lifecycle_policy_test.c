#include "../lara/kexploit/sjz/SJZHUDLifecyclePolicy.h"
#include "../lara/kexploit/SJZHUDBridge.h"

#include <assert.h>
#include <stddef.h>

static void verify_outcome(bool menu, bool draw, bool ready)
{
    sjzhud_hosting_outcome_t outcome = sjzhud_hosting_outcome(menu, draw);
    assert(outcome.menuRegistered == menu);
    assert(outcome.drawRegistered == draw);
    assert(outcome.ready == ready);
}

int main(void)
{
    const bool applicationEvents[] = {true, false, false, true, false, true};
    const sjzhud_render_backend_t expectedBackends[] = {
        SJZHUDRenderBackendMetal,
        SJZHUDRenderBackendCoreAnimation,
        SJZHUDRenderBackendCoreAnimation,
        SJZHUDRenderBackendMetal,
        SJZHUDRenderBackendCoreAnimation,
        SJZHUDRenderBackendMetal,
    };
    for (size_t index = 0;
         index < sizeof(applicationEvents) / sizeof(applicationEvents[0]);
         ++index) {
        const bool active = applicationEvents[index];
        const sjzhud_render_transition_t transition =
            sjzhud_render_transition_for_application_active(active);
        assert(transition.backend == expectedBackends[index]);
        // Background CA publication still needs ticks. The old fixture
        // predates the retained-background-renderer policy in the header.
        assert(transition.foregroundTickEnabled);
        assert(transition.metalVisible == active);
        assert(transition.coreAnimationVisible == !active);
    }

    // AX keeps each successful side independently; only TT is fully ready.
    verify_outcome(true, true, true);
    verify_outcome(true, false, false);
    verify_outcome(false, true, false);
    verify_outcome(false, false, false);

    bool terminationRequested = false;
    assert(sjzhud_try_begin_termination(&terminationRequested));
    assert(terminationRequested);
    assert(!sjzhud_try_begin_termination(&terminationRequested));
    sjzhud_reset_termination(&terminationRequested);
    assert(sjzhud_try_begin_termination(&terminationRequested));

    assert(sjzhud_remote_cleanup_status_succeeded(0));
    assert(!sjzhud_remote_cleanup_status_succeeded(-1));
    assert(!sjzhud_remote_cleanup_status_succeeded(1));

    for (unsigned int resultMask = 0; resultMask != 4; ++resultMask) {
        sjzhud_hosting_fields_t fields = {true, true, 0x22, 0x11, true};
        sjzhud_clear_hosting_fields(&fields,
                                   (resultMask & 1u) != 0,
                                   (resultMask & 2u) != 0);
        assert(!fields.menuController && !fields.drawController);
        assert(fields.menuContext == 0 && fields.drawContext == 0);
        assert(!fields.ready);
    }
    return 0;
}
