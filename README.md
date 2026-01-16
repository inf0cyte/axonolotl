# Axonolotl

Flipper Zero port of [AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra) by withlovefromminneapolis.

Broadcasts BLE advertisements using the Axon Signal protocol to trigger Axon body cameras.

## Features

### Trigger
Continuously broadcasts the Axon Signal BLE advertisement. Any Axon body camera within range (~5-10m) will be triggered to start recording. Broadcasting continues until you press Back.

- LED: Green while active

### Fuzz Mode
Attempts to bypass the trigger cooldown restriction. Once a camera has been triggered, it ignores repeated signals from the same source for a cooldown period. Fuzz mode mutates payload bytes every 500ms in an attempt to make each broadcast appear as a distinct trigger source.

- LED: Magenta while active
- Display shows current fuzz value (0x0000 - 0xFFFF)

## Installation

Copy the `axonolotl` folder to your Flipper Zero's SD card:
```
SD Card/apps/Bluetooth/axonolotl.fap
```

Or build from source using `ufbt`:
```bash
ufbt build
ufbt launch
```

## Protocol Details

Derived from `MainActivity.kt` in the original AxonCadabra source:

| Parameter | Value |
|-----------|-------|
| Service UUID | 0xFE6C |
| Service Data | 24 bytes |
| Axon OUI | 00:25:DF |
| Fuzz Interval | 500ms |

Base payload (hex):
```
01583837303032465034010200000000CE1B330000020000
```

Fuzz mode mutates bytes at positions 10, 11, 20, and 21.

## Limitations

- **No scanning**: The Flipper Zero BLE stack only supports peripheral mode. Scanning for nearby Axon cameras (by MAC prefix 00:25:DF) would require central mode, which is not exposed in the public API.

## Files

```
axonolotl/
├── application.fam     # App manifest
├── axonolotl.c         # Main implementation
├── axonolotl.h         # Header with protocol constants
├── axonolotl.png       # App icon
└── icons/
    └── Signal_10x10.png
```

## Credits

- **Original**: [withlovefromminneapolis](https://github.com/WithLoveFromMinneapolis/AxonCadabra)
- **Flipper Zero port**: infocyte

## License

Same as original AxonCadabra project.
