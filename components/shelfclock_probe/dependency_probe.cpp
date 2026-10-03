// Compile/link probe only. This function is deliberately never executed.
#if SHELFCLOCK_PROBE_GROUP
#include "ShelfClockConfig.h"
#include <Arduino.h>

#if SHELFCLOCK_PROBE_GROUP & 1
#include <ArduinoJson.h>
#include <MultiMap.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 17
#include <LittleFS.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 2
#include <FastLED.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 4
#include <NonBlockingRtttl.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 8
#include <RTClib.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 32
#include <DHT.h>
#endif
#if SHELFCLOCK_PROBE_GROUP & 16
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#endif

void shelfclockDependencyProbe() {
#if SHELFCLOCK_PROBE_GROUP & 1
  DynamicJsonDocument json(256);
  deserializeJson(json, "{\"minutes\":\"99\"}");
  String output;
  serializeJson(json, output);
  LittleFS.begin(false);
  File file = LittleFS.open("/settings/clockSettings-generic.json", "r");
  int input[] = {0, 10};
  int values[] = {0, 255};
  Serial.println(multiMap(5, input, values, 2));
#endif
#if SHELFCLOCK_PROBE_GROUP & 2
  CRGB leds[NUM_LEDS];
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.show();
#endif
#if SHELFCLOCK_PROBE_GROUP & 4
  rtttl::begin(BUZZER_PIN, "Probe:d=4,o=5,b=120:c");
  rtttl::play();
  rtttl::stop();
  double real[SOUNDDETECTOR_SAMPLES] = {};
  double imaginary[SOUNDDETECTOR_SAMPLES] = {};
  arduinoFFT fft(real, imaginary, SOUNDDETECTOR_SAMPLES, SOUNDDETECTOR_SAMPLING_FREQ);
  fft.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  fft.Compute(FFT_FORWARD);
  fft.ComplexToMagnitude();
  const i2s_config_t config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SOUNDDETECTOR_SAMPLING_FREQ,
    .bits_per_sample = i2s_bits_per_sample_t(SOUNDDETECTOR_BITS_PER_SAMPLE),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = SOUNDDETECTOR_SAMPLES,
    .use_apll = false
  };
  i2s_driver_install(SOUNDDETECTOR_I2S_PORT, &config, 0, nullptr);
  const i2s_pin_config_t pins = {
    .bck_io_num = SOUNDDETECTOR_I2S_SCK,
    .ws_io_num = SOUNDDETECTOR_I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = SOUNDDETECTOR_I2S_SD
  };
  i2s_set_pin(SOUNDDETECTOR_I2S_PORT, &pins);
  int16_t samples[8];
  size_t bytes = 0;
  i2s_read(SOUNDDETECTOR_I2S_PORT, samples, sizeof(samples), &bytes, 0);
#endif
#if SHELFCLOCK_PROBE_GROUP & 8
  RTC_DS3231 rtc;
  rtc.begin();
  Serial.println(rtc.now().unixtime());
#endif
#if SHELFCLOCK_PROBE_GROUP & 32
  DHT dht(DHT_PIN, DHTTYPE);
  dht.begin();
  Serial.println(dht.readTemperature());
#endif
#if SHELFCLOCK_PROBE_GROUP & 16
  WebServer server(80);
  server.enableCORS(true);
  server.serveStatic("/", LittleFS, "/index.html");
  server.handleClient();
  HTTPClient http;
  http.begin("http://example.invalid");
  http.end();
  Serial.println(WiFi.localIP());
  MDNS.begin("shelfclock-compile-probe");
#endif
}
#endif
