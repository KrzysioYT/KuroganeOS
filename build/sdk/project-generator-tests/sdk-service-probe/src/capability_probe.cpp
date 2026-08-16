#include <stdio.h>
#include <kurogane/kurogane.h>

int main() {
    printf("sdk-service-probe service started\n");
    for (unsigned heartbeat = 0; heartbeat < 10; ++heartbeat) {
        (void)kuro_sleep(10);
    }
    return 0;
}
