# TODO

Follow-ups from the code review on 2026-04-18. The concurrency fixes and full-DCC-function-range extension are being handled separately.

## Correctness

- **Pin conflict landmine in `src/board_config.h`.** `PIN_LED = 23` collides with `PIN_SIGNAL_B = 23`. `PIN_LED` is not referenced today, so it compiles, but the first person who adds a heartbeat LED will stomp on Channel B's DCC output. Either delete the define or point it at `PICO_DEFAULT_LED_PIN`.

- **`pqueue_pop` decrements repeats before checking alloc** — `src/queue/priority_queue.c:74-85`. If `packet_alloc()` returns NULL (pool exhausted), the copy is skipped but `top->repeats` has already been decremented. The packet is sent fewer times than the caller asked for, silently. Fix: only decrement on successful alloc.

- **Pool vs. queue sizing.** `PACKET_POOL_SIZE = 64` and `PQUEUE_CAPACITY = 64` in `src/board_config.h` / `src/queue/priority_queue.h`. When the priority queue fills, every other allocator path (wavegen copies, new throttle commands, LCC-driven packets) starts failing silently. Either bump `PACKET_POOL_SIZE` to give headroom (e.g. `PQUEUE_CAPACITY + 16`) or cap the queue below the pool size.

## Cleanup / dead code

- **`event_bus` is compiled in and never used.** `event_bus_init(&event_bus)` is called in `src/main.c:139` but there are zero `event_bus_subscribe` / `event_bus_publish` call sites. Delete `src/util/event_bus.c` / `.h` and the init call, or wire it up to something real (e.g. track-power state transitions in `motor.c`).

- **`src/motor/profile.h` is dead.** `ADC_CURRENT_LIMIT_MAIN` / `ADC_CURRENT_LIMIT_PROG` are defined but not referenced — `motor_init` does the mA→ADC conversion inline via `ma_to_adc_units`. Delete the header and drop the `#include "motor/profile.h"` from `main.c`.

- **`track_monitor_params_t` is duplicated** in `src/main.c` and `src/motor/motor.c`. They happen to match today, but they're internal to each compilation unit, so a change to one won't catch the mismatch. Move the type to `src/motor/motor.h` and have both files include it.

- **`serial_init()` is a no-op wrapper.** `src/serial/serial.c:14-16` contains only a comment saying the real init lives in `main`. Either delete the function and its declaration, or move `stdio_init_all()` into it.

## Docs

- **`CLAUDE.md` drift.** It says "single-core, 7 priorities, Heap4" but `include/FreeRTOSConfig.h:7` sets `configNUMBER_OF_CORES = 2` (confirmed SMP by the `enable SMP dual-core` commit in CHANGELOG). Update the line to reflect dual-core SMP so future readers / agents don't make wrong assumptions about concurrency safety.
