#ifndef KUROGANE_SDK_NETWORK_EVENTS_H
#define KUROGANE_SDK_NETWORK_EVENTS_H

#include <kurogane/event_broker.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Edge-triggered state-change topics emitted by /system/neteventd.
 * Consumers query ku_network_get_status() after receiving a signal. */
#define KU_NETWORK_EVENT_CHANGED "network.changed"
#define KU_NETWORK_EVENT_CHANGED_SIZE 15U
#define KU_NETWORK_EVENT_READY "network.ready"
#define KU_NETWORK_EVENT_READY_SIZE 13U
#define KU_NETWORK_EVENT_LINK "network.link"
#define KU_NETWORK_EVENT_LINK_SIZE 12U
#define KU_NETWORK_EVENT_ADDRESS "network.address"
#define KU_NETWORK_EVENT_ADDRESS_SIZE 15U
#define KU_NETWORK_EVENT_DNS "network.dns"
#define KU_NETWORK_EVENT_DNS_SIZE 11U

#ifdef __cplusplus
}
#endif
#endif
