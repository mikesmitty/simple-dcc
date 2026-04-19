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

## LCC protocol

- **Remote consist slaves — alias resolution.** `forward_to_consist` in `src/lcc/lcc_traction.c` silently skips any consist member whose node ID doesn't resolve to a locally-registered `lcc_node_t`. Fine for a standalone CS where every consist member is a local train proxy, but it breaks multi-CS consists and consists that include an external LCC-enabled loco. Three reasons it was deferred in M8:
  1. **Addressed messages are alias-keyed, not node-ID-keyed.** `lcc_node_send_addressed` in `src/lcc/lcc_node.c` bakes `dst_alias` into the 2-byte addressed header — we have the alias for local nodes via `lcc_node_t.alias`, but nothing maps a remote node ID to its current alias.
  2. **No alias-resolution machinery.** OpenMRN's iface transparently resolves `NodeHandle{id, alias}` by issuing `VERIFY_NODE_ID_GLOBAL` with the target node ID and waiting for `VERIFIED_NODE_ID`. We'd need an alias cache, a pending-forward queue, and retry/timeout state — separate from consist itself (~150 LOC).
  3. **Aliases rotate.** `CNSTFLAGS_ALIASVALID` lets the throttle supply the slave's alias at ATTACH time (bytes 9-10 of the ATTACH payload — currently ignored). A cached alias can go stale on conflict-driven re-allocation, so any cache also needs invalidation on AMR / RID-collision traffic.
