#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EVENT_TRACK_POWER_ON = 0,
    EVENT_TRACK_POWER_OFF,
    EVENT_TRACK_FAULT,
    EVENT_TRACK_OVERLOAD,
    EVENT_TRACK_NORMAL,
    EVENT_COUNT,
} event_type_t;

typedef void (*event_handler_t)(event_type_t event, void *data);

#define MAX_SUBSCRIBERS 8

typedef struct {
    event_handler_t handlers[EVENT_COUNT][MAX_SUBSCRIBERS];
    uint8_t         handler_count[EVENT_COUNT];
} event_bus_t;

void event_bus_init(event_bus_t *bus);
bool event_bus_subscribe(event_bus_t *bus, event_type_t event, event_handler_t handler);
void event_bus_publish(event_bus_t *bus, event_type_t event, void *data);

#endif // EVENT_BUS_H
