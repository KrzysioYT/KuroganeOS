#include "../common/dev_credential.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    const uint64_t expected = UINT64_C(0x57FF0F93FF125DDB);

    assert(ku_dev_credential_hash("user", "secret") == expected);
    assert(ku_dev_credential_verify("user", "secret", expected));
    assert(!ku_dev_credential_verify("user", "bad", expected));
    assert(!ku_dev_credential_verify("other", "secret", expected));
    assert(ku_dev_credential_hash("test-user", "S3cret!") ==
           UINT64_C(0x5DCA5E80E10275D7));

    puts("dev credential compatibility tests: PASS");
    return 0;
}
