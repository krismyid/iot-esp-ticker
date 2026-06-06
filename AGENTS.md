# AGENTS.md — USDC/IDR & Crypto Ticker

## Project Overview

ESP8266 (RobotDyn WiFi D1 R2) firmware that displays 7 crypto prices on a 16x2 I2C LCD (PCF8574 backpack). USDC/IDR comes from **Indodax**, the other 6 from **CoinGecko**. Cycles every 5 seconds, refreshes every 15 minutes, blinks backlight on >10% moves, syncs time from Indodax's `server_time` field (no NTP).

## Architecture

Single-file firmware: `src/main.cpp` (~660 lines).

### Key Components

- **Ticker system:** Array of `Ticker` structs with `label`, `coinId`, `currency`, `prefix`, `lastPrice`, `currentPrice`, `bigMove` flag, `useIndodax` source toggle.
- **Display:** 16x2 I2C LCD via `LiquidCrystal_I2C`. Row 0 = price + diff arrow. Row 1 = symbol + right-aligned timestamp with blinking colon.
- **Data sources:**
  - **Indodax** for USDC/IDR: raw HTTPS over `WiFiClientSecure` with HTTP/1.1, Firefox User-Agent, 2048-byte BearSSL buffers.
  - **CoinGecko** for the other 6: raw HTTPS with HTTP/1.0 (CoinGecko serves chunked under HTTP/1.1 which breaks on ESP8266).
- **Alerts:** `bigMove` flag per ticker when price changes >10%. Backlight blinks (30ms cycle) only when that ticker is displayed. Stops after 5 minutes.
- **Time:** Synced from Indodax's `server_time` via `settimeofday()` on every successful fetch. Display uses `time(nullptr)` directly so minutes auto-rollover. Format: `DMMM HH:MM` in UTC+7.
- **Connectivity check:** Every 10s, DNS resolve to `api.coingecko.com`. On failure: replace timestamp with `NONET-Recon` and disconnect/reconnect WiFi.

### Timing

- `DISPLAY_CYCLE_MS` (5s): ticker rotation via `millis()` in `loop()`
- `UPDATE_INTERVAL_MS` (15min): price refresh via `millis()` in `loop()`
- `NET_CHECK_MS` (10s): DNS check for internet
- `BLINK_COLON_MS` (500ms): colon blink on/off
- `BLINK_DURATION_MS` (5min): alert blink duration
- Minute-change detection in blink loop re-renders the timestamp without full screen clear

### Display Layout

```
Row 0: $price          ↑diff
Row 1: SYMBOL    DMMM HH:MM
```

Where:
- Price has currency prefix (`Rp` for IDR, `$` for USD)
- Diff is `↑<n>` (up), `↓<n>` (down), or `=` (no change / rounds to 0)
- Timestamp shows `DMMM HH:MM` (no leading zero on day), blinking `:` between hours and minutes

### Critical Implementation Details

1. **HTTP/1.0 for CoinGecko, HTTP/1.1 for Indodax.** CoinGecko responds with chunked encoding under HTTP/1.1 which breaks on ESP8266. Indodax's Cloudflare requires HTTP/1.1 + Firefox User-Agent to pass the challenge.
2. **WiFiClientSecure raw.** Do NOT use `HTTPClient` — it caused TLS connection drops. The firmware builds raw HTTP requests over `WiFiClientSecure`.
3. **Buffer sizes.** BearSSL on ESP8266 needs `client.setBufferSizes(2048, 2048)` minimum.
4. **Retry logic.** 3 attempts with 3-5s delay for each source.
5. **Hidden SSID.** Firmware scans first; if the SSID doesn't appear in scan, it tries connecting directly with a 60s timeout.
6. **Indodax response parsing.** The response includes Cloudflare headers — search for `{"ticker"` substring to skip the Cloudflare JSON and find the actual ticker data.
7. **ArduinoJson v7 quirks.** Use `.as<float>()` and `.as<long>()` for explicit type conversion. The `|` operator with default values doesn't work for string extraction in v7.
8. **I2C address.** LCD backpack can be `0x27` or `0x3F`. Defined as `LCD_ADDR` constant.
9. **Price formatting.** Manual integer-to-formatted-string with thousand separators (no `sprintf` float support on ESP8266).
10. **Flash strings.** All static strings use `F()` macro to save RAM.
11. **Time sync.** `settimeofday()` updates ESP8266's internal clock from Indodax `server_time` on every successful fetch.
12. **Minute rollover.** The `updateTimestampBlink()` function checks the current minute every 500ms and re-renders only the timestamp area (no full screen clear) when it changes.

## Build System

PlatformIO with `platformio.ini` at project root.

```bash
# Build & upload
pio run -e d1_r2 -t upload

# Serial monitor (115200 baud)
pio device monitor -e d1_r2
```

### Dependencies

```ini
lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    bblanchon/ArduinoJson@^7.0.0
```

### Board Config

```ini
[env:d1_r2]
platform = espressif8266
board = d1
framework = arduino
upload_port = /dev/ttyUSB0
upload_speed = 115200
```

## CoinGecko IDs

| Display Symbol | CoinGecko `id` | `vs_currencies` |
|---|---|---|
| BTC | `bitcoin` | `usd` |
| PAXG | `pax-gold` | `usd` |
| ETH | `ethereum` | `usd` |
| SOL | `solana` | `usd` |
| SPYX | `sp500-xstock` | `usd` |
| QQQX | `nasdaq-xstock` | `usd` |

## Indodax Pairs

| Display Symbol | Indodax Pair | Endpoint |
|---|---|---|
| USDC | `usdc_idr` | `/api/usdc_idr/ticker` |

Response includes `ticker.last` (price) and `ticker.server_time` (UTC unix timestamp).

## Adding a New Ticker

1. Add a `Ticker` entry to the `tickers[]` array with the appropriate `coinId`, `currency`, `prefix`, and `useIndodax` flag.
2. If CoinGecko: update `CG_PATH` to include the new coin ID.
3. If Indodax: change `ID_PATH` to the new endpoint.
4. Recompile and upload.

## Known Issues

- ESP8266 is single-threaded. All timing is cooperative via `millis()` — there are no real threads.
- ESP8266 only supports 2.4 GHz 802.11 b/g/n. No 5 GHz, no WiFi 5/6.
- `gmtime_r()` is not available on ESP8266 — use `gmtime()`.
- `setTime()` from TimeLib doesn't work without including the library — use `settimeofday()` instead.
- Indodax requires Cloudflare-friendly User-Agent ("Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/115.0").
- The first 15 minutes after boot show no diff (no previous price to compare).
- `float` precision for BTC/IDR prices (>1 billion IDR) may truncate. We use USD prices for BTC instead.

## File Map

```
usdc-ticker/
  platformio.ini      # PlatformIO build config
  src/
    main.cpp           # All firmware code (setup, loop, WiFi, fetch, display)
  AGENTS.md            # This file
  README.md            # Human-readable documentation
```
