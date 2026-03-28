# Changelog

## [0.2.2](https://github.com/mikesmitty/simple-dcc/compare/v0.2.1...v0.2.2) (2026-03-28)


### Bug Fixes

* grant contents: write permission for firmware release upload ([54e9607](https://github.com/mikesmitty/simple-dcc/commit/54e9607c0eaf4abf0aee3d577e35aa1187f14b69))

## [0.2.1](https://github.com/mikesmitty/simple-dcc/compare/v0.2.0...v0.2.1) (2026-03-28)


### Bug Fixes

* update UF2 filename in release-please workflow and docs ([dc88377](https://github.com/mikesmitty/simple-dcc/commit/dc88377aba667e860743d8625fcd5ff1c66ba41b))

## [0.2.0](https://github.com/mikesmitty/simple-dcc/compare/v0.1.0...v0.2.0) (2026-03-28)


### Features

* initial DCC command station implementation for RP2350 ([51a1a3e](https://github.com/mikesmitty/simple-dcc/commit/51a1a3e291bf7251e1b63ce9211bb89135421ced))
* **lcc:** add CDI and RAM-backed config memory ([84c5253](https://github.com/mikesmitty/simple-dcc/commit/84c52531c8430d71324974ad091f8874198c3fe6))
* **protocol:** enable JMRI traction control via LCC ([#3](https://github.com/mikesmitty/simple-dcc/issues/3)) ([6901a33](https://github.com/mikesmitty/simple-dcc/commit/6901a3315cdda485e8c7062e0196ec2b3884b3dc))
* **rtos:** enable SMP dual-core and fix LCC enumeration for JMRI ([f4b6bca](https://github.com/mikesmitty/simple-dcc/commit/f4b6bcac892293a95af22b4aaa8e6e664d03285d))


### Bug Fixes

* **lcc:** save config to flash without stack overflow ([4beddfa](https://github.com/mikesmitty/simple-dcc/commit/4beddfad39f845a2a19196feff38fac4272e2591))
* **usb:** ensure USB CDC enumerates under FreeRTOS ([9d7ba1d](https://github.com/mikesmitty/simple-dcc/commit/9d7ba1de43e7ac5e8d4275312e6a708123a9a35e))


### Miscellaneous

* add Makefile for ease of building ([bfa9240](https://github.com/mikesmitty/simple-dcc/commit/bfa924021501fe3971e147e8785ed8d9b8a31211))
* remove .vscode from .gitignore ([cd6a8b2](https://github.com/mikesmitty/simple-dcc/commit/cd6a8b2c1ff14833aa66aebbc77f9fa9e2850ce4))


### Documentation

* clean up initial README ([c716c02](https://github.com/mikesmitty/simple-dcc/commit/c716c025fad44be236f18ee0025d8b45381f65f5))
