# Axonolotl

Flipper Zero port of [AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra) by withlovefromminneapolis.

Broadcasts BLE advertisements using the Axon Signal protocol to trigger Axon body cameras.

## Features

### Broadcast
Single unified broadcast action that combines trigger and fuzz functionality:

1. **Sends original payload** - triggers any Axon camera in range
2. **Sends 10 fuzzed payloads** - attempts to bypass cooldown restriction
3. **Repeats** until user presses Back

Each transmission uses a new randomly-generated MAC address with the **Axon OUI prefix (00:25:DF)**, making each broadcast appear to come from a different Axon device.

- **Rate**: 100ms between transmissions (10 TX/second)
- **LED**: Green while active
- **Display**: Shows TX count, mode (ORIGINAL/FUZZED), and current fuzz value

### Why Fuzz?
Once a camera has been triggered, it ignores repeated signals from the same source for a cooldown period. The fuzz mode mutates payload bytes using random values to bypass this restriction, combined with MAC address rotation to maximize trigger success.

## Installation

Copy the `axonolotl` folder to your Flipper Zero's SD card:
```
SD Card/apps/Bluetooth/axonolotl.fap
```

Or build from source using `ufbt`:
```bash
ufbt build
```

## Protocol Details

Derived from `MainActivity.kt` in the original AxonCadabra source:

| Parameter | Value | Source |
|-----------|-------|--------|
| Service UUID | 0xFE6C | Line 40 |
| Service Data | 24 bytes | Lines 42-46 |
| Axon OUI | 00:25:DF | Line 38, IEEE Registry |
| Broadcast Interval | 100ms | User config |
| Fuzz per cycle | 10 | User config |

**Base payload (hex):**
```
01583837303032465034010200000000CE1B330000020000
```

**Fuzz mutations** (from `updateServiceDataWithFuzz()` lines 303-321):
- Byte 10: `(fuzz_value >> 8) & 0xFF`
- Byte 11: `fuzz_value & 0xFF`
- Byte 20: `(fuzz_value >> 4) & 0xFF`
- Byte 21: `(fuzz_value << 4) & 0xFF`

**MAC Address:**
- Bytes 0-2: Axon OUI (00:25:DF) - constant
- Bytes 3-5: Random - regenerated each transmission

## Limitations

- **No scanning**: The Flipper Zero BLE stack only supports peripheral mode. Scanning for nearby Axon cameras (by MAC prefix 00:25:DF) would require central mode, which is not exposed in the public API.

## Files

```
axonolotl/
├── application.fam     # App manifest
├── axonolotl.c         # Main implementation
├── axonolotl.h         # Header with protocol constants
├── axonolotl.png       # App icon
```

## Credits

- **Original**: [withlovefromminneapolis](https://github.com/WithLoveFromMinneapolis/AxonCadabra)
- **Flipper Zero port**: infocyte

## License

Same as original AxonCadabra project.
