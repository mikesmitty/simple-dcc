# simple-dcc

C/FreeRTOS DCC command station for RP2350 (Adafruit Metro RP2350) with LCC/OpenLCB over USB.

## Build

Requires `PICO_SDK_PATH` and `FREERTOS_KERNEL_PATH` environment variables.

```bash
make          # configures with Ninja on first run, then ninja -C build
make clean    # wipes build/
```

The root `Makefile` is a thin wrapper around `cmake -B build -G Ninja` + `ninja -C build`. Do not run `cmake ..` from inside `build/` — that silently switches the generator to Unix Makefiles.

Output: `build/simple-dcc.uf2` — drag to RP2350 in BOOTSEL mode.

## Host-side Tests

```bash
cd test && mkdir -p build && cd build
cmake .. && make && ctest
```

## Architecture

- **Target:** Adafruit Metro RP2350 (RP2350B: 48 GPIOs, ADC on GPIO 40-47), EX-MotorShield8874
- **Board config:** `boards/adafruit_metro_rp2350.h` (copied from upstream pico-sdk; 2.2.0 lacks it). Changing `PICO_BOARD` needs `make clean` — the cached value wins otherwise.
- **RTOS:** FreeRTOS (SMP dual-core, 7 priorities, Heap4)
- **Protocol:** LCC/OpenLCB via GridConnect CAN frames over USB CDC
- **LCC stack:** in-tree C implementation at `src/lcc/`

### FreeRTOS Tasks (by priority)

| Pri | Task | Responsibility |
|-----|------|----------------|
| 6 | task_wavegen | Feed PIO FIFO from priority queue |
| 5 | task_priority_queue | Dedup, reorder, dispatch packets |
| 4 | task_track_monitor | 1ms overcurrent/fault check |
| 3 | task_dcc_reminder | Cycle speed + function reminders |
| 2 | task_protocol | Pump LCC RX queue + 100 ms alias/flush tick |
| 2 | task_railcom | RailCom POM reads on the main, channel 1 address log |
| 1 | task_serial | USB CDC read, GridConnect parsing |
| 1 | task_servicemode | Programming-track CV read/write (Service Mode) |

### Data Flow

```
USB CDC → task_serial → GridConnect parser → CAN RX → src/lcc/
src/lcc/ traction callbacks → DCC engine → priority queue → wavegen → PIO
src/lcc/ memconfig 0xF8 on CS node → task_servicemode → PIO2 (prog track) → ACK via ADC
src/lcc/ memconfig 0xF8 on train node → task_railcom → POM read on main → RailCom reply on UART0
```

## Conventions

- C11, no C++ in application code
- Static allocation preferred (packet pool, loco state array)
- Conventional commits: `feat(scope):`, `fix(scope):`, `chore(scope):`
- MIT license
