/*
 * Multi Ticker — RobotDyn WiFi D1 R2 + I2C 16x2 LCD
 *
 * 7 tickers cycling every 5s, prices refresh every 15m.
 * USDC/IDR from Indodax, all others from CoinGecko.
 * Row 0: $price          ↑123  (price left, diff right)
 * Row 1: SYMBOL   DDMMM HH:MM  (name left, timestamp right)
 *
 * Timestamp from Indodax server_time (no NTP needed).
 * Colon blinks every 500ms when internet OK.
 * Backlight blinks for 5 min when a ticker moves >10%.
 */

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "gryffindor";
const char* WIFI_PASSWORD  = "YOUR_WIFI_PASSWORD";
const uint8_t LCD_ADDR     = 0x27;
const unsigned long UPDATE_INTERVAL_MS = 15UL * 60UL * 1000UL;
const unsigned long DISPLAY_CYCLE_MS   = 5UL * 1000UL;
const unsigned long NET_CHECK_MS      = 10UL * 1000UL;
const unsigned long BLINK_COLON_MS    = 500UL;
const unsigned long BLINK_DURATION_MS  = 5UL * 60UL * 1000UL;
const float BLINK_THRESHOLD            = 0.10f;
const int BLINK_ON_MS                  = 30;
const int BLINK_OFF_MS                 = 30;

const char* CG_HOST = "api.coingecko.com";
const char* CG_PATH = "/api/v3/simple/price?ids=bitcoin,pax-gold,ethereum,solana,sp500-xstock,nasdaq-xstock&vs_currencies=usd";

const char* ID_HOST = "indodax.com";
const char* ID_PATH = "/api/usdc_idr/ticker";

uint8_t ARROW_UP[8] = {
  0b00100, 0b01010, 0b10001, 0b00100,
  0b00100, 0b00100, 0b00100, 0b00000
};
uint8_t ARROW_DN[8] = {
  0b00000, 0b00100, 0b00100, 0b00100,
  0b00100, 0b10001, 0b01010, 0b00100
};

struct Ticker {
  const char* label;
  const char* coinId;
  const char* currency;
  const char* prefix;
  float lastPrice;
  float currentPrice;
  bool bigMove;
  bool useIndodax;
};

Ticker tickers[] = {
  { "USDC", "usd-coin",      "idr", "Rp", 0.0f, 0.0f, false, true  },
  { "BTC",  "bitcoin",       "usd", "$",  0.0f, 0.0f, false, false },
  { "PAXG", "pax-gold",      "usd", "$",  0.0f, 0.0f, false, false },
  { "ETH",  "ethereum",      "usd", "$",  0.0f, 0.0f, false, false },
  { "SOL",  "solana",        "usd", "$",  0.0f, 0.0f, false, false },
  { "SPYX", "sp500-xstock",  "usd", "$",  0.0f, 0.0f, false, false },
  { "QQQX", "nasdaq-xstock", "usd", "$",  0.0f, 0.0f, false, false },
};

const int NUM_TICKERS = sizeof(tickers) / sizeof(tickers[0]);
int currentTicker = 0;
bool firstFetch = true;
time_t lastFetchTime = 0;
unsigned long blinkStartMs = 0;
bool blinking = false;

bool netOk = true;
bool prevNetOk = true;
unsigned long lastNetCheckMs = 0;
unsigned long lastBlinkMs = 0;
bool colonVisible = true;
int8_t storedColonCol = -1;

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void connectWiFi();
bool fetchCoinGecko();
bool fetchIndodax();
void fetchAllPrices();
void showTicker(int idx);
void showBootScreen();
void showError(const __FlashStringHelper* msg);
void formatPrice(char* buf, size_t len, float price, const char* prefix);
void formatDiff(char* buf, size_t len, float diff);
void formatTimestamp(char* buf, size_t len);
void updateTimestampBlink();

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n--- Multi Ticker v3.1 boot ---"));

  Wire.begin(4, 5);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, ARROW_UP);
  lcd.createChar(1, ARROW_DN);

  showBootScreen();
  connectWiFi();

  delay(1000);

  fetchAllPrices();
  if (firstFetch) {
    showError(F("All fetches failed"));
  }
}

