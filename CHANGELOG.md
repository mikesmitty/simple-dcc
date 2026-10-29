# Changelog

## [0.5.1](https://github.com/mikesmitty/simple-dcc/compare/v0.5.0...v0.5.1) (2026-10-29)


### Miscellaneous

* Update actions/checkout action to v7 ([#12](https://github.com/mikesmitty/simple-dcc/issues/12)) ([c0415a2](https://github.com/mikesmitty/simple-dcc/commit/c0415a26373ee50c68f5b3f7ef41f5e322d4c42e))
* Update googleapis/release-please-action action to v5 ([#13](https://github.com/mikesmitty/simple-dcc/issues/13)) ([34f4bcb](https://github.com/mikesmitty/simple-dcc/commit/34f4bcb92803c8680591b53f5988788ac8788807))

## [0.5.0](https://github.com/mikesmitty/simple-dcc/compare/v0.4.0...v0.5.0) (2026-10-29)


### Features

* **board:** target the Adafruit Metro RP2350 ([fc5d93b](https://github.com/mikesmitty/simple-dcc/commit/fc5d93bc36a92fabb69f0a6e42bca23696eb4d83))
* **dcc:** add POM byte-write over LCC traction CV space (0xF8) ([8e8b31a](https://github.com/mikesmitty/simple-dcc/commit/8e8b31a29e8e6d53667ae689c1eb9be006068d60))
* **dcc:** add SMP concurrency safety and F0-F68 function range ([77eeebc](https://github.com/mikesmitty/simple-dcc/commit/77eeebcd919e3b715905892de875d33a0bba0c39))
* **dcc:** build POM read packets ([547aa9e](https://github.com/mikesmitty/simple-dcc/commit/547aa9e9787decf681d3c978d7a52f8d112e73ee))
* **display:** add SSD1306 OLED with CDI-configurable I2C pins ([db35c56](https://github.com/mikesmitty/simple-dcc/commit/db35c56f29b0a0a9ef0903acdf9fb85645a7c75c))
* **hardware:** add RailCom detector shield ([d2eae8a](https://github.com/mikesmitty/simple-dcc/commit/d2eae8aa401089ff5cc4bbea3044036721b79414))
* **lcc:** add GridConnect interface skeleton (M2) ([30f24da](https://github.com/mikesmitty/simple-dcc/commit/30f24da19f1fb0cf1e4e7d357e288caf26d11173))
* **lcc:** add M3 single-node stack (alias + PIP/SNIP/AME) ([68f9428](https://github.com/mikesmitty/simple-dcc/commit/68f94281d14d4a4f681f2557f54c81b2b1abb810))
* **lcc:** add M4 events + memconfig stack ([2d57270](https://github.com/mikesmitty/simple-dcc/commit/2d572703dcdc28ae172b26f2d940858690c5b029))
* **lcc:** add M5 traction core (float16, traction MTI, test train) ([633e2a3](https://github.com/mikesmitty/simple-dcc/commit/633e2a39d83731535f2391ab897ea75503a03e5c))
* **lcc:** add M6 controller cfg + M7 train search ([8fb112b](https://github.com/mikesmitty/simple-dcc/commit/8fb112b5f22708103e88dc519ebfa880e6c1cf8f))
* **lcc:** add M8 consist config + forwarding ([6a484a9](https://github.com/mikesmitty/simple-dcc/commit/6a484a91deb52f440a5ac246b028ca8c934273fc))
* **lcc:** add M9 host tests + restore dynamic config updates ([167021f](https://github.com/mikesmitty/simple-dcc/commit/167021fff6eda335779f141217287bcaca70ca4d))
* **lcc:** answer Get Configuration Options ([b686d60](https://github.com/mikesmitty/simple-dcc/commit/b686d608c226230f3b211791f9d9d472f1d4cee1))
* **lcc:** let memory config read handlers defer requests ([34cbe68](https://github.com/mikesmitty/simple-dcc/commit/34cbe68c577d843159959fee1cc96b754bb95975))
* **lcc:** produce Is Train from train nodes ([0247c98](https://github.com/mikesmitty/simple-dcc/commit/0247c9893d8fcfdc4ce8937f7a691d089eb90849))
* **lcc:** read train CVs on the main through a host hook ([560e54c](https://github.com/mikesmitty/simple-dcc/commit/560e54cb22d4e2702ee47efe1540e6df5711b409))
* **lcc:** report track power state in emergency event replies ([0ce5619](https://github.com/mikesmitty/simple-dcc/commit/0ce5619bc3bc83046348a66ce35890c28775a219))
* **lcc:** send consist and long address instructions for CV writes on the main ([cc74ea2](https://github.com/mikesmitty/simple-dcc/commit/cc74ea28c586499e17ad36b8e919364dc5496136))
* **mcp2515:** add MCP2515 CAN as second LCC transport ([45e7b73](https://github.com/mikesmitty/simple-dcc/commit/45e7b73818f5cb9f3dd9f09ac3b19654a300c4ce))
* **railcom:** read CVs on the main over RailCom ([df0742d](https://github.com/mikesmitty/simple-dcc/commit/df0742de0032c312319220f80838fc7cc340ed7a))
* **servicemode:** add Service Mode CV programming for JMRI over LCC ([4b8460c](https://github.com/mikesmitty/simple-dcc/commit/4b8460c62e6c186238818d8060dbaf72c623d343))
* **wavegen:** report packets and cutout phases for RailCom ([279829c](https://github.com/mikesmitty/simple-dcc/commit/279829cfee61967cfab12604140600e4f7bd5b64))


### Bug Fixes

* **dcc:** honor throttle long-address flag for short addresses ([aecb096](https://github.com/mikesmitty/simple-dcc/commit/aecb096477e77f284344dfddf96bf694f8460768))
* **dcc:** keep trains stopped after an emergency stop ([43d61cb](https://github.com/mikesmitty/simple-dcc/commit/43d61cb69411c398457df276f48e63a2f33badb8))
* **display:** rename DISPLAY_W/H so they don't clash with the include guard ([3d6b5ce](https://github.com/mikesmitty/simple-dcc/commit/3d6b5ceec626c61b6db16dbd7e08028235242801))
* **lcc:** advertise Traction Control on train nodes ([5b4f946](https://github.com/mikesmitty/simple-dcc/commit/5b4f946ffc333ece4c391277477d25a740d97911))
* **lcc:** answer every memconfig write with a Write Reply ([4b3c570](https://github.com/mikesmitty/simple-dcc/commit/4b3c5703ceadc545f181b134e1c4b765d58a31c7))
* **lcc:** give the command station a locally allocated node ID ([bd2cf79](https://github.com/mikesmitty/simple-dcc/commit/bd2cf794db51fba6c1072831f9e9d661e242a3af))
* **lcc:** move saved fault pins to the shield's nFAULT outputs ([f4f8b95](https://github.com/mikesmitty/simple-dcc/commit/f4f8b95e8aa6d5474a7c51187cab6cd0761e2dca))
* **lcc:** reject memconfig requests that can't be served ([89542b1](https://github.com/mikesmitty/simple-dcc/commit/89542b1fcab84b3ffa2eccbad916f572f81f921d))
* **lcc:** report e-stop in bit 0 of the Query Speeds status ([95bd663](https://github.com/mikesmitty/simple-dcc/commit/95bd66312652b179a7bef2333b54a0c6199eebc4))
* **lcc:** send real datagram rejections and read-failure replies ([a135bbd](https://github.com/mikesmitty/simple-dcc/commit/a135bbdd282222c53a03fc5a1a9f171cd5693e2b))
* **lcc:** tell the old controller when another throttle takes a train ([5ea3cb7](https://github.com/mikesmitty/simple-dcc/commit/5ea3cb7a680a117f5b9cf089b0d774f65c2419ff))
* **lcc:** use well-known DCC-proxy prefix for train node IDs ([c6f2197](https://github.com/mikesmitty/simple-dcc/commit/c6f219744a04dfac6819e2de0e0c996029c58f91))
* **motor:** correct current-sense conversion, which was off by 2x ([4c6f431](https://github.com/mikesmitty/simple-dcc/commit/4c6f431d822920b3312935151be3554a63ff2d27))
* **motor:** report the measured current in track alerts ([5e89f5a](https://github.com/mikesmitty/simple-dcc/commit/5e89f5ad65a309b69cef01cdd8197fc703643a28))
* **motor:** stop the overload retry from undoing a power-off ([307f304](https://github.com/mikesmitty/simple-dcc/commit/307f30487fb98a62ddd9b04feb747a7a7a118a62))
* **servicemode:** follow DCC-EX for programming track power and limit ([3d8a4c2](https://github.com/mikesmitty/simple-dcc/commit/3d8a4c2274ec4cefc0061c521659de8c36c6a417))
* **servicemode:** refuse decoders that draw too much at idle ([ed0b5e4](https://github.com/mikesmitty/simple-dcc/commit/ed0b5e478c938aa0008a3420dc7cd2dd0ed9865c))
* **servicemode:** reject noisy ACKs instead of reading wrong values ([a73eb28](https://github.com/mikesmitty/simple-dcc/commit/a73eb284b9db4d0c8bd19a63e860e58042eb3993))
* **wavegen:** drive only the main signal pin ([0adaeab](https://github.com/mikesmitty/simple-dcc/commit/0adaeab17bfe232d5d2a325fb3a7cf4a8600b3b4))
* **wavegen:** free PIO program memory on reinit ([06beb94](https://github.com/mikesmitty/simple-dcc/commit/06beb947563aa6022e6cb681c1138829f8d4efe9))


### Miscellaneous

* **repo:** resolve code review TODOs ([96e5b5c](https://github.com/mikesmitty/simple-dcc/commit/96e5b5c4e06b7a13a8444c3815ad389a11115aa6))
* update templated pico sdk import ([5119de1](https://github.com/mikesmitty/simple-dcc/commit/5119de161f8c2661fe6daeed22fb0efbff59cd03))
* **usb:** set USB manufacturer and product strings ([63654d0](https://github.com/mikesmitty/simple-dcc/commit/63654d01e2b22b003243717dcd61b78620646f67))


### Documentation

* **build:** use root make wrapper (ninja generator) ([c6db0c0](https://github.com/mikesmitty/simple-dcc/commit/c6db0c0fea5f300344be204430e43dd079810085))
* **todo:** add Train Control fixes, accessories and MCP2515 removal ([f4a1871](https://github.com/mikesmitty/simple-dcc/commit/f4a187178e14ad0d84be4f64e6d6c82c70b61bbd))


### Refactoring

* **lcc:** gut OpenLcbCLib wiring (M1) ([76d8c79](https://github.com/mikesmitty/simple-dcc/commit/76d8c793a300f37d6510368c692571510a33f031))

## [0.4.0](https://github.com/mikesmitty/simple-dcc/compare/v0.3.0...v0.4.0) (2026-03-30)


### Features

* **lcc:** add dynamic configuration for RailCom, current limits, and pins ([25e056d](https://github.com/mikesmitty/simple-dcc/commit/25e056d7ae400179e8c64fe48dbc4d6480fe1d54))


### Bug Fixes

* **wavegen:** fix RailCom cutout brake pin and leader timing ([ec9e77b](https://github.com/mikesmitty/simple-dcc/commit/ec9e77b40bfecd6d6a7ef646f739e5edb4ebbf10))

## [0.3.0](https://github.com/mikesmitty/simple-dcc/compare/v0.2.4...v0.3.0) (2026-03-29)


### Features

* **protocol:** implement unique Proxy Node IDs and command enforcement ([1d8e986](https://github.com/mikesmitty/simple-dcc/commit/1d8e986596b7b79fb74ac2c37810f61e6c8f1028))


### Miscellaneous

* **submodule:** point OpenLcbCLib to mikesmitty fork with authorization fix ([884841f](https://github.com/mikesmitty/simple-dcc/commit/884841f75885a7eed89d232409e578f909998674))


### Tests

* **protocol:** add host-side unit tests for LCC traction and interface ([7047ec2](https://github.com/mikesmitty/simple-dcc/commit/7047ec243cf1e3bc86415881008cd4e4879026aa))

## [0.2.4](https://github.com/mikesmitty/simple-dcc/compare/v0.2.3...v0.2.4) (2026-03-28)


### Bug Fixes

* explicit dependency on generated cdi_data.c ([a81bc5d](https://github.com/mikesmitty/simple-dcc/commit/a81bc5d52e8bec8dfba0dc8aebcdaa1a09ea4ece))

## [0.2.3](https://github.com/mikesmitty/simple-dcc/compare/v0.2.2...v0.2.3) (2026-03-28)


### Bug Fixes

* automate firmware version updates and asset uploads ([2f506e7](https://github.com/mikesmitty/simple-dcc/commit/2f506e776c8160b4d57b41faa65914a119ccd8d3))

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
