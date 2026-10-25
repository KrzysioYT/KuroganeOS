#include "../userspace/gui/files/paging.h"
#include <stdint.h>
#include <stdio.h>
int main(void) {
#define CHK(x) do { if (!(x)) { fprintf(stderr, "failed page test: %d\n", __LINE__); return 1; } } while (0)
    CHK(flux_files_wheel_page(0, 24, 6, -1) == 6);
    CHK(flux_files_wheel_page(0, 24, 6, -3) == 18);
    CHK(flux_files_wheel_page(6, 24, 6, -2) == 18);
    CHK(flux_files_wheel_page(18, 24, 6, -9) == 18);
    CHK(flux_files_wheel_page(18, 24, 6, 1) == 12);
    CHK(flux_files_wheel_page(18, 24, 6, 3) == 0);
    CHK(flux_files_wheel_page(12, 24, 6, INT32_MAX) == 0);
    CHK(flux_files_wheel_page(0, 24, 6, INT32_MIN) == 18);
    CHK(flux_files_wheel_page(6, 11, 6, -1) == 6);
    CHK(flux_files_wheel_page(0, 11, 6, -1) == 6);
    CHK(flux_files_wheel_page(0, 0, 6, -1) == 0);
    CHK(flux_files_wheel_page(9, 11, 0, -1) == 9);
    CHK(flux_files_wheel_page(6, 11, 6, 0) == 6);
    puts("Flux Files paging: PASS");
    return 0;
}