void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastCycle   = 0;

  if (WiFi.status() != WL_CONNECTED) {
    netOk = false;
    showError(F("WiFi lost"));
    connectWiFi();
    delay(1000);
  }

  unsigned long now = millis();

  if (now - lastCycle >= DISPLAY_CYCLE_MS) {
    lastCycle = now;
    currentTicker = (currentTicker + 1) % NUM_TICKERS;
    showTicker(currentTicker);
  }

  if (now - lastUpdate >= UPDATE_INTERVAL_MS || lastUpdate == 0) {
    lastUpdate = now;
    fetchAllPrices();
  }

  if (now - lastNetCheckMs >= NET_CHECK_MS || lastNetCheckMs == 0) {
    lastNetCheckMs = now;
    IPAddress dnsResult;
    netOk = (WiFi.hostByName(CG_HOST, dnsResult) == 1);
    Serial.printf("DNS check: %s\n", netOk ? "OK" : "FAIL");
    if (!netOk) {
      Serial.println(F("No internet — reconnecting WiFi"));
      WiFi.disconnect();
      delay(100);
      connectWiFi();
    }
    if (netOk != prevNetOk) {
      prevNetOk = netOk;
      showTicker(currentTicker);
    }
  }

  if (now - lastBlinkMs >= BLINK_COLON_MS) {
    lastBlinkMs = now;
    colonVisible = !colonVisible;
    updateTimestampBlink();
  }

  if (blinking) {
    unsigned long elapsed = now - blinkStartMs;
    if (elapsed < BLINK_DURATION_MS && tickers[currentTicker].bigMove) {
      unsigned long inPhase = elapsed % (unsigned long)(BLINK_ON_MS + BLINK_OFF_MS);
      if (inPhase < (unsigned)BLINK_ON_MS) {
        lcd.backlight();
      } else {
        lcd.noBacklight();
      }
    } else {
      lcd.backlight();
      if (elapsed >= BLINK_DURATION_MS) blinking = false;
    }
  }

  delay(10);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Connecting WiFi"));
  lcd.setCursor(0, 1);
  lcd.print(WIFI_SSID);

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);

  // Hidden SSID: scan first to find the network before connecting
  Serial.println(F("Scanning for network..."));
  int8_t scanCount = WiFi.scanNetworks();
  bool found = false;
  for (int8_t i = 0; i < scanCount; i++) {
    if (strcmp(WiFi.SSID(i).c_str(), WIFI_SSID) == 0) {
      found = true;
      Serial.printf("Found %s (ch %d, rssi %d)\n", WIFI_SSID, WiFi.channel(i), WiFi.RSSI(i));
      break;
    }
  }
  WiFi.scanDelete();

  if (!found) {
    // For hidden SSIDs, scan won't find by name — try connecting directly
    Serial.println(F("SSID not in scan, connecting anyway (hidden?)"));
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi connected"));
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("WiFi connected!"));
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(1500);
    netOk = true;
    prevNetOk = true;
  } else {
    Serial.println(F("\nWiFi FAILED"));
    showError(F("WiFi connect fail"));
  }
}

