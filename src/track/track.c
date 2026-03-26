#include "track/track.h"

void track_init(track_t *t, char id, track_mode_t mode, motor_t *motor) {
    t->id = id;
    t->mode = mode;
    t->motor = motor;
    t->power_on = false;
}

void track_set_power(track_t *t, bool on) {
    t->power_on = on;
    motor_set_power(t->motor, on);
}

bool track_is_powered(track_t *t) {
    return t->power_on && motor_is_on(t->motor);
}
