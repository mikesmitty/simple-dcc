#ifndef TRACK_H
#define TRACK_H

#include <stdbool.h>
#include "motor/motor.h"

typedef enum {
    TRACK_MODE_MAIN = 0,
    TRACK_MODE_PROG,
} track_mode_t;

typedef struct {
    motor_t     *motor;
    track_mode_t mode;
    char         id;
    bool         power_on;
} track_t;

void track_init(track_t *t, char id, track_mode_t mode, motor_t *motor);
void track_set_power(track_t *t, bool on);
bool track_is_powered(track_t *t);

#endif // TRACK_H