bool fetchCoinGecko() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(2048, 2048);

  const int MAX_RETRIES = 3;
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("CoinGecko (%d)...\n", attempt);

    if (!client.connect(CG_HOST, 443)) {
      Serial.println(F("CG TLS fail"));
      client.stop();
      if (attempt < MAX_RETRIES) { delay(5000); continue; }
      return false;
    }

    client.print(F("GET "));
    client.print(CG_PATH);
    client.println(F(" HTTP/1.0"));
    client.print(F("Host: "));
    client.println(CG_HOST);
    client.println(F("User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/115.0"));
    client.println(F("Accept: application/json"));
    client.println(F("Connection: close"));
    client.println();

    unsigned long timeout = millis();
    while (!client.available() && client.connected()) {
      if (millis() - timeout > 10000) {
        Serial.println(F("CG timeout"));
        client.stop();
        if (attempt < MAX_RETRIES) { delay(5000); continue; }
        return false;
      }
      delay(10);
    }

    String statusLine = client.readStringUntil('\n');
    if (!statusLine.startsWith(F("HTTP/1.0 200")) && !statusLine.startsWith(F("HTTP/1.1 200"))) {
      Serial.println(F("CG HTTP error"));
      client.stop();
      if (attempt < MAX_RETRIES) { delay(5000); continue; }
      return false;
    }

    while (client.connected() || client.available()) {
      String header = client.readStringUntil('\n');
      if (header == "\r" || header == "\n" || header.length() == 0) break;
    }

    String payload = client.readString();
    client.stop();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.println(F("CG JSON err"));
      if (attempt < MAX_RETRIES) { delay(5000); continue; }
      return false;
    }

    bool anyPrice = false;
    bool anyBigMove = false;

    for (int i = 0; i < NUM_TICKERS; i++) {
      if (tickers[i].useIndodax) continue;

      float newPrice = doc[tickers[i].coinId][tickers[i].currency] | 0.0f;
      if (newPrice > 0.0f) {
        tickers[i].lastPrice    = tickers[i].currentPrice;
        tickers[i].currentPrice = newPrice;
        tickers[i].bigMove      = false;

        if (!firstFetch && tickers[i].lastPrice > 0.0f) {
          float change = (tickers[i].currentPrice - tickers[i].lastPrice) / tickers[i].lastPrice;
          if (change > BLINK_THRESHOLD || change < -BLINK_THRESHOLD) {
            tickers[i].bigMove = true;
            anyBigMove = true;
            Serial.printf("%s: %.2f%% move!\n", tickers[i].label, change * 100.0f);
          }
        }

        anyPrice = true;
        Serial.printf("%s: %.2f\n", tickers[i].label, newPrice);
      }
    }

    if (!anyPrice) {
      Serial.println(F("CG no prices"));
      if (attempt < MAX_RETRIES) { delay(5000); continue; }
      return false;
    }

    if (anyBigMove) {
      blinking = true;
      blinkStartMs = millis();
      Serial.println(F(">>> BIG MOVE"));
    }

    if (lastFetchTime == 0 && doc.containsKey("bitcoin")) {
      lastFetchTime = time(nullptr);
    }

    firstFetch = false;
    return true;
  }

  return false;
}

