#include "../lara/kexploit/sjz/SJZHUDSourceAgePolicy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const double start=16, finish=16.125, receipt=16.25, ttl=.45;
    assert(sjzhud_source_fresh(start,finish,receipt,receipt,ttl));
    assert(sjzhud_source_fresh(start,finish,receipt,nextafter(start+ttl,start),ttl));
    assert(!sjzhud_source_fresh(start,finish,receipt,nextafter(start+ttl,INFINITY),ttl));
    assert(!sjzhud_source_fresh(start,start+1,start+1,start+1,ttl));
    assert(!sjzhud_source_fresh(start,finish,start+.5,start+.5,ttl));
    assert(!sjzhud_source_fresh(start,finish,receipt,receipt-.01,ttl));
    assert(!sjzhud_source_fresh(start,start-.01,receipt,receipt,ttl));
    assert(!sjzhud_source_fresh(start,finish,finish-.01,receipt,ttl));
    assert(!sjzhud_source_fresh(NAN,finish,receipt,receipt,ttl));
    assert(!sjzhud_source_fresh(start,INFINITY,receipt,receipt,ttl));
    assert(!sjzhud_source_fresh(start,finish,receipt,receipt,0));
    puts("PASS: source lifetime is never renewed by delivery or slow downstream work");
}
