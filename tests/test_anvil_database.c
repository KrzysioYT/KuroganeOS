#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "../userspace/gui/anvil/database_compaction.h"

int main(void) {
    static const char database[] =
        "kurofetch|1.0.2|/apps/kurofetch\r\n"
        "net-utils|1.2.8|/apps/net-utils\n"
        "kurofetch|1.0.1|/apps/kurofetch\n";
    static const char replacement[] =
        "kurofetch|1.0.3|/apps/kurofetch\n";
    static const char expected[] =
        "net-utils|1.2.8|/apps/net-utils\n"
        "kurofetch|1.0.3|/apps/kurofetch\n";
    char output[256];
    size_t output_size = 0U;

    assert(anvil_database_compact(
        database, "kurofetch", replacement,
        output, sizeof(output), &output_size));
    assert(output_size == strlen(expected));
    assert(strcmp(output, expected) == 0);

    output_size = 99U;
    assert(!anvil_database_compact(
        database, "kurofetch", replacement,
        output, 16U, &output_size));
    assert(output_size == 99U);
    return 0;
}
