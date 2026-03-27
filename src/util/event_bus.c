#include "util/event_bus.h"
#include <string.h>

void event_bus_init(event_bus_t *bus) {
    memset(bus, 0, sizeof(*bus));
}

bool event_bus_subscribe(event_bus_t *bus, event_type_t event, event_handler_t handler) {
    if (event >= EVENT_COUNT) return false;
    uint8_t idx = bus->handler_count[event];
    if (idx >= MAX_SUBSCRIBERS) return false;

    bus->handlers[event][idx] = handler;
    bus->handler_count[event]++;
    return true;
}

void event_bus_publish(event_bus_t *bus, event_type_t event, void *data) {
    if (event >= EVENT_COUNT) return;
    for (uint8_t i = 0; i < bus->handler_count[event]; i++) {
        if (bus->handlers[event][i]) {
            bus->handlers[event][i](event, data);
        }
    }
}
