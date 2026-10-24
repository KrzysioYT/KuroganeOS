#include <kurogane/flux_app_lifecycle.h>
#include <assert.h>
#include <stdint.h>
int main(void) {
    const uint64_t start = UINT64_C(200);
    const uint64_t expiry = start + KU_FLUX_APP_LAUNCH_TIMEOUT_TICKS;
    assert(!ku_flux_app_retry_eligible(start, start, 0));
    assert(!ku_flux_app_retry_eligible(start, expiry-1U, 0));
    assert(ku_flux_app_retry_eligible(start, expiry, 0));
    assert(!ku_flux_app_retry_eligible(start, expiry, 1));
    assert(!ku_flux_app_retry_eligible(start, start-1U, 0));
    assert(ku_flux_app_retry_eligible(0U, UINT64_MAX, 0));
    assert(!ku_flux_app_retry_eligible(UINT64_MAX-1U, 0U, 0));
    return 0;
}
