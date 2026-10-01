#include "../lara/kexploit/sjz/SJZHUDEntityAgePolicy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(sjzhud_entity_fresh(400,100,100.049,450));
    assert(!sjzhud_entity_fresh(400,100,100.051,450));
    // A new batch preserves the old source instant: it cannot extend lifetime.
    const double source=99.600;
    const double old_finish=100,new_finish=100.030;
    const double old_age=(old_finish-source)*1000;
    const double new_age=(new_finish-source)*1000;
    assert(fabs(sjzhud_entity_deadline(old_age,old_finish,450)-
                sjzhud_entity_deadline(new_age,new_finish,450))<1e-9);
    assert(!sjzhud_entity_fresh(new_age,new_finish,100.051,450));
    assert(sjzhud_entity_fresh(20,new_finish,100.051,450));
    assert(sjzhud_entity_deadline(400,100,450)<sjzhud_entity_deadline(20,100,450));
    assert(!sjzhud_entity_fresh(-1,100,100,450));
    assert(!sjzhud_entity_fresh(NAN,100,100,450));
    assert(!sjzhud_entity_fresh(0,100,99,450));
    assert(!sjzhud_entity_fresh(0,INFINITY,100,450));
    assert(!sjzhud_entity_fresh(0,100,100,0));
    puts("PASS: independent entity expiry and publication cannot renew old samples");
}
