# simple-dcc

A simple LCC-connected DCC command station, because sometimes a tool just needs to do the one thing.

## Features

- DCC signal generation
- RailCom cutout support

## Current Reference Hardware

- **Board:** Adafruit Metro RP2350 (Raspberry Pi Pico 2)
- **Motor Shield:** EX-MotorShield8874
- **Protocol:** LCC/OpenLCB via GridConnect CAN frames over USB CDC

## Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) (2.x)
- [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) with RP2350 community port
- ARM GCC toolchain with newlib (e.g., [ARM GNU Toolchain](https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain))
- CMake 3.13+
- [picotool](https://github.com/raspberrypi/picotool) (for UF2 generation)

### macOS (Homebrew)

```bash
brew install cmake
brew install --cask gcc-arm-embedded
brew install picotool
```

### Clone dependencies

```bash
# Pico SDK
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk && git submodule update --init && cd ..

# FreeRTOS Kernel (with RP2350 community port)
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
cd FreeRTOS-Kernel && git submodule update --init && cd ..
```

## Build

Set environment variables pointing to the SDK and FreeRTOS Kernel:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel
```

If the ARM toolchain is not on your PATH, also set:

```bash
export PICO_TOOLCHAIN_PATH=/path/to/arm-none-eabi/bin
```

Then build:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Output: `build/simple-dcc.uf2`

## Flash

1. Hold BOOTSEL on the Pico 2 and connect USB
2. Drag `simple-dcc.uf2` to the mounted `RPI-RP2` drive

Or use picotool:

```bash
picotool load build/simple-dcc.uf2
picotool reboot
```

## Host-side Tests

Unit tests run on the host (no hardware required):

```bash
cd test
mkdir -p build && cd build
cmake .. && make && ctest
```

## License

MIT
