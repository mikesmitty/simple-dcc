# simple-dcc

C/FreeRTOS DCC command station for RP2350 (Pico 2) with LCC/OpenLCB over USB.

## Build

Requires `PICO_SDK_PATH` and `FREERTOS_KERNEL_PATH` environment variables.

```bash
mkdir build && cd build
cmake ..
make
```

Output: `build/simple-dcc.uf2` — drag to RP2350 in BOOTSEL mode.

## Host-side Tests

```bash
cd test && mkdir -p build && cd build
cmake .. && make && ctest
```

## Architecture

- **Target:** RP2350 (Pico 2), EX-MotorShield8874
- **RTOS:** FreeRTOS (single-core, 7 priorities, Heap4)
- **Protocol:** LCC/OpenLCB via GridConnect CAN frames over USB CDC
- **LCC library:** OpenLcbCLib (git submodule at `lib/OpenLcbCLib/`)

### FreeRTOS Tasks (by priority)

| Pri | Task | Responsibility |
|-----|------|----------------|
| 6 | task_wavegen | Feed PIO FIFO from priority queue |
| 5 | task_priority_queue | Dedup, reorder, dispatch packets |
| 4 | task_track_monitor | 1ms overcurrent/fault check |
| 3 | task_dcc_reminder | Cycle speed + function reminders |
| 2 | task_protocol | Run OpenLcb_run() loop |
| 1 | task_serial | USB CDC read, GridConnect parsing |

### Data Flow

```
USB CDC → task_serial → GridConnect parser → CAN RX → OpenLcbCLib
OpenLcbCLib traction callbacks → DCC engine → priority queue → wavegen → PIO
```

## Conventions

- C11, no C++ in application code
- Static allocation preferred (packet pool, loco state array)
- Conventional commits: `feat(scope):`, `fix(scope):`, `chore(scope):`
- MIT license