bool fetchIndodax() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(2048, 2048);

  const int MAX_RETRIES = 3;
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("Indodax (%d)...\n", attempt);

    if (!client.connect(ID_HOST, 443)) {
      Serial.println(F("ID TLS fail"));
      client.stop();
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }

    client.print(F("GET "));
    client.print(ID_PATH);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: "));
    client.println(ID_HOST);
    client.println(F("User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/115.0"));
    client.println(F("Accept: application/json"));
    client.println(F("Connection: close"));
    client.println();

    unsigned long timeout = millis();
    while (!client.available() && client.connected()) {
      if (millis() - timeout > 15000) {
        Serial.println(F("ID timeout"));
        client.stop();
        if (attempt < MAX_RETRIES) { delay(3000); continue; }
        return false;
      }
      delay(50);
    }

    // Wait for response data — Indodax can be slow
    delay(1000);

    // Read entire response at once
    String response = client.readString();
    client.stop();

    Serial.print(F("ID raw len: "));
    Serial.println(response.length());

    // Find HTTP status line
    int firstLineEnd = response.indexOf('\n');
    if (firstLineEnd < 0) {
      Serial.println(F("ID no response"));
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }
    String statusLine = response.substring(0, firstLineEnd);
    statusLine.trim();
    Serial.print(F("ID status: "));
    Serial.println(statusLine);
    if (!statusLine.startsWith(F("HTTP/1.0 200")) && !statusLine.startsWith(F("HTTP/1.1 200"))) {
      Serial.println(F("ID HTTP error"));
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }

    // Find JSON body: search for first '{'
    int jsonStart = response.indexOf(F("{\"ticker\""));
    if (jsonStart < 0) {
      Serial.println(F("ID no ticker JSON"));
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }
    String payload = response.substring(jsonStart);
    int jsonEnd = payload.lastIndexOf('}');
    if (jsonEnd >= 0) {
      payload = payload.substring(0, jsonEnd + 1);
    }

    Serial.print(F("ID payload: "));
    Serial.println(payload);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print(F("ID JSON err: "));
      Serial.println(err.c_str());
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }

    float newPrice = doc["ticker"]["last"].as<float>();
    long serverTime = doc["ticker"]["server_time"].as<long>();

    if (newPrice <= 0.0f) {
      Serial.println(F("ID no price"));
      if (attempt < MAX_RETRIES) { delay(3000); continue; }
      return false;
    }

    int i = 0;
    tickers[i].lastPrice    = tickers[i].currentPrice;
    tickers[i].currentPrice = newPrice;
    tickers[i].bigMove      = false;

    if (!firstFetch && tickers[i].lastPrice > 0.0f) {
      float change = (tickers[i].currentPrice - tickers[i].lastPrice) / tickers[i].lastPrice;
      if (change > BLINK_THRESHOLD || change < -BLINK_THRESHOLD) {
        tickers[i].bigMove = true;
        Serial.printf("USDC: %.2f%% move!\n", change * 100.0f);
      }
    }

    if (serverTime > 1000000000) {
      lastFetchTime = (time_t)serverTime;
      struct timeval tv = { .tv_sec = (time_t)serverTime, .tv_usec = 0 };
      settimeofday(&tv, nullptr);
    }

    Serial.printf("USDC: %.2f\n", newPrice);
    firstFetch = false;
    return true;
  }

  return false;
}

void fetchAllPrices() {
  bool cgOk = fetchCoinGecko();
  bool idOk = fetchIndodax();

  if (!cgOk) Serial.println(F("CoinGecko failed"));
  if (!idOk) Serial.println(F("Indodax failed"));

  if (!cgOk && !idOk) {
    netOk = false;
  } else {
    netOk = true;
  }
  if (netOk != prevNetOk) {
    prevNetOk = netOk;
    showTicker(currentTicker);
  }
}

void showTicker(int idx) {
  if (blinking) lcd.backlight();
  storedColonCol = -1;
  lcd.clear();

  if (tickers[idx].currentPrice == 0.0f) {
    lcd.setCursor(0, 0);
    lcd.print(F("---"));
  } else {
    lcd.setCursor(0, 0);
    char priceBuf[17];
    formatPrice(priceBuf, sizeof(priceBuf), tickers[idx].currentPrice, tickers[idx].prefix);
    lcd.print(priceBuf);

    if (!firstFetch && tickers[idx].lastPrice > 0.0f) {
      float diff = tickers[idx].currentPrice - tickers[idx].lastPrice;
      long intDiff = (long)diff;

      if (intDiff != 0) {
        char diffNum[9];
        long absIntDiff = (intDiff > 0) ? intDiff : -intDiff;
        formatDiff(diffNum, sizeof(diffNum), (float)absIntDiff);
        int diffLen = 1 + strlen(diffNum);
        lcd.setCursor(16 - diffLen, 0);
        if (intDiff > 0) lcd.write(byte(0));
        else lcd.write(byte(1));
        lcd.print(diffNum);
      } else {
        lcd.setCursor(15, 0);
        lcd.print(F("="));
      }
    }
  }

  lcd.setCursor(0, 1);
  lcd.print(tickers[idx].label);

  if (!netOk) {
    const char* noNet = "NONET-Recon";
    lcd.setCursor(16 - strlen(noNet), 1);
    lcd.print(noNet);
  } else {
    char timeBuf[16];
    formatTimestamp(timeBuf, sizeof(timeBuf));
    if (strlen(timeBuf) > 0) {
      int startCol = 16 - strlen(timeBuf);
      lcd.setCursor(startCol, 1);
      lcd.print(timeBuf);
      for (int i = 0; timeBuf[i]; i++) {
        if (timeBuf[i] == ':') {
          storedColonCol = startCol + i;
          break;
        }
      }
    }
  }
}

