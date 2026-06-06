# USDC/IDR & Crypto Ticker

ESP8266-based multi-crypto ticker on a 16x2 I2C LCD. Fetches prices from **Indodax** (USDC/IDR) and **CoinGecko** (6 other coins) every 15 minutes, cycles through 7 tickers every 5 seconds, blinks the backlight when a price moves >10%.

## Hardware

- **Board:** RobotDyn WiFi D1 R2 (ESP8266, 2.4 GHz 802.11 b/g/n)
- **Display:** 16x2 LCD with I2C backpack (PCF8574)
- **Connection via USB** for programming and serial monitoring (115200 baud)

### Wiring

| LCD Backpack Pin | D1 R2 Pin |
|---|---|
| GND | GND |
| VCC | 5V |
| SDA | D2 (GPIO4) |
| SCL | D1 (GPIO5) |

> If the LCD is blank but backlit, adjust the contrast potentiometer (blue square on the backpack) or try I2C address `0x3F` instead of `0x27`.

## Tickers

| Symbol | Source | Pair | Example |
|---|---|---|---|
| USDC | **Indodax** | USDC/IDR | Rp18,176 |
| BTC | CoinGecko | BTC/USD | $61,197 |
| PAXG | CoinGecko | PAXG/USD | $4,303 |
| ETH | CoinGecko | ETH/USD | $1,580 |
| SOL | CoinGecko | SOL/USD | $63 |
| SPYX | CoinGecko | SPYX/USD | $737 |
| QQQX | CoinGecko | QQQX/USD | $703 |

- **Indodax** is used for USDC/IDR because it gives the actual Indonesian exchange rate in IDR.
- **CoinGecko** is used for the rest because it covers the xStocks tokens (SPYX, QQQX) which Indodax doesn't list.
- SPYX and QQQX are xStocks-based crypto tokens tracking the S&P 500 and Nasdaq-100 respectively.

## Display Layout

```
┌────────────────┐
│Rp18,213   ↑123 │  Row 0: price (left) + diff arrow (right)
│USDC    6Jun14:22│  Row 1: symbol (left) + timestamp UTC+7 (right)
└────────────────┘
```

- **Row 0:** Price with currency prefix + directional arrow with diff amount
  - `↑` = price went up (absolute diff)
  - `↓` = price went down (absolute diff)
  - `=` = no change (or diff rounds to 0)
- **Row 1:** Ticker symbol + blinking timestamp `DMMM HH:MM` in UTC+7
  - The `:` blinks on/off every 500ms
  - Timestamp updates every minute automatically
  - The day rolls over automatically (e.g. `6Jun` → `7Jun`)

## Alert System

When any ticker moves **more than 10%** between 15-minute fetch cycles:

- The LCD backlight blinks rapidly (30ms on/off) **only while that specific ticker is displayed**
- Other tickers display normally with backlight on
- Blinking stops after 5 minutes

## Connectivity Check

Every 10 seconds, the firmware does a DNS resolve to `api.coingecko.com`. If it fails:
- The timestamp is replaced with `NONET-Recon`
- WiFi is disconnected and reconnected
- When connectivity is restored, the next 15-min fetch will resume

## Time Sync

No NTP server is used. The ESP8266's internal clock is synced from **Indodax's `server_time`** field on every successful fetch. The display reads from `time(nullptr)` directly, so the timestamp ticks forward every second and the minute rolls over automatically.

## Configuration

Edit `src/main.cpp` to change:

| Constant | Default | Description |
|---|---|---|
| `WIFI_SSID` | gryffindor | WiFi network name (supports hidden SSIDs) |
| `WIFI_PASSWORD` | your_password_here | WiFi password |
| `LCD_ADDR` | 0x27 | I2C address of LCD backpack (0x27 or 0x3F) |
| `UPDATE_INTERVAL_MS` | 900000 (15 min) | Price refresh interval |
| `DISPLAY_CYCLE_MS` | 5000 (5 sec) | Ticker cycle interval |
| `NET_CHECK_MS` | 10000 (10 sec) | Connectivity check interval |
| `BLINK_COLON_MS` | 500 | Colon blink rate |
| `BLINK_DURATION_MS` | 300000 (5 min) | How long blink alert lasts |
| `BLINK_THRESHOLD` | 0.10 (10%) | Price change threshold for alerts |
| `BLINK_ON_MS` / `BLINK_OFF_MS` | 30 | Backlight blink speed |

## Adding a New Ticker

1. Add a `Ticker` entry to the `tickers[]` array in `src/main.cpp`
2. If using CoinGecko: update `CG_PATH` to include the new coin ID
3. If using Indodax: change the `ID_PATH` URL or set `useIndodax = false`
4. Recompile and upload

## Build & Upload

Requires [PlatformIO](https://platformio.org/):

```bash
# Build
pio run -e d1_r2

# Build and upload
pio run -e d1_r2 -t upload

# Serial monitor
pio device monitor -e d1_r2
```

### Board selection in Arduino IDE

LOLIN(WEMOS) D1 R2 mini or NodeMCU 1.0 (ESP-12E Module).

### Required libraries

- **LiquidCrystal_I2C** by Frank de Brabander (v1.1.4)
- **ArduinoJson** by Benoit Blanchon (v7)

## API

### CoinGecko (no key required)

```
https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,pax-gold,ethereum,solana,sp500-xstock,nasdaq-xstock&vs_currencies=usd
```

### Indodax (no key required)

```
https://indodax.com/api/usdc_idr/ticker
```

Returns:
```json
{"ticker":{"buy":"18186","high":"18449","last":"18213","low":"18055","sell":"18213","server_time":1780749701,...}}
```

The firmware also uses the response to sync the ESP8266's internal clock via `settimeofday()`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| LCD blank, backlight on | Adjust contrast pot on backpack, or try I2C address `0x3F` |
| LCD blank, no backlight | Check wiring (SDA→D2, SCL→D1), check 5V power |
| `TLS connect fail` | Firmware retries 3x with 5s delay; CoinGecko/Indodax sometimes drops ESP8266 TLS |
| `WiFi connect fail` | Check SSID/password in `src/main.cpp`; board only supports 2.4 GHz |
| `ID HTTP error` | Indodax blocked the request; will auto-retry |
| Prices show `---` | First fetch failed; will retry in 15 minutes |
| `NONET-Recon` shown | DNS check failed; firmware will try to reconnect WiFi |
| Hidden SSID won't connect | The firmware scans first; if SSID doesn't appear in scan, it tries connecting directly with a 60s timeout |