void updateTimestampBlink() {
  if (currentTicker < 0 || currentTicker >= NUM_TICKERS) return;
  if (!netOk) return;

  // Re-render timestamp when minute changes
  static int8_t lastDisplayedMin = -1;
  time_t now = time(nullptr);
  if (now > 1000000000) {
    int8_t currentMin = (int8_t)((now + 7 * 3600) / 60 % 1440);
    if (currentMin != lastDisplayedMin) {
      lastDisplayedMin = currentMin;
      char timeBuf[16];
      formatTimestamp(timeBuf, sizeof(timeBuf));
      if (strlen(timeBuf) > 0) {
        storedColonCol = -1;
        int startCol = 16 - strlen(timeBuf);
        lcd.setCursor(startCol, 1);
        lcd.print(timeBuf);
        for (int i = 0; timeBuf[i]; i++) {
          if (timeBuf[i] == ':') {
            storedColonCol = startCol + i;
            break;
          }
        }
      }
    }
  }

  if (storedColonCol >= 0) {
    lcd.setCursor(storedColonCol, 1);
    lcd.print(colonVisible ? ':' : ' ');
  }
}

void formatTimestamp(char* buf, size_t len) {
  buf[0] = '\0';
  time_t now = time(nullptr);
  if (now < 1000000000) return;

  // Convert UTC to UTC+7 manually
  time_t localTime = now + (7 * 3600);
  struct tm* timeinfo = gmtime(&localTime);
  if (!timeinfo) return;

  int day = timeinfo->tm_mday;
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int m = timeinfo->tm_mon;
  if (m < 0 || m > 11) m = 0;
  int h = timeinfo->tm_hour;
  int mi = timeinfo->tm_min;

  snprintf(buf, len, "%d%s %d:%02d", day, months[m], h, mi);
}

void formatPrice(char* buf, size_t len, float price, const char* prefix) {
  long whole = (long)price;

  char raw[16];
  int i = 0;
  int digitCount = 0;

  if (whole == 0) {
    raw[i++] = '0';
  } else {
    long n = whole;
    while (n > 0) {
      if (digitCount > 0 && digitCount % 3 == 0) {
        raw[i++] = ',';
      }
      raw[i++] = '0' + (n % 10);
      n /= 10;
      digitCount++;
    }
  }

  int pos = 0;
  while (*prefix && pos < (int)len - 1) {
    buf[pos++] = *prefix++;
  }

  for (int j = i - 1; j >= 0 && pos < (int)len - 1; j--) {
    buf[pos++] = raw[j];
  }
  buf[pos] = '\0';
}

void formatDiff(char* buf, size_t len, float diff) {
  long whole = (long)diff;

  char raw[12];
  int i = 0;
  int digitCount = 0;

  if (whole == 0) {
    raw[i++] = '0';
  } else {
    long n = whole;
    while (n > 0) {
      if (digitCount > 0 && digitCount % 3 == 0) {
        raw[i++] = ',';
      }
      raw[i++] = '0' + (n % 10);
      n /= 10;
      digitCount++;
    }
  }

  int pos = 0;
  for (int j = i - 1; j >= 0 && pos < (int)len - 1; j--) {
    buf[pos++] = raw[j];
  }
  buf[pos] = '\0';
}

void showBootScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Multi Ticker"));
  lcd.setCursor(0, 1);
  lcd.print(F("v3.1 7coins"));
  delay(2000);
}

void showError(const __FlashStringHelper* msg) {
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("ERROR:"));
  lcd.setCursor(0, 1);
  lcd.print(msg);
  Serial.print(F("ERROR: "));
  Serial.println(msg);
}