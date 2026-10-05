#include "ShelfClockConfig.h"
#include "ShelfClockRuntime.h"
#if SHELFCLOCK_STANDALONE
#include "StandaloneHost.h"
#endif
#include <shelfclock/HomeCommand.h>
#include <shelfclock/ScheduleTime.h>
#include <shelfclock/ScrollSequence.h>
using shelfclock::scheduleTimeMatches;
#include <vector>
#include <FastLED.h>
#if defined(FASTLED_ESP32_USE_CLOCKLESS_SPI)
#include <fl/unique_ptr.h>
#include <pixel_iterator.h>
#include <platforms/esp/32/spi_ws2812/strip_spi.h>

// FastLED 3.10.3's ESP32 SPI controller leaves each DMA transfer running after
// show() returns. This controller uses the same conversion path but waits for
// the complete frame, preventing the SPI pixel buffer from changing in flight.
template <int DATA_PIN, EOrder RGB_ORDER>
class ShelfClockSynchronousSpiController : public CPixelLEDController<RGB_ORDER> {
 public:
  void init() override {}

  uint16_t getMaxRefreshRate() const override { return 800; }

 protected:
  void showPixels(PixelController<RGB_ORDER> &pixels) override {
    const auto rgbw = this->getRgbw();
    const bool isRgbw = rgbw.active();
    PixelIterator input = pixels.as_iterator(rgbw);

    if (!strip_) {
      strip_.reset(ISpiStripWs2812::Create(DATA_PIN, input.size(), isRgbw));
    }

    auto output = strip_->outputIterator();
    if (isRgbw) {
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      uint8_t white;
      while (input.has(1)) {
        input.loadAndScaleRGBW(&red, &green, &blue, &white);
        output(red);
        output(green);
        output(blue);
        output(white);
        input.advanceData();
        input.stepDithering();
      }
    } else {
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      while (input.has(1)) {
        input.loadAndScaleRGB(&red, &green, &blue);
        output(red);
        output(green);
        output(blue);
        input.advanceData();
        input.stepDithering();
      }
    }
    output.finish();
    strip_->drawSync();
    // The SPI backend does not add a WS2812 reset pulse after transmission.
    // Continuous animations can otherwise start another frame before the
    // strip has latched the previous one. FastLED's RMT path uses 280 us.
    delayMicroseconds(300);
  }

 private:
  fl::unique_ptr<ISpiStripWs2812> strip_;
};
#endif
#include <WiFi.h>
#include "WebServer.h"
#include <HTTPClient.h>
#include <Update.h>
#include <FS.h>     
#if SHELFCLOCK_STANDALONE
#include <ESPmDNS.h>
#include <AutoConnect.h>
#endif
#include <ArduinoJson.h>
#include <MultiMap.h>
#include <freertos/FreeRTOS.h>
#include "ShelfClick.h"
#include "ShelfClockLedMaps.h"

#if HAS_SOUNDDETECTOR
  #include <arduinoFFT.h>				// Don't forget to change CPU Frequency to 240MHz in Arduino board settings
  #if SHELFCLOCK_STANDALONE
    #include <driver/i2s.h>
  #else
    #include <driver/i2s_std.h>
  #endif
  #endif

#if HAS_BUZZER
//  #include "../lib/Sounds/Sounds.h"
  #include <NonBlockingRtttl.h> 
  #endif

#if USE_LITTLEFS
  // Use LittleFS
  #include "FS.h"
  // The library has been merged into esp32 core from release 1.0.6
  #include <LittleFS.h>       // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS
  FS* filesystem =      &LittleFS;
  #define FileFS        LittleFS
  #define FS_Name       "LittleFS"
#elif USE_SPIFFS
  #include <SPIFFS.h>
  FS* filesystem =      &SPIFFS;
  #define FileFS        SPIFFS
  #define FS_Name       "SPIFFS"
#else
  // +Use FFat
  #include <FFat.h>
  FS* filesystem =      &FFat;
  #define FileFS        FFat
  #define FS_Name       "FFat"
  #endif

#define FORMAT_SPIFFS_IF_FAILED false
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#if HAS_SOUNDDETECTOR
  /*
  b     b     b     b     b     b     b     b
  a     a     a     a     a     a     a     a
  r     r     r     r     r     r     r     r
  0     1     2     3     4     5     6     7

    34    28    24    18    14     8     4
    →→→   →→→   →→→  →→→   →→→   →→→   →→→
  ↑     ↓     ↑     ↓     ↑     ↓     ↑     ↓
  ↑33   ↓35   ↑23   ↓25   ↑13   ↓15   ↑3    ↓5
  ↑     ↓     ↑     ↓     ↑     ↓     ↑     ↓
  ↑ 36  ↓ 29  ↑ 26  ↓ 19  ↑ 16  ↓   9 ↑  6  ↓
    →→→   →→→   →→→  →→→   →→→   →→→   →→→
  ↑     ↓     ↑     ↓     ↑     ↓     ↑     ↓
  ↑32   ↓30   ↑22   ↓20   ↑12   ↓10   ↑2    ↓0
  ↑     ↓     ↑     ↓     ↑     ↓     ↑     ↓
  ↑ 31  ↓ 27  ↑ 21  ↓ 17  ↑ 11  ↓  7  ↑  1  ↓
   →→→   →→→   →→→  →→→   →→→   →→→   →→→  
  */

  // Sampling and FFT stuff
  unsigned int sampling_period_us;
  byte peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};              // The length of these arrays must be >= SOUNDDETECTOR_BANDS_WIDTH
  int oldBarHeights[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  int SOUNDDETECTOR_bandValues[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  int SOUNDDETECTOR_noiseThresholds[SOUNDDETECTOR_BANDS_WIDTH] = {800, 300, 200, 200, 200, 200, 200, 200}; // Example thresholds
  double vReal[SOUNDDETECTOR_SAMPLES];
  double vImag[SOUNDDETECTOR_SAMPLES];
  long SOUNDDETECTOR_Amplitude = 1000;
  arduinoFFT FFT = arduinoFFT(vReal, vImag, SOUNDDETECTOR_SAMPLES, SOUNDDETECTOR_SAMPLING_FREQ);
  #if !SHELFCLOCK_STANDALONE
    i2s_chan_handle_t soundDetectorRxChannel = nullptr;
  #endif
  DEFINE_GRADIENT_PALETTE( purple_gp ) {
    0,   0, 212, 255,   //blue
  255, 179,   0, 255 }; //purple
  DEFINE_GRADIENT_PALETTE( outrun_gp ) {
    0, 141,   0, 100,   //purple
  127, 255, 192,   0,   //yellow
  255,   0,   5, 255 };  //blue
  DEFINE_GRADIENT_PALETTE( greenblue_gp ) {
    0,   0, 255,  60,   //green
  64,   0, 236, 255,   //cyan
  128,   0,   5, 255,   //blue
  192,   0, 236, 255,   //cyan
  255,   0, 255,  60 }; //green
  DEFINE_GRADIENT_PALETTE( redyellow_gp ) {
    0,   200, 200,  200,   //white
  64,   255, 218,    0,   //yellow
  128,   231,   0,    0,   //red
  192,   255, 218,    0,   //yellow
  255,   200, 200,  200 }; //white
  DEFINE_GRADIENT_PALETTE( fire_gp ) {
    0,   255,   0,    0,   //red
  64,   255, 64, 0,   //orange
  128,   255, 128,    0,   //yellow
  192,   255, 192,    0,   //light yellow
  255,   255, 255,  255 }; //white
  CRGBPalette16 purplePal = purple_gp;
  CRGBPalette16 outrunPal = outrun_gp;
  CRGBPalette16 greenbluePal = greenblue_gp;
  CRGBPalette16 heatPal = redyellow_gp;
  CRGBPalette16 heaterPal = fire_gp;
  uint8_t colorTimer = 0;
  int buttonPushCounter = 0;
  int SOUNDDETECTOR_averageAudioInput = 0;
  int SOUNDDETECTOR_decay = 0; // HOW MANY MS BEFORE ONE LIGHT DECAY
  int SOUNDDETECTOR_decay_check = 0;
  int SOUNDDETECTOR_pre_react = 0; // NEW SPIKE CONVERSION
  int SOUNDDETECTOR_react = 0; // NUMBER OF LEDs BEING LIT
  int SOUNDDETECTOR_post_react = 0; // OLD SPIKE CONVERSION
  #endif

String softwareVersion = "version-3.0.2";
const char* host = "shelfclock";
const int   daylightOffset_sec = 3600;
const char* ntpServer = "pool.ntp.org";

#if HAS_BUZZER
  const int valid_durations[] = {1, 2, 4, 8, 16, 32};
  const int valid_octaves[] = {4, 5, 6, 7};
  const int valid_beats[] = {25, 28, 31, 35, 40, 45, 50, 56, 63, 70, 80, 90, 100, 112, 125, 140, 160, 180, 200, 225, 250, 285, 320, 355, 400, 450, 500, 565, 635, 715, 800, 900};
  int totalSongs = 0;
  const size_t songTaskbufferSize = 128;  // Adjust the buffer size as per your requirements
  char songTaskbuffer[songTaskbufferSize];
  #endif
#if SHELFCLOCK_STANDALONE
shelfclock::standalone::NetworkStatus standaloneNetworkStatus;
#endif
shelfclock::runtime::HostServices *activeHostServices = nullptr;
int breakOutSet = 0; //jump out of count
int colorWheelPosition = 255; // COLOR WHEEL POSITION
int colorWheelPositionTwo = 255; // 2nd COLOR WHEEL POSITION
const int colorWheelSpeed = 3;
int sleepTimerCurrent = 0;
int isAsleep = 0;
#if HAS_PHOTOSENSOR
  int photoresisterReadings[PHOTO_SAMPLES] = {0};      // the readings from the analog input
  int readIndex = 0;              // the index of the current reading
  int lightSensorValue = 255;
  static long  brightnessSum   = 0;
  static bool  brightnessInited = false;
  #endif
int previousTimeMin = 0;
int previousTimeHour = 0;
int previousTimeDay = 0;
int previousTimeWeek = 0;
int previousTimeMonth = 0;
byte randomMinPassed = 1;
byte randomHourPassed = 1;
byte randomDayPassed = 1;
byte randomWeekPassed = 1;
byte randomMonthPassed = 1;
bool dotsOn = true;
unsigned long prevTime = 0;
unsigned long prevTime2 = 0;
unsigned long countdownMilliSeconds;
unsigned long endCountDownMillis;
byte  fakeclockrunning = 0;    
unsigned long countupMilliSeconds;
unsigned long CountUpMillis;
int scoreboardLeft = 0;
int scoreboardRight = 0; 
int currentMode = 0;
int currentReal = 0;
int cylonPosition = 0;
int clearOldLeds = 0; 
byte rain[SPECTRUM_PIXELS];
byte greenMatrix[SPECTRUM_PIXELS];
int lightshowSpeed = 1;
int snakeLastDirection = 0;  //snake's last dirction
int snakePosition = 0;  //snake's position
int foodSpot = random(SPECTRUM_PIXELS-1);  //food spot
int snakeWaiting = 0;  //waiting
int getSlower = 180;
int daysUptime = 0;
int hoursUptime = 0;
int minutesUptime = 0;
#if HAS_ONLINEWEATHER || HAS_USWEATHER
  #define LAT_SIZE 15
  #define LON_SIZE 15
  #define API_SIZE 50
  char latitude[LAT_SIZE] = "47.070808";    // Default value if needed  47.070808
  char longitude[LON_SIZE] = "-122.804500";// Default value if needed  -122.804500
  bool humidity_outdoor_enable = 0;
  bool temperature_outdoor_enable = 0;
  float outdoorTemp = -500;
  float outdoorHumidity = -1;
  #endif
#if HAS_USWEATHER
  int rainForecast[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // Global array to store rain forecast data
  enum PrecipitationType : uint8_t { PRECIP_UNKNOWN, PRECIP_RAIN, PRECIP_SNOW, PRECIP_MIXED };
  uint8_t precipitationType[14] = {};
  portMUX_TYPE rainForecastMux = portMUX_INITIALIZER_UNLOCKED;
  float noaaTemp = 0.0f;
  float noaaHumidity = 0.0f;
  float tideHeight[48];   // Stores hourly tide heights 
  char textTides[128] = "H 00:00 0.0 ft L 00:00 0.0 ft H 00:00 0.0 ft L 00:00 0.0 ft";    // Stores high/low tide information as a formatted string
  #define SID_SIZE 8
  char stationID[SID_SIZE] = "9446484"; // Default station ID, can be modified by the user 9434939  https://tidesandcurrents.noaa.gov/map/index.html
  uint32_t tideColor[14];   // Stores RGB colors for each hour based on tide height
  bool dailyPollSuccess = false; //did the tide poll work?
  int dailyPollFailed = 0;
  static const uint8_t WEATHER_UPDATE_RAIN = 0x01;
  static const uint8_t WEATHER_UPDATE_TIDES = 0x02;
  volatile uint8_t weatherUpdateRequests = 0;
  portMUX_TYPE weatherUpdateMux = portMUX_INITIALIZER_UNLOCKED;
  #endif
#if HAS_ONLINEWEATHER
    char apikey[API_SIZE] = "";
  #endif
#if HAS_BUZZER
  bool useAudibleAlarm = 0;
  char defaultAudibleAlarm[100] = "Final Countdown";
  char specialAudibleAlarm[100] = "Final Countdown";
  DynamicJsonDocument listOfSong(8192);
  char songsArray[64][64];
  char filesArray[64][64];
  #endif

bool updateSettingsRequired = 0;

DynamicJsonDocument jsonDoc(8192); // create a JSON document to store the data
DynamicJsonDocument jsonScheduleData(8192);  //stores schedules
File fsUploadFile;
#if HAS_BUZZER
bool lastSongUploadOk = false;
String lastSongUploadMessage;
char lastSongUploadPath[256];
#endif

struct tm timeinfo; 
CRGB LEDs[NUM_LEDS];
#if HAS_DHT
  int altitudeLocal = 81;  //in meters
#endif
WebServer server(80);
#if SHELFCLOCK_STANDALONE
AutoConnect      Portal(server);
#endif
bool hostedWebServerStarted = false;
bool networkIdentityChecked = false;

bool networkRecoveryRequested = false;
unsigned long networkRecoveryAt = 0;
bool firmwareUpdateSucceeded = false;
bool firmwareUploadAccepted = false;
bool firmwareUploadInProgress = false;
bool firmwareRestartRequested = false;
unsigned long firmwareRestartAt = 0;

// global settings that get saved to flash via preffs
byte clockMode = 11;   // Clock modes: 0=Clock, 1=Countdown, 2=Temperature, 3=Scoreboard, 4=Stopwatch, 5=Lightshow, 6=Rainbows, 7=Date, 8=Humidity, 9=Spectrum, 10=Display Off, 11=Scroll
byte clockDisplayType = 3; //0-Center Times, 1-24-hour Military Time, 2-12-hour Space-Padded, 3-Blinking Center Light
byte dateDisplayType = 5; //0-Zero-Padded (MMDD), 1-Space-Padded (MMDD), 2-Center Dates (1MDD), 3-Just Day of Week (Sun), 4-Just Numeric Day (DD), 5-With "." Separator (MM.DD), 6-Just Year (YYYY)
byte tempDisplayType = 0; //0-Temperature with Degree and Type (79°F), 1-Temperature with just Type (79 F), 2-Temperature with just Degree (79°), 3-Temperature with Decimal (79.9), 4-Just Temperature (79)
byte humiDisplayType = 0; //0-Humidity with Symbol (34 H), 1-Humidity with Decimal (34.9), 2-Just Humidity (79)
byte pastelColors = 0;
byte temperatureSymbol = 39;   // 36=Celcius, 39=Fahrenheit check 'numbers'
bool DSTime = 0;
long gmtOffset_sec = -28800;
bool fakeTime = 0;
byte ClockColorSettings = 0;
byte DateColorSettings = 0;
byte tempColorSettings = 0;
byte humiColorSettings = 0;
int temperatureCorrection = 0;
int colonType = 0;
byte ColorChangeFrequency = 0;
byte brightness = 10;  //set to 10 for photoresister control at startup
String scrollText = "ShELFcLOCk";
bool colorchangeCD = 1;
int spectrumMode = 0;
int spectrumColorSettings = 2;
int spectrumBackgroundSettings = 0;
int realtimeMode = 0;
int spotlightsColorSettings = 0;
bool useSpotlights = 1;
int scrollColorSettings = 0;
bool scrollOverride = 0;
bool scrollOptions1 = 0;   //Military Time (HHMM)
bool scrollOptions2 = 0;   //Day of Week (DOW)
bool scrollOptions3 = 0;   //Today's Date (DD-MM)
bool scrollOptions4 = 0;   //Year (YYYY)
bool scrollOptions5 = 0;   //Temperature (70 °F)
bool scrollOptions6 = 0;   //Humidity (47 H)
bool scrollOptions7 = 0;   //Text Message
bool scrollOptions8 = 1;   //IP Address of Clock
bool scrollOptions9 = 0;   //Tide Forecast
int scrollFrequency = 1;
int lightshowMode = 0;
byte randomSpectrumMode = 0;
int suspendFrequency = 1;  //in minutes
byte suspendType = 0; //0-off, 1-digits-only, 2-everything
bool firstRun = 0;  //will slow down the startup for diagnosing boot issues
#if HAS_SOUNDDETECTOR
  static int prevPeak[SOUNDDETECTOR_BANDS_WIDTH] = {0}; // Initialize all elements to 0
#endif
uint32_t countdownColorValue = 0x00FF04; 
uint32_t spectrumColorValue = 0xFF0000;  // default red
uint32_t spectrumBackgroundColorValue = 0x000000;  
uint32_t hourColorValue = 0xFF0000;  // default red
uint32_t colonColorValue = 0xFF0000;  // default red
uint32_t minColorValue = 0xFF0000;  // default red
uint32_t spotlightsColorValue = 0xE8EEBA;  
uint32_t scrollColorValue = 0xFFFFFF;  
uint32_t humiColorValue = 0x0033FF; 
uint32_t humiSymbolColorValue = 0x0033FF; 
uint32_t humiDecimalColorValue = 0x0033FF; 
uint32_t scoreboardColorLeftValue = 0x0091FF;  
uint32_t scoreboardColorRightValue = 0x00FF6E;  
uint32_t tempColorValue = 0xFFF700;  
uint32_t typeColorValue = 0xFFF700;  
uint32_t degreeColorValue = 0xFFF700;  
uint32_t dayColorValue = 0xEE00FF; 
uint32_t monthColorValue = 0xEE00FF;  
uint32_t separatorColorValue = 0xEE00FF; 
CRGB lightchaseColorOne = CRGB::Blue;
CRGB lightchaseColorTwo = CRGB::Red;
struct ChaseRuntimeState {
  bool active = false;
  bool forward = true;
  int position = 0;
  int mapping = 0;
};
ChaseRuntimeState chaseState;
struct SegmentPathRuntimeState {
  bool active = false;
  uint32_t startedAt = 0;
  uint32_t lastFrameAt = 0;
  uint32_t frame = 0;
};
SegmentPathRuntimeState segmentPathState;
int observedClockMode = -1;
int observedLightshowMode = -1;
struct TimerEndRuntimeState {
  bool active = false;
  uint16_t framesRemaining = 0;
};
TimerEndRuntimeState timerEndState;
CRGB alternateColor = CRGB::Black; 
CRGB oldsnakecolor = CRGB::Green;
CRGB spotcolor = CHSV(random(0, 255), 255, 255);
CRGB countdownColor = CRGB(countdownColorValue);
CRGB spectrumColor = CRGB(spectrumColorValue);
CRGB spectrumBackgroundColor = CRGB(spectrumBackgroundColorValue);
CRGB hourColor = CRGB(hourColorValue);
CRGB colonColor = CRGB(colonColorValue); 
CRGB minColor = CRGB(minColorValue);
CRGB tinyhourColor = CRGB(colonColorValue);
CRGB spotlightsColor = CRGB(spotlightsColorValue);
CRGB scrollColor = CRGB(scrollColorValue);
CRGB humiColor = CRGB(humiColorValue);
CRGB tinyhumiColor = CRGB(humiColorValue);
CRGB humiSymbolColor = CRGB(humiSymbolColorValue);
CRGB humiDecimalColor = CRGB(humiDecimalColorValue);
CRGB scoreboardColorLeft = CRGB(scoreboardColorLeftValue);
CRGB scoreboardColorRight = CRGB(scoreboardColorRightValue);
CRGB tempColor = CRGB(tempColorValue);
CRGB tinytempColor = CRGB(tempColorValue);
CRGB typeColor = CRGB(typeColorValue);
CRGB degreeColor = CRGB(degreeColorValue);
CRGB dayColor = CRGB(dayColorValue);
CRGB monthColor = CRGB(monthColorValue);
CRGB tinymonthColor = CRGB(monthColorValue);
CRGB separatorColor = CRGB(separatorColorValue);

struct PendingScroll {
  String text;
  int startingClockMode;
  int startingRealtimeMode;
  uint32_t waitForSongRequest;
};

shelfclock::ScrollSequence scrollSequence;
std::vector<PendingScroll> pendingScrolls;
size_t pendingScrollIndex = 0;
int activeScrollClockMode = 0;
int activeScrollRealtimeMode = 0;

static bool scrollIsBusy();
static void cancelScrollIfInterrupted();
static void serviceScroll();
static void scrollAfterSong(String text, uint32_t songRequestId);
static void serviceTimerEndEffect();

//  real6     fake5     real4     fake3     real2     fake1     real0
// RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR 
//R       R F       F R       R F       F R       R F       F R       R
//R   S   R F   S   F R   S   R F   S   F R   S   R F   S   F R   S   R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
// RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR 
//R       R F       F R       R F       F R       R F       F R       R
//R   S   R F   S   F R   S   R F   S   F R   S   R F   S   F R   S   R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
//R       R F       F R       R F       F R       R F       F R       R
// RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR   RRRRRRR 

//digit wiring order
//  4444444 
// 3       5
// 3       5
// 3       5
// 3       5
// 3       5
// 3       5
// 3       5
//  6666666 
// 2       0
// 2       0
// 2       0
// 2       0
// 2       0
// 2       0
// 2       0
//  1111111 
// byte numbers digit order 0b6543210
byte numbers[] = { 
   0b0111111,   //  [0] 0
   0b0100001,   //  [1] 1
   0b1110110,   //  [2] 2
   0b1110011,   //  [3] 3
   0b1101001,   //  [4] 4
   0b1011011,   //  [5] 5
   0b1011111,   //  [6] 6
   0b0110001,   //  [7] 7
   0b1111111,   //  [8] 8
   0b1111011,   //  [9] 9
   0b0000000,   //  [10] all off, space
   0b0100001,   //  [11] !
   0b0101000,   //  [12] "
   0b1101111,   //  [13] #
   0b1011011,   //  [14] $
   0b1100100,   //  [15] %
   0b1100001,   //  [16] &
   0b0001000,   //  [17] '
   0b0011010,   //  [18] (
   0b0110010,   //  [19] )
   0b0011000,   //  [20] *
   0b1001100,   //  [21] +
   0b0000100,   //  [22] ,
   0b1000000,   //  [23] -
   0b0000010,   //  [24] .
   0b1100100,   //  [25] /
   0b1111000,   //  [26] degrees symbol
   0b0010010,   //  [27] :
   0b0010011,   //  [28] ;
   0b1011000,   //  [29] <
   0b1000010,   //  [30] =
   0b1110000,   //  [31] >
   0b1110100,   //  [32] ?
   0b1110111,   //  [33] @
   0b1111101,   //  [34] A
   0b1001111,   //  [35] B
   0b0011110,   //  [36] C(elsius)
   0b1100111,   //  [37] D
   0b1011110,   //  [38] E
   0b1011100,   //  [39] F(ahrenheit)
   0b0011111,   //  [40] G
   0b1101101,   //  [41] H(umidity)
   0b0001100,   //  [42] I
   0b0100111,   //  [43] J
   0b1011101,   //  [44] K
   0b0001110,   //  [45] L
   0b0010101,   //  [46] M
   0b0111101,   //  [47] N
   0b0111111,   //  [48] O
   0b1111100,   //  [49] P
   0b1111010,   //  [50] Q
   0b0111100,   //  [51] R
   0b1011011,   //  [52] S
   0b1001110,   //  [53] T
   0b0101111,   //  [54] U
   0b0101111,   //  [55] V
   0b0101010,   //  [56] W
   0b1101101,   //  [57] X
   0b1101011,   //  [58] Y
   0b1110110,   //  [59] Z
   0b0011110,   //  [60] left bracket
   0b1001001,   //  [61] backslash
   0b0110011,   //  [62] right bracket
   0b0111000,   //  [63] carrot
   0b0000010,   //  [64] underscore
   0b0100000,   //  [65] aposhtophe
   0b1110111,   //  [66] a
   0b1001111,   //  [67] b
   0b1000110,   //  [68] c
   0b1100111,   //  [69] d
   0b1111110,   //  [70] e
   0b1011100,   //  [71] f
   0b1111011,   //  [72] g
   0b1001101,   //  [73] h
   0b0000100,   //  [74] i
   0b0000011,   //  [75] j
   0b1011101,   //  [76] k
   0b0001100,   //  [77] l
   0b0000101,   //  [78] m
   0b1000101,   //  [79] n
   0b1000111,   //  [80] o
   0b1111100,   //  [81] p
   0b1111001,   //  [82] q
   0b1000100,   //  [83] r
   0b1011011,   //  [84] s
   0b1001110,   //  [85] t
   0b0000111,   //  [86] u
   0b0000111,   //  [87] v
   0b0000101,   //  [88] w
   0b1101101,   //  [89] x
   0b1101011,   //  [90] y
   0b1110110,   //  [91] z
   0b1100001,   //  [92] left squigly bracket
   0b0001100,   //  [93] pipe
   0b1001100,   //  [94] right squigly bracket
   0b0010000,   //  [95] tilde
   0b1010010    //  [96] 3-lines (special pad)
   };

TaskHandle_t Task1;

static bool copyCString(char *destination, size_t destinationSize, const char *source) {
  if (destinationSize == 0 || source == nullptr) {
    return false;
  }

  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
  return true;
}

void shelfclock::runtime::setHostServices(HostServices &services) {
  activeHostServices = &services;
}

static shelfclock::runtime::HostServices &platformServices() {
  return *activeHostServices;
}

#if HAS_BUZZER
struct SongQueueMessage {
  char value[songTaskbufferSize];
  uint32_t requestId;
  bool inlineRtttl;
};

QueueHandle_t jobQueue;
volatile bool rtttlStopRequested = false;
volatile bool rtttlActive = false;
volatile uint32_t completedSongRequestId = 0;
uint32_t nextSongRequestId = 0;
uint32_t activeSongRequestId = 0;
String activeRtttlBuffer;
#if !SHELFCLOCK_STANDALONE && defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
bool rtttlPinAttached = false;
#endif

void copyAlarmName(char *destination, size_t destinationSize, const char *songName) {
  copyCString(destination, destinationSize, songName);
}

static int scheduleMonthDays(int month, int year) {
  if (month == 2) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 29 : 28;
  }

  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }

  return 31;
}

static bool monthlyScheduleMatchesDate(JsonObject jsonObj, int month, int day, int year) {
  int dom = (int)jsonObj["dom"];
  int currentEOM = scheduleMonthDays(month, year);
  return (dom == day) || (dom == 34 && day == currentEOM) || (dom == 33 && day == (currentEOM - 1)) || (dom == 32 && day == (currentEOM - 2));
}

void refreshSpecialAudibleAlarmForDate(int month, int day, int year) {
  copyAlarmName(specialAudibleAlarm, sizeof(specialAudibleAlarm), defaultAudibleAlarm);

  int object_count = jsonScheduleData.size();
  for (int i = 0; i < object_count; i++) {
    JsonObject jsonObj = jsonScheduleData[i];
    int scheduleType = (int)jsonObj["scheduleType"];
    bool dateMatches = false;

    if (scheduleType == 1) {
      dateMatches = monthlyScheduleMatchesDate(jsonObj, month, day, year);
    } else if (scheduleType == 3) {
      dateMatches = ((int)jsonObj["month"] == month && (int)jsonObj["day"] == day);
    }

    if (dateMatches && !jsonObj["song"].isNull()) {
      copyAlarmName(specialAudibleAlarm, sizeof(specialAudibleAlarm), jsonObj["song"].as<const char*>());
    }
  }
}

void refreshSpecialAudibleAlarmFromCurrentTime() {
  refreshSpecialAudibleAlarmForDate(timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
}

String songPathForName(const char *songName) {
  if (songName == nullptr || songName[0] == '\0') {
    return "";
  }

  if (songName[0] == '/' && FileFS.exists(songName)) {
    return String(songName);
  }

  JsonObjectConst songs = listOfSong.as<JsonObjectConst>();
  JsonVariantConst songPath = songs[songName];
  if (!songPath.isNull()) {
    return songPath.as<String>();
  }

  return "";
}

String alarmSongPath(const char *preferredSongName) {
  String songPath = songPathForName(preferredSongName);
  if (songPath.length() > 0) {
    return songPath;
  }

  Serial.print("Alarm song not found: ");
  Serial.println(preferredSongName == nullptr ? "(null)" : preferredSongName);

  songPath = songPathForName(defaultAudibleAlarm);
  if (songPath.length() > 0) {
    return songPath;
  }

  songPath = songPathForName("Final Countdown");
  if (songPath.length() > 0) {
    return songPath;
  }

  if (FileFS.exists("/settings/countdown.rttl")) {
    return "/settings/countdown.rttl";
  }

  Serial.println("No alarm song path resolved");
  return "";
}

static uint32_t queueSongRequest(const String &value, bool inlineRtttl) {
  if (value.length() == 0) {
    Serial.println(inlineRtttl ? "RTTTL buffer empty" : "song path empty");
    return 0;
  }
  if (jobQueue == NULL) {
    Serial.println("song queue unavailable");
    return 0;
  }

  SongQueueMessage message = {};
  value.toCharArray(message.value, sizeof(message.value));
  message.requestId = nextSongRequestId + 1;
  if (message.requestId == 0) {
    message.requestId = 1;
  }
  message.inlineRtttl = inlineRtttl;
  strncpy(songTaskbuffer, message.value, songTaskbufferSize);
  songTaskbuffer[songTaskbufferSize - 1] = '\0';

  if (xQueueSend(jobQueue, &message, (TickType_t)0) != pdTRUE) {
    Serial.println("song queue full");
    return 0;
  }

  nextSongRequestId = message.requestId;
  return message.requestId;
}

static uint32_t queueSongByPath(const String &songPath) {
  return queueSongRequest(songPath, false);
}

static uint32_t queueRtttlBuffer(const char *songBuffer) {
  return queueSongRequest(songBuffer == nullptr ? "" : String(songBuffer), true);
}

static void playRequestedSong(JsonVariantConst songValue) {
  String songName = listOfSong[songValue.as<String>()];
  Serial.print("songName ");
  Serial.println(songName);
  queueSongByPath(songName);
  Serial.println("play song only");
}

static void deleteRequestedSong(JsonVariantConst songValue) {
  String songKey = songValue.as<String>();
  Serial.print("Upload Size: ");
  Serial.println(songKey);
  String songPath = listOfSong[songKey];
  deleteFile(FileFS, songPath.c_str());
  getListOfSongs();
}

static void initRtttlPlayback() {
  pinMode(BUZZER_PIN, OUTPUT);
}

static void finishRtttlRequest(uint32_t requestId) {
  rtttlActive = false;
  activeSongRequestId = 0;
  activeRtttlBuffer = "";
  completedSongRequestId = requestId;
}

static bool startRtttlRequest(const SongQueueMessage &message) {
  if (message.inlineRtttl) {
    activeRtttlBuffer = message.value;
  } else {
    File file = FileFS.open(message.value, "r");
    Serial.print("goforit ");
    Serial.println(message.value);
    if (!file) {
      Serial.println("No Saved Data!");
      completedSongRequestId = message.requestId;
      return false;
    }
    activeRtttlBuffer = file.readString();
    file.close();
  }

  if (activeRtttlBuffer.length() == 0) {
    Serial.println("RTTTL buffer empty");
    completedSongRequestId = message.requestId;
    return false;
  }

  activeSongRequestId = message.requestId;
#if !SHELFCLOCK_STANDALONE && defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  // NonBlockingRTTTL 1.4.0 attaches the pin on every begin(). Arduino 3.x
  // requires it to be detached before it can be attached again.
  if (rtttlPinAttached) {
    ledcDetach(BUZZER_PIN);
  }
#endif
  rtttl::begin(BUZZER_PIN, activeRtttlBuffer.c_str());
#if !SHELFCLOCK_STANDALONE && defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  rtttlPinAttached = true;
#endif
  rtttlActive = true;
  return true;
}

static bool serviceRtttlPlayback() {
  if (rtttlStopRequested) {
    if (rtttlActive) {
      rtttl::stop();
      finishRtttlRequest(activeSongRequestId);
    }
    rtttlStopRequested = false;
  }

  if (rtttlActive) {
    rtttl::play();
    if (rtttl::done()) {
      finishRtttlRequest(activeSongRequestId);
    }
  }

  if (!rtttlActive && jobQueue != NULL) {
    SongQueueMessage message = {};
    if (xQueueReceive(jobQueue, &message, (TickType_t)0) == pdTRUE) {
      strncpy(songTaskbuffer, message.value, songTaskbufferSize);
      songTaskbuffer[songTaskbufferSize - 1] = '\0';
      Serial.print("job ");
      Serial.println(message.inlineRtttl ? "inline RTTTL" : message.value);
      startRtttlRequest(message);
    }
  }

  return rtttlActive || (jobQueue != NULL && uxQueueMessagesWaiting(jobQueue) > 0);
}

static void requestRtttlStop() {
  rtttlStopRequested = true;
}

static bool isRtttlPlayingSafe() {
  return rtttlActive;
}

static bool songRequestIsComplete(uint32_t requestId) {
  return requestId == 0 || static_cast<int32_t>(completedSongRequestId - requestId) >= 0;
}
#endif

static int configuredTimeIsDst() {
  #if SHELFCLOCK_ISOLATION
  return 0;
  #else
  return DSTime ? 1 : 0;
  #endif
}

static bool enterDisplayMode(byte nextClockMode, int nextRealtimeMode, bool persistentMode, bool shouldBreakOut) {
  timerEndState.active = false;
  timerEndState.framesRemaining = 0;
  allBlank();
  clockMode = nextClockMode;
  realtimeMode = nextRealtimeMode;
  if (persistentMode) {
    updateSettingsRequired = 1;
  }
  if (shouldBreakOut) {
    breakOutSet = 1;
  }
  return persistentMode;
}

static void registerUserDisplayActivity(shelfclock::HomeCommandType commandType) {
  if (commandType == shelfclock::HomeCommandType::DisplayOff ||
      commandType == shelfclock::HomeCommandType::SavePreset) {
    return;
  }
  sleepTimerCurrent = 0;
  isAsleep = 0;
}

static void submitHomeCommand(const shelfclock::HomeCommand &command, bool &saveSettingsNow) {
  const shelfclock::HomeCommandResult result = shelfclock::applyHomeCommand(command);
  if (result.accepted) {
    registerUserDisplayActivity(command.type);
  }
  saveSettingsNow |= result.saveSettingsNow;
}

static void applyDisplayModeCommands(JsonObjectConst json, bool &saveSettingsNow) {
  // ClockMode
  if (!json["ClockMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::Clock), saveSettingsNow);
  }

  // DateMode
  if (!json["DateMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::Date), saveSettingsNow);
  }

  // TemperatureMode
  if (!json["TemperatureMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::Temperature), saveSettingsNow);
  }

  // HumidityMode
  if (!json["HumidityMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::Humidity), saveSettingsNow);
  }

  // ScrollingMode
  if (!json["ScrollingMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::Scrolling), saveSettingsNow);
  }

  // DisplayOffMode
  if (!json["DisplayOffMode"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::DisplayOff), saveSettingsNow);
  }
}

static int clampIntValue(int value, int minimum, int maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

static bool readLoggedPostJson(DynamicJsonDocument &json, String &body) {
  if (server.args() <= 0) {
    return false;
  }

  body = server.arg(0);
  Serial.println("----");
  Serial.println(body);
  Serial.println("----");
  deserializeJson(json, body);
  return true;
}

static void assignJsonBoolean(JsonVariantConst value, bool &target) {
  if (value == true) {
    target = 1;
  }
  if (value == false) {
    target = 0;
  }
  updateSettingsRequired = 1;
}

static void assignJsonBoolean(JsonVariantConst value, byte &target) {
  if (value == true) {
    target = 1;
  }
  if (value == false) {
    target = 0;
  }
  updateSettingsRequired = 1;
}

static void assignJsonBoolean(JsonVariantConst value, bool &target, int refreshClockMode) {
  assignJsonBoolean(value, target);
  if (refreshClockMode >= 0 && clockMode == refreshClockMode) {
    allBlank();
  }
}

static void assignJsonBoolean(JsonVariantConst value, byte &target, int refreshClockMode) {
  assignJsonBoolean(value, target);
  if (refreshClockMode >= 0 && clockMode == refreshClockMode) {
    allBlank();
  }
}

static void assignTimerJsonBoolean(JsonVariantConst value, bool &target) {
  assignJsonBoolean(value, target);
  if ((clockMode == 1) || (clockMode == 4)) {
    allBlank();
  }
}

static bool copyJsonCString(JsonVariantConst value, char *destination, size_t destinationSize) {
  return copyCString(destination, destinationSize, value.as<const char*>());
}

static void loadJsonColor(JsonVariantConst value, uint32_t &targetValue, CRGB &targetColor) {
  targetValue = value.as<uint32_t>();
  targetColor = CRGB(targetValue);
}

static void markSettingChanged(int refreshClockMode) {
  updateSettingsRequired = 1;
  if (refreshClockMode >= 0 && clockMode == refreshClockMode) {
    allBlank();
  }
}

static void syncConfiguredTimeSetting() {
  #if !SHELFCLOCK_ISOLATION
  platformServices().configureNetworkTime(gmtOffset_sec, (daylightOffset_sec * DSTime), ntpServer);
  bool localTimeOk = platformServices().readLocalTime(timeinfo);
  if(!localTimeOk){Serial.println("Error, no NTP Server found!");}
  #if HAS_RTC
    if (localTimeOk) {
      platformServices().writeRtc(timeinfo);
    }
  #endif
  #else
  Serial.println("Isolation keeps local wall time; timezone/DST settings saved for network builds");
  #endif
  markSettingChanged(0);
  printLocalTime();
}

static void applyGmtOffsetSetting(JsonVariantConst value) {
  gmtOffset_sec = value.as<long>();
  syncConfiguredTimeSetting();
}

static void applyDstSetting(JsonVariantConst value) {
  assignJsonBoolean(value, DSTime);
  syncConfiguredTimeSetting();
}

static void assignJsonByte(JsonVariantConst value, byte &target, int refreshClockMode) {
  target = value.as<byte>();
  markSettingChanged(refreshClockMode);
}

static void assignJsonInt(JsonVariantConst value, int &target, int refreshClockMode) {
  target = value.as<int>();
  markSettingChanged(refreshClockMode);
}

static void assignJsonString(JsonVariantConst value, String &target, const char *fallback, int refreshClockMode) {
  target = value.as<String>();
  if (fallback != nullptr && target == "") {
    target = fallback;
  }
  markSettingChanged(refreshClockMode);
}

static void assignJsonColor(JsonVariantConst value, uint32_t &targetValue, CRGB &targetColor, int refreshClockMode) {
  loadJsonColor(value, targetValue, targetColor);
  markSettingChanged(refreshClockMode);
}

static void resetRandomColorChangeCycle() {
  randomMinPassed = 1;   //used to force the first change for the next cycle
  randomHourPassed = 1;   //used to force the first change for the next cycle
  randomDayPassed = 1;   //used to force the first change for the next cycle
  randomWeekPassed = 1;   //used to force the first change for the next cycle
  randomMonthPassed = 1;   //used to force the first change for the next cycle
}

static void applyColorChangeFrequencySetting(JsonVariantConst value) {
  assignJsonByte(value, ColorChangeFrequency, -1);
  resetRandomColorChangeCycle();
  allBlank();
}

static void applyBrightnessSetting(JsonVariantConst value) {
  assignJsonByte(value, brightness, -1);
  ShelfDownLights();
}

static void applySpotlightsColorSetting(JsonVariantConst value) {
  assignJsonColor(value, spotlightsColorValue, spotlightsColor, -1);
  ShelfDownLights();
}

static void applyCountdownColorSetting(JsonVariantConst value) {
  assignJsonColor(value, countdownColorValue, countdownColor, -1);
  if ((clockMode == 1) || (clockMode == 4)) {
    allBlank();
  }
}

static void savePresetSettings(const char *presetName) {
  saveclockSettings(presetName);
}

static void loadPresetSettings(const char *presetName) {
  getclockSettings(presetName);
  allBlank();
}

static void applySavePresetCommands(JsonObjectConst json) {
  bool saveSettingsNow = false;
  // setpreset1
  if (!json["setpreset1"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::SavePreset, 1), saveSettingsNow);
  }

  // setpreset2
  if (!json["setpreset2"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::SavePreset, 2), saveSettingsNow);
  }

  // setpreset3
  if (!json["setpreset3"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::SavePreset, 3), saveSettingsNow);
  }

  // setpreset4
  if (!json["setpreset4"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::SavePreset, 4), saveSettingsNow);
  }
}

static void applyLoadPresetCommands(JsonObjectConst json) {
  bool saveSettingsNow = false;
  // loadpreset1
  if (!json["loadPreset1"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::LoadPreset, 1), saveSettingsNow);
  }

  // loadpreset2
  if (!json["loadPreset2"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::LoadPreset, 2), saveSettingsNow);
  }

  // loadpreset3
  if (!json["loadPreset3"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::LoadPreset, 3), saveSettingsNow);
  }

  // loadpreset4
  if (!json["loadPreset4"].isNull()) {
    submitHomeCommand(shelfclock::HomeCommand(shelfclock::HomeCommandType::LoadPreset, 4), saveSettingsNow);
  }
}

static void applyManualDateTime(const struct tm &value) {
  struct tm tm = value;
  tm.tm_isdst = configuredTimeIsDst();
  #if HAS_RTC
    platformServices().writeRtc(tm);   //set time on the RTC of the DS3231
  #endif
  time_t t = mktime(&tm);
  struct timeval now1 = { .tv_sec = t };
  settimeofday(&now1, NULL);    //set time on the RTC of the ESP32
  printLocalTime();
}

static void applyManualDateTime(JsonVariantConst value) {
  struct tm tm = {};
  tm.tm_year = value["year"].as<int>() - 1900;
  tm.tm_mon = value["month"].as<int>() - 1;
  tm.tm_mday = value["day"].as<int>();
  tm.tm_hour = value["hour"].as<int>();
  tm.tm_min = value["min"].as<int>();
  tm.tm_sec = value["sec"].as<int>();
  applyManualDateTime(tm);
}

bool shelfclock::runtime::synchronizeDateTimeFromHost() {
  struct tm currentTime = {};
  if (!platformServices().readLocalTime(currentTime)) {
    return false;
  }
  applyManualDateTime(currentTime);
  return true;
}

static void applyManualDateTimeCommand(JsonObjectConst json) {
  // setdate
  if (!json["setdate"].isNull()) {
    applyManualDateTime(json["setdate"]);
  }
}

#if HAS_BUZZER
static void applyDefaultAudibleAlarm(JsonVariantConst value) {
  copyAlarmName(defaultAudibleAlarm, sizeof(defaultAudibleAlarm), value.as<const char*>());
  refreshSpecialAudibleAlarmFromCurrentTime();
  updateSettingsRequired = 1;
}
#endif

static void applyDefaultAudibleAlarmCommand(JsonObjectConst json) {
  #if HAS_BUZZER
    // set default music
    if (!json["defaultAudibleAlarm"].isNull()) {
      applyDefaultAudibleAlarm(json["defaultAudibleAlarm"]);
    }
  #endif
}

#if HAS_ONLINEWEATHER || HAS_USWEATHER
static void applyWeatherLocationSettings(JsonVariantConst weatherapi) {
  if (!weatherapi["latitude"].isNull()) {
    copyJsonCString(weatherapi["latitude"], latitude, sizeof(latitude));
  }
  if (!weatherapi["longitude"].isNull()) {
    copyJsonCString(weatherapi["longitude"], longitude, sizeof(longitude));
  }
  Serial.println(latitude);
  Serial.println(longitude);
  updateSettingsRequired = 1;
}
#endif

#if HAS_ONLINEWEATHER
static void applyWeatherApiKeySetting(JsonVariantConst weatherapi) {
  if (!weatherapi["apikey"].isNull()) {
    copyJsonCString(weatherapi["apikey"], apikey, sizeof(apikey));
  }
  Serial.println(apikey);
  updateSettingsRequired = 1;
}
#endif

static void applySpotlightsEnabled(JsonVariantConst value) {
  assignJsonBoolean(value, useSpotlights);
  ShelfDownLights();
}

#if HAS_ONLINEWEATHER || HAS_USWEATHER
static void applyOutdoorWeatherEnabled(JsonVariantConst value, bool &target) {
  assignJsonBoolean(value, target);
  Serial.println(target);
}
#endif

static void startTimerDisplayMode(bool countdownRequested, int requestedMilliseconds) {
  CountUpMillis = countdownRequested ? CountUpMillis : millis();
  countdownMilliSeconds = clampIntValue(requestedMilliseconds, 1000, 86400000);
  endCountDownMillis = millis() + countdownMilliSeconds;
  if (currentMode == 0) {
    currentMode = clockMode;
    currentReal = realtimeMode;
  }
  enterDisplayMode(countdownRequested ? 1 : 4, 0, false, false);
}

static bool enterScoreboardDisplayMode(int leftScore, int rightScore) {
  scoreboardLeft = clampIntValue(leftScore, 0, 99);
  scoreboardRight = clampIntValue(rightScore, 0, 99);
  return enterDisplayMode(3, 0, true, false);
}

static bool enterLightshowDisplayMode(int requestedLightshowMode) {
  lightshowMode = requestedLightshowMode;
  oldsnakecolor = CRGB::Green;
  getSlower = 180;
  return enterDisplayMode(5, 1, true, false);
}

static bool enterSpectrumDisplayMode(int requestedSpectrumMode) {
  spectrumMode = requestedSpectrumMode;
  return enterDisplayMode(9, 1, true, false);
}

namespace shelfclock {

HomeCommandResult applyHomeCommand(const HomeCommand &command) {
  HomeCommandResult result;

  switch (command.type) {
    case HomeCommandType::Clock:
      result.saveSettingsNow = enterDisplayMode(0, 0, true, false);
      printLocalTime();
      breakOutSet = 1;
      break;
    case HomeCommandType::Date:
      result.saveSettingsNow = enterDisplayMode(7, 0, true, false);
      break;
    case HomeCommandType::Temperature:
      result.saveSettingsNow = enterDisplayMode(2, 0, true, false);
      break;
    case HomeCommandType::Humidity:
      result.saveSettingsNow = enterDisplayMode(8, 0, true, false);
      break;
    case HomeCommandType::Scrolling:
      result.saveSettingsNow = enterDisplayMode(11, 0, true, false);
      break;
    case HomeCommandType::DisplayOff:
      result.saveSettingsNow = enterDisplayMode(10, 0, true, false);
      #if HAS_BUZZER
        requestRtttlStop();
      #endif
      breakOutSet = 1;
      break;
    case HomeCommandType::Countdown:
      startTimerDisplayMode(true, command.primary);
      break;
    case HomeCommandType::Stopwatch:
      startTimerDisplayMode(false, command.primary);
      break;
    case HomeCommandType::Scoreboard:
      result.saveSettingsNow = enterScoreboardDisplayMode(command.primary, command.secondary);
      break;
    case HomeCommandType::Lightshow:
      result.saveSettingsNow = enterLightshowDisplayMode(command.primary);
      break;
    case HomeCommandType::Spectrum:
      result.saveSettingsNow = enterSpectrumDisplayMode(command.primary);
      break;
    case HomeCommandType::SavePreset: {
      const char *presetName = presetStorageName(command.primary);
      if (presetName == nullptr) {
        return result;
      }
      savePresetSettings(presetName);
      break;
    }
    case HomeCommandType::LoadPreset: {
      const char *presetName = presetStorageName(command.primary);
      if (presetName == nullptr) {
        return result;
      }
      loadPresetSettings(presetName);
      break;
    }
  }

  result.accepted = true;
  return result;
}

HomeState getHomeState() {
  HomeState state;
  state.clockMode = clockMode;
  state.realtimeMode = realtimeMode;
  state.scoreboardLeft = scoreboardLeft;
  state.scoreboardRight = scoreboardRight;
  state.lightshowMode = lightshowMode;
  state.spectrumMode = spectrumMode;
  return state;
}

}  // namespace shelfclock

static void applyScheduledPreset(int presetNumber) {
  const char *presetName = shelfclock::presetStorageName(presetNumber);
  if (presetName == nullptr) {
    return;
  }

  char presetPath[64];
  snprintf(presetPath, sizeof(presetPath), "/settings/clockSettings-%s.json", presetName);
  if (FileFS.exists(presetPath)) {
    shelfclock::applyHomeCommand(
      shelfclock::HomeCommand(shelfclock::HomeCommandType::LoadPreset, presetNumber)
    );
  }
}

static void applyRealtimeModeCommands(JsonObjectConst json, bool &saveSettingsNow) {
  // CountdownMode
  if (!json["CountdownMode"].isNull() || !json["StopwatchMode"].isNull()) {
    const bool countdownRequested = !json["CountdownMode"].isNull();
    submitHomeCommand(
      shelfclock::HomeCommand(
        countdownRequested ? shelfclock::HomeCommandType::Countdown : shelfclock::HomeCommandType::Stopwatch,
        countdownRequested ? json["CountdownMode"].as<int>() : json["StopwatchMode"].as<int>()
      ),
      saveSettingsNow
    );
  }

  // ScoreboardMode
  if (!json["ScoreboardMode"].isNull()) {
    submitHomeCommand(
      shelfclock::HomeCommand(
        shelfclock::HomeCommandType::Scoreboard,
        json["ScoreboardMode"]["left"].as<int>(),
        json["ScoreboardMode"]["right"].as<int>()
      ),
      saveSettingsNow
    );
  }

  // lightshowMode
  if (!json["lightshowMode"].isNull()) {
    submitHomeCommand(
      shelfclock::HomeCommand(shelfclock::HomeCommandType::Lightshow, json["lightshowMode"].as<int>()),
      saveSettingsNow
    );
  }

  // spectrumMode
  if (!json["spectrumMode"].isNull()) {
    submitHomeCommand(
      shelfclock::HomeCommand(shelfclock::HomeCommandType::Spectrum, json["spectrumMode"].as<int>()),
      saveSettingsNow
    );
  }
}

#if HAS_USWEATHER
static void requestWeatherUpdate(uint8_t requestFlags) {
  portENTER_CRITICAL(&weatherUpdateMux);
  weatherUpdateRequests |= requestFlags;
  portEXIT_CRITICAL(&weatherUpdateMux);
}

static void applyStationIdSetting(JsonVariantConst value) {
  copyJsonCString(value, stationID, sizeof(stationID));
  updateSettingsRequired = 1;
  requestWeatherUpdate(WEATHER_UPDATE_TIDES);
}

static uint8_t consumeWeatherUpdateRequests() {
  portENTER_CRITICAL(&weatherUpdateMux);
  uint8_t requestFlags = weatherUpdateRequests;
  weatherUpdateRequests = 0;
  portEXIT_CRITICAL(&weatherUpdateMux);
  return requestFlags;
}

static void processRequestedWeatherUpdates() {
  uint8_t requestFlags = consumeWeatherUpdateRequests();
  if (requestFlags == 0) {
    return;
  }

  if (requestFlags & WEATHER_UPDATE_RAIN) {
    updateRainForecast();
  }

  if (requestFlags & WEATHER_UPDATE_TIDES) {
    fetchTides();
    processCurrentTide();
  }
}
#endif

static void applyWeatherStationCommand(JsonObjectConst json) {
  #if HAS_USWEATHER
    // stationID
    if (!json["stationID"].isNull()) {
      applyStationIdSetting(json["stationID"]);
    }
  #endif
}

static void applyWeatherSettings(JsonObjectConst json) {
  #if HAS_ONLINEWEATHER || HAS_USWEATHER
    if (!json["weatherapi"].isNull()) {
      applyWeatherLocationSettings(json["weatherapi"]);
    }

    // humidity
    if (!json["humidity_outdoor_enable"].isNull()) {
      applyOutdoorWeatherEnabled(json["humidity_outdoor_enable"], humidity_outdoor_enable);
    }

    // temperature
    if (!json["temperature_outdoor_enable"].isNull()) {
      applyOutdoorWeatherEnabled(json["temperature_outdoor_enable"], temperature_outdoor_enable);
    }
  #endif

  #if HAS_ONLINEWEATHER
    if (!json["weatherapi"].isNull()) {
      applyWeatherApiKeySetting(json["weatherapi"]);
    }
  #endif
}

static void applySpotlightsColorModeSetting(JsonVariantConst value) {
  assignJsonInt(value, spotlightsColorSettings, -1);
  #if HAS_USWEATHER
    if (spotlightsColorSettings == 5 ){ requestWeatherUpdate(WEATHER_UPDATE_RAIN); }
    if (spotlightsColorSettings == 6 ){ requestWeatherUpdate(WEATHER_UPDATE_TIDES); }
  #endif
  ShelfDownLights(); 
}

static void applyScrollAndSpectrumOptionSettings(JsonObjectConst json) {
  // randomSpectrumMode
  if (!json["randomSpectrumMode"].isNull()) {
    assignJsonBoolean(json["randomSpectrumMode"], randomSpectrumMode, 9);
  }

  // scrollOptions1 Military Time
  if (!json["scrollOptions1"].isNull()) {
    assignJsonBoolean(json["scrollOptions1"], scrollOptions1, 11);
  }

  // scrollOptions2 Day of Week
  if (!json["scrollOptions2"].isNull()) {
    assignJsonBoolean(json["scrollOptions2"], scrollOptions2, 11);
  }

  // scrollOptions3 Date
  if (!json["scrollOptions3"].isNull()) {
    assignJsonBoolean(json["scrollOptions3"], scrollOptions3, 11);
  }

  // scrollOptions4 Year
  if (!json["scrollOptions4"].isNull()) {
    assignJsonBoolean(json["scrollOptions4"], scrollOptions4, 11);
  }

  // scrollOptions5 Temperature
  if (!json["scrollOptions5"].isNull()) {
    assignJsonBoolean(json["scrollOptions5"], scrollOptions5, 11);
  }

  // scrollOptions6 Humidity
  if (!json["scrollOptions6"].isNull()) {
    assignJsonBoolean(json["scrollOptions6"], scrollOptions6, 11);
  }

  // scrollOptions7 dAdS ArE tHE bESt
  if (!json["scrollOptions7"].isNull()) {
    assignJsonBoolean(json["scrollOptions7"], scrollOptions7, 11);
  }

  // scrollOptions8 IP Address of Clock
  if (!json["scrollOptions8"].isNull()) {
    assignJsonBoolean(json["scrollOptions8"], scrollOptions8, 11);
  }

  // scrollOptions9 Tide Forecast
  if (!json["scrollOptions9"].isNull()) {
    assignJsonBoolean(json["scrollOptions9"], scrollOptions9, 11);
    if (clockMode == 11) {
      #if HAS_USWEATHER
        if (scrollOptions9 == 1) { requestWeatherUpdate(WEATHER_UPDATE_TIDES); }
      #endif
    } 
  }

  // scrollOverride
  if (!json["scrollOverride"].isNull()) {
    assignJsonBoolean(json["scrollOverride"], scrollOverride, 11);
  }

  // scrollText
  if (!json["scrollText"].isNull()) {
    assignJsonString(json["scrollText"], scrollText, "dAdS ArE tHE bESt", 11);
  }
}

static void applyDisplayScalarSettings(JsonObjectConst json) {
  // pastelColors
  if (!json["pastelColors"].isNull()){
    assignJsonByte(json["pastelColors"], pastelColors, -1);
    allBlank();
  }

  // SuspendType
  if (!json["suspendType"].isNull()) {
    assignJsonByte(json["suspendType"], suspendType, -1);
  }

  // spotlightsColorSettings
  if (!json["spotlightsColorSettings"].isNull()) {
    applySpotlightsColorModeSetting(json["spotlightsColorSettings"]);
  }

  // ClockDisplayType
  if (!json["ClockDisplayType"].isNull()) {
    assignJsonByte(json["ClockDisplayType"], clockDisplayType, 0);
  }

  // colonType
  if (!json["colonType"].isNull()) {
    assignJsonInt(json["colonType"], colonType, 0);
  }

  // ClockColorSettings
  if (!json["ClockColorSettings"].isNull()) {
    assignJsonByte(json["ClockColorSettings"], ClockColorSettings, 0);
  }

  // dateDisplayType
  if (!json["dateDisplayType"].isNull()) {
    assignJsonByte(json["dateDisplayType"], dateDisplayType, 7);
  }

  // DateColorSettings
  if (!json["DateColorSettings"].isNull()) {
    assignJsonByte(json["DateColorSettings"], DateColorSettings, 7);
  }

  // temperatureSymbol
  if (!json["temperatureSymbol"].isNull()) {
    assignJsonByte(json["temperatureSymbol"], temperatureSymbol, 2);
  }

  // tempDisplayType
  if (!json["tempDisplayType"].isNull()) {
    assignJsonByte(json["tempDisplayType"], tempDisplayType, 2);
  }

  // tempColorSettings
  if (!json["tempColorSettings"].isNull()) {
    assignJsonByte(json["tempColorSettings"], tempColorSettings, 2);
  }

  // humiDisplayType
  if (!json["humiDisplayType"].isNull()) {
    assignJsonByte(json["humiDisplayType"], humiDisplayType, 8);
  }

  // humiColorSettings
  if (!json["humiColorSettings"].isNull()) {
    assignJsonByte(json["humiColorSettings"], humiColorSettings, 8);
  }

  // spectrumBackgroundSettings
  if (!json["spectrumBackgroundSettings"].isNull()) {
    assignJsonInt(json["spectrumBackgroundSettings"], spectrumBackgroundSettings, 9);
  }

  // spectrumColorSettings
  if (!json["spectrumColorSettings"].isNull()) {
    assignJsonInt(json["spectrumColorSettings"], spectrumColorSettings, 9);
  }

  // scrollColorSettings
  if (!json["scrollColorSettings"].isNull()) {
    assignJsonInt(json["scrollColorSettings"], scrollColorSettings, 11);
  }

  // ColorChangeFrequency
  if (!json["ColorChangeFrequency"].isNull()) {
    applyColorChangeFrequencySetting(json["ColorChangeFrequency"]);
  }

  // suspendFrequency
  if (!json["suspendFrequency"].isNull()) {
    assignJsonInt(json["suspendFrequency"], suspendFrequency, -1);
  }

  // gmtOffset_sec
  if (!json["gmtOffset_sec"].isNull()) {
    applyGmtOffsetSetting(json["gmtOffset_sec"]);
  }

  // temperatureCorrection
  if (!json["temperatureCorrection"].isNull()) {
    assignJsonInt(json["temperatureCorrection"], temperatureCorrection, 2);
  }

  // scrollFrequency
  if (!json["scrollFrequency"].isNull()) {
    assignJsonInt(json["scrollFrequency"], scrollFrequency, 11);
  }

  // rangeBrightness
  if (!json["rangeBrightness"].isNull()) {
    applyBrightnessSetting(json["rangeBrightness"]);
  }

  // spotlightsColor
  if (!json["spotlightsColor"].isNull()) {
    applySpotlightsColorSetting(json["spotlightsColor"]);
  }
}

static void applyColorAndToggleSettings(JsonObjectConst json) {
  // hourColor
  if (!json["hourColor"].isNull()) {
    assignJsonColor(json["hourColor"], hourColorValue, hourColor, 0);
  }

  // minColor
  if (!json["minColor"].isNull()) {
    assignJsonColor(json["minColor"], minColorValue, minColor, 0);
  }

  // colonColor
  if (!json["colonColor"].isNull()) {
    assignJsonColor(json["colonColor"], colonColorValue, colonColor, 0);
  }

  // dayColor
  if (!json["dayColor"].isNull()) {
    assignJsonColor(json["dayColor"], dayColorValue, dayColor, 7);
  }

  // monthColor
  if (!json["monthColor"].isNull()) {
    assignJsonColor(json["monthColor"], monthColorValue, monthColor, 7);
  }

  // separatorColor
  if (!json["separatorColor"].isNull()) {
    assignJsonColor(json["separatorColor"], separatorColorValue, separatorColor, 7);
  }

  // tempColor
  if (!json["tempColor"].isNull()) {
    assignJsonColor(json["tempColor"], tempColorValue, tempColor, 2);
  }

  // typeColor
  if (!json["typeColor"].isNull()) {
    assignJsonColor(json["typeColor"], typeColorValue, typeColor, 2);
  }

  // degreeColor
  if (!json["degreeColor"].isNull()) {
    assignJsonColor(json["degreeColor"], degreeColorValue, degreeColor, 2);
  }

  // humiColor
  if (!json["humiColor"].isNull()) {
    assignJsonColor(json["humiColor"], humiColorValue, humiColor, 8);
  }

  // humiDecimalColor
  if (!json["humiDecimalColor"].isNull()) {
    assignJsonColor(json["humiDecimalColor"], humiDecimalColorValue, humiDecimalColor, 8);
  }

  // humiSymbolColor
  if (!json["humiSymbolColor"].isNull()) {
    assignJsonColor(json["humiSymbolColor"], humiSymbolColorValue, humiSymbolColor, 8);
  }

  // countdownColor
  if (!json["countdownColor"].isNull()) {
    applyCountdownColorSetting(json["countdownColor"]);
  }

  // scoreboardColorLeft
  if (!json["scoreboardColorLeft"].isNull()) {
    assignJsonColor(json["scoreboardColorLeft"], scoreboardColorLeftValue, scoreboardColorLeft, 3);
  }

  // scoreboardColorRight
  if (!json["scoreboardColorRight"].isNull()) {
    assignJsonColor(json["scoreboardColorRight"], scoreboardColorRightValue, scoreboardColorRight, 3);
  }

  // spectrumColor
  if (!json["spectrumColor"].isNull()) {
    assignJsonColor(json["spectrumColor"], spectrumColorValue, spectrumColor, 9);
  }

  // scrollColor
  if (!json["scrollColor"].isNull()) {
    assignJsonColor(json["scrollColor"], scrollColorValue, scrollColor, 11);
  }

  // spectrumBackgroundColor
  if (!json["spectrumBackgroundColor"].isNull()) {
    assignJsonColor(json["spectrumBackgroundColor"], spectrumBackgroundColorValue, spectrumBackgroundColor, 9);
  }

  // useSpotlights
  if (!json["useSpotlights"].isNull()) {
    applySpotlightsEnabled(json["useSpotlights"]);
  }

  // DSTime
  if (!json["DSTime"].isNull()) {
    applyDstSetting(json["DSTime"]);
  }

  #if HAS_BUZZER
    // useAudibleAlarm
    if (!json["useAudibleAlarm"].isNull()) {
      assignTimerJsonBoolean(json["useAudibleAlarm"], useAudibleAlarm);
    }
  #endif

  // colorchangeCD
  if (!json["colorchangeCD"].isNull()) {
    assignTimerJsonBoolean(json["colorchangeCD"], colorchangeCD);
  }
}

static void setupClockBeforeHost() {
  #if SHELFCLOCK_STANDALONE
  Serial.begin(115200);
  #endif

  //setup LEDs
#if defined(FASTLED_ESP32_USE_CLOCKLESS_SPI)
  static ShelfClockSynchronousSpiController<LED_PIN, COLOR_ORDER> ledController;
  FastLED.addLeds(&ledController, LEDs, NUM_LEDS).setCorrection(TypicalLEDStrip);
#else
  FastLED.addLeds<LED_TYPE,LED_PIN,COLOR_ORDER>(LEDs,NUM_LEDS).setCorrection(TypicalLEDStrip);
#endif
  FastLED.setBrightness(255);
  fill_solid(LEDs, NUM_LEDS, CRGB::Black);
  FastLED.show();
  if (firstRun) {
  allTest();
  }
  fakeClock(2);  // blink 12:00 like old clocks once did
  platformServices().cooperateDuringSetup();
  #if HAS_SOUNDDETECTOR
    sampling_period_us = round(1000000 * (1.0 / SOUNDDETECTOR_SAMPLING_FREQ));
  #endif

  //display "LoAd" while activating the websever
   allBlank();
  displayNumber(45,6,CRGB::Red);  // L
  displayNumber(80,4,CRGB::Red);  // o
  displayNumber(34,2,CRGB::Red); // A
  displayNumber(69,0,CRGB::Red);  // d
  FastLED.show();
  if (firstRun) {delay(500);}
  loadWebPageHandlers();  //load about 900 webpage handlers from the bottom of this sketch
  platformServices().cooperateDuringSetup();

  // Initialize FileFS 
  //display "file" while activating the filesystem
  allBlank();
  displayNumber(39,6,CRGB::Purple);  // F
  displayNumber(42,4,CRGB::Purple);  // I
  displayNumber(45,2,CRGB::Purple); // L
  displayNumber(38,0,CRGB::Purple);  // E
  FastLED.show();
  if (firstRun) {delay(500);}
  Serial.println(F("Inizializing FS..."));
  if (FileFS.begin()){
      Serial.println(F("FileFS mounted correctly."));
      Serial.println("Total Bytes");
      Serial.println(FileFS.totalBytes());
      Serial.println("Used Bytes");
      Serial.println(FileFS.usedBytes());
      listDir(FileFS, "/", 0);
      listDir(FileFS, "/css/", 0);
      listDir(FileFS, "/js/", 0);
  }else{
      Serial.println(F("!An error occurred during FileFS mounting"));
      //display "Err" if error
      allBlank();
      displayNumber(38,6,CRGB::Purple);  // E
      displayNumber(83,4,CRGB::Purple);  // r
      displayNumber(83,2,CRGB::Purple); // r
      FastLED.show();
      delay(500);
  }
  platformServices().cooperateDuringSetup();

  //display "SEtS"
  allBlank();
  displayNumber(52,6,CRGB::Red);  // S
  displayNumber(38,4,CRGB::Red);  // E
  displayNumber(85,2,CRGB::Red); // t
  displayNumber(52,0,CRGB::Red);  // S
  FastLED.show();
  if (firstRun) {delay(500);}
  Serial.println("list setting folder files");
  listDir(FileFS, "/settings/", 0);
  //load settings from nvram and flash
  Serial.println("load all settings from json file");
  getclockSettings("generic"); //load all settings from json file
  platformServices().cooperateDuringSetup();
  
  #if HAS_RTC
  //display "rtc"
  allBlank();
  displayNumber(83,6,CRGB::Red);  // r
  displayNumber(85,4,CRGB::Red);  // t
  displayNumber(68,2,CRGB::Red); // c
  FastLED.show();
  if (firstRun) {delay(500);}
    //init DS3231 RTC
    if (!platformServices().beginRtc()) {
      Serial.println("Couldn't find DS3231 RTC");
      Serial.flush();
      //display "Err"
      allBlank();
      displayNumber(38,6,CRGB::Red);  // E
      displayNumber(83,4,CRGB::Red);  // r
      displayNumber(83,2,CRGB::Red); // r
      FastLED.show();
      delay(1000);
    //  abort();
    }
  #endif


  #if HAS_SOUNDDETECTOR
    //display "Snd"
    allBlank(); 
    displayNumber(52,5,CRGB::Red); // S
    displayNumber(79,3,CRGB::Red);  // n
    displayNumber(69,1,CRGB::Red);  // d
    FastLED.show();
    if (firstRun) {delay(500);}
    // Initialize peaks to zero
    for (byte band = 0; band < SOUNDDETECTOR_BANDS_WIDTH; band++) {
        peak[band] = 0;
        prevPeak[band] = 0;
    }
    i2sConfig();
    i2sPins();
  #endif

  // init temp & humidity sensor
  #if HAS_DHT
    //display "dht"
    allBlank();
    displayNumber(69,6,CRGB::Red);  // d
    displayNumber(73,4,CRGB::Red);  // h
    displayNumber(85,2,CRGB::Red); // t
    FastLED.show();
    if (firstRun) {delay(500);}
    Serial.println(F("DHTxx test!"));
    platformServices().beginEnvironmentSensor();
  #endif
}

#if SHELFCLOCK_STANDALONE
static void setupStandaloneHost() {
  //display "no AP" while activating the wifi

  allBlank();
  displayNumber(79,6,CRGB::Red);  // n
  displayNumber(80,4,CRGB::Red);  // o
  displayNumber(34,2,CRGB::Red); // A
  displayNumber(49,0,CRGB::Red);  // P
  FastLED.show();
  //display "AP" while activating the websever
  allBlank();
  displayNumber(34,4,CRGB::Red); // A
  displayNumber(49,2,CRGB::Red);  // P
  FastLED.show();
  if (firstRun) {delay(500);}
  Serial.println("Wifi Starting");

  // setup AutoConnect to control WiFi
  if (shelfclock::standalone::beginNetwork(Portal, host, standaloneNetworkStatus)) {
    char ipAddress[16] = {0};
    platformServices().formatIpAddress(ipAddress, sizeof(ipAddress));
    Serial.println("WiFi Connected: " + String(ipAddress));
    allBlank();  //clear "no AP" from screen once wifi is online
   }  else {
    Serial.println("Wifi Failed");
    //display "Err" while activating the wifi
    allBlank();
    displayNumber(38,6,CRGB::Red);  // E
    displayNumber(83,4,CRGB::Red);  // r
    displayNumber(83,2,CRGB::Red); // r
    FastLED.show();
    delay(1000);
    }

  #if !SHELFCLOCK_ISOLATION
  //display "dnS" while activating the mdns
  allBlank();
  displayNumber(69,6,CRGB::Orange);  // d
  displayNumber(79,4,CRGB::Orange);  // n
  displayNumber(52,2,CRGB::Orange); // S
  FastLED.show();
  if (firstRun) {delay(500);}
  //use mdns for host name resolution
  if (!MDNS.begin(host)) { //http://shelfclock
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      allBlank();
      displayNumber(38,6,CRGB::Orange);  // E
      displayNumber(83,4,CRGB::Orange);  // r
      displayNumber(83,2,CRGB::Orange); // r
      FastLED.show();
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  #else
  Serial.println("Isolation build uses its permanent local access point");
  #endif
}
#endif

static void setFallbackTimeToLocalNoon() {
  struct tm fallbackTime = {};
  fallbackTime.tm_hour = 12;
  fallbackTime.tm_min = 0;
  fallbackTime.tm_sec = 0;
  fallbackTime.tm_mday = 18;
  fallbackTime.tm_mon = 4;
  fallbackTime.tm_year = 125;
  fallbackTime.tm_isdst = 0;

  #if SHELFCLOCK_ISOLATION
  const long localOffset = 0;
  #else
  const long localOffset = gmtOffset_sec + (daylightOffset_sec * DSTime);
  #endif
  const time_t fallbackUtc = static_cast<time_t>(1747569600LL - localOffset);
  struct timeval now = { .tv_sec = fallbackUtc };
  settimeofday(&now, NULL);
  fallbackTime.tm_isdst = configuredTimeIsDst();
  timeinfo = fallbackTime;
  fakeTime = 1;
  Serial.println("Using temporary local time 12:00 until a real time source is available");
}

static void setupClockTime() {
  allBlank();
  displayNumber(79,6,CRGB::Blue);  // n
  displayNumber(85,4,CRGB::Blue);  // t
  displayNumber(49,2,CRGB::Blue);  // P
  FastLED.show();
  if (firstRun) {delay(500);}

  bool networkTimeAvailable = false;
  #if !SHELFCLOCK_ISOLATION
    Serial.println("set the time of the internal RTC from NTP server");
    platformServices().configureNetworkTime(gmtOffset_sec, (daylightOffset_sec * DSTime), ntpServer);
    networkTimeAvailable = platformServices().readLocalTime(timeinfo);
    if (!networkTimeAvailable) {
      allBlank();
      displayNumber(38,6,CRGB::Blue);  // E
      displayNumber(83,4,CRGB::Blue);  // r
      displayNumber(83,2,CRGB::Blue); // r
      FastLED.show();
      delay(1000);
    }
  #else
    platformServices().configureNetworkTime(0, 0, nullptr);
    Serial.println("Isolation build skips NTP and uses the DS3231");
  #endif

  fakeTime = networkTimeAvailable ? 0 : 1;
  if (!networkTimeAvailable) {
    setFallbackTimeToLocalNoon();
  }

  #if HAS_RTC
    const bool rtcLostPower = platformServices().rtcLostPower();
    if (!networkTimeAvailable && !rtcLostPower) {
      struct tm rtcTime = {};
      if (platformServices().readRtc(rtcTime)) {
        rtcTime.tm_isdst = configuredTimeIsDst();
        const time_t rtcEpoch = mktime(&rtcTime);
        printf("NTP server not found, setting localtime from DS3231: %s", asctime(&rtcTime));
        struct timeval now = { .tv_sec = rtcEpoch };
        settimeofday(&now, NULL);
        timeinfo = rtcTime;
        fakeTime = 0;
      }
    }

    if (rtcLostPower && networkTimeAvailable) {
      allBlank();
      displayNumber(67,6,CRGB::Red);  // b
      displayNumber(34,4,CRGB::Red);  // A
      displayNumber(85,2,CRGB::Red); // t
      displayNumber(85,0,CRGB::Red);  // t
      FastLED.show();
      if (firstRun) {delay(1000);}
      Serial.println("DS3231's RTC lost power, setting the time via NTP!");
      platformServices().writeRtc(timeinfo);
    }
  #endif

  Serial.println("print time");
  printLocalTime();

  previousTimeMin = timeinfo.tm_min;
  previousTimeHour = timeinfo.tm_hour;
  previousTimeDay = timeinfo.tm_mday;
  previousTimeWeek = ((timeinfo.tm_yday + 7 - (timeinfo.tm_wday ? (timeinfo.tm_wday - 1) : 6)) / 7);
  previousTimeMonth = timeinfo.tm_mon;
}

#if SHELFCLOCK_STANDALONE
static void setupStandaloneOta() {
  // I assume this starts the OTA stuff
  //display "OtA" 
  allBlank();
  displayNumber(48,6,CRGB::Red);  // O
  displayNumber(85,4,CRGB::Red);  // t
  displayNumber(34,2,CRGB::Red); // A
  FastLED.show();
  if (firstRun) {delay(500);}
}
#endif

static void setupClockAfterHost() {
  initGreenMatrix();   //setup lightshow functions
  raininit();          //setup lightshow functions
    
  //display "SErv" while activating the websever
   allBlank();
  displayNumber(52,6,CRGB::Red);  // S
  displayNumber(38,4,CRGB::Red);  // E
  displayNumber(83,2,CRGB::Red); // r
  displayNumber(87,0,CRGB::Red);  // v
  FastLED.show();
   server.enableCrossOrigin(true);
  server.enableCORS(true);

  // Explicit CORS preflight handling
  server.on("/uploadSong", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  });

  auto notFoundHandler = []() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, DELETE");
      server.sendHeader("Access-Control-Allow-Headers", "*");
      server.send(204);
      return;
    }
    server.send(404, "text/plain", "Not found");
  };
  #if SHELFCLOCK_STANDALONE && !SHELFCLOCK_ISOLATION
    Portal.onNotFound(notFoundHandler);
  #else
    server.onNotFound(notFoundHandler);
  #endif

  //Webpage Handlers for FileFS access to flash
  server.serveStatic("/", FileFS, "/index.html");  //send default webpage from root request
  //server.serveStatic("/", FileFS, "/", "max-age=86400");
   server.serveStatic("/", FileFS, "/", "no-cache, max-age=0"); //for debugging
  #if SHELFCLOCK_STANDALONE && SHELFCLOCK_ISOLATION
    server.begin();
    Serial.println("ShelfClock isolation web server started");
  #endif

  //OTA firmware Upgrade Webpage Handlers
  Serial.println("OTA Available");


  #if HAS_BUZZER
    //display "Song" while loading the songs from flash
    allBlank();
    displayNumber(52,6,CRGB::Red);  // S
    displayNumber(80,4,CRGB::Red);  // o
    displayNumber(79,2,CRGB::Red); // n
    displayNumber(72,0,CRGB::Red);  // g
    FastLED.show();
    initRtttlPlayback();
    Serial.println("start of song queue");
    jobQueue = xQueueCreate(30, sizeof(SongQueueMessage));
    if (jobQueue == NULL) {
      Serial.println("song queue creation failed");
    } else {
      queueRtttlBuffer("Intel:d=4,o=5,b=400:32p,d,g,d,2a");
    }
    Serial.println("get list of songs");
    getListOfSongs();
    platformServices().cooperateDuringSetup();
  #endif

  
  //display "Schd" while loading the schedules from flash
  allBlank();
  displayNumber(52,6,CRGB::Red);  // S
  displayNumber(68,4,CRGB::Red);  // c
  displayNumber(73,2,CRGB::Red); // h
  displayNumber(69,0,CRGB::Red);  // d
  FastLED.show();
  if (firstRun) {delay(500);}
  listDir(FileFS, "/scheduler/", 0);
  Serial.println("load array of schedule files on drive ");
  createSchedulesArray();  //load array of schedule files on drive
  platformServices().cooperateDuringSetup();
  Serial.println("print out schedule array");
  processSchedules(0);  //print out schedule array

  Serial.println("start of xTaskCreatePinnedToCore");
  xTaskCreatePinnedToCore(Task1code, "Task1", 10000, NULL, 0, &Task1, 0);

  //display "donE" after setup
  allBlank();
  displayNumber(69,6,CRGB::Red);  // d
  displayNumber(80,4,CRGB::Red);  // o
  displayNumber(79,2,CRGB::Red); // n
  displayNumber(38,0,CRGB::Red);  // E
  FastLED.show();
  for (int i = 0; i < PHOTO_SAMPLES; i++) {
 getsamplePhoto(); 
  }
   GetBrightnessLevel();
  if (firstRun) {delay(500);}
  allBlank();

  //display IP adresss 2x after setup
  if (firstRun) {
  char processedTextIP[16] = {0};
  if (platformServices().networkConnected()) {
      platformServices().formatIpAddress(processedTextIP, sizeof(processedTextIP));
  } else {
      strcpy(processedTextIP, "no AP, no IP");
  }
  scroll(processedTextIP);
  scroll(processedTextIP);
}
  allBlank();
  Serial.println("end of Setup");
  #if HAS_USWEATHER
    if (spotlightsColorSettings == 5 ){ requestWeatherUpdate(WEATHER_UPDATE_RAIN); }
    if ((spotlightsColorSettings == 6) || (scrollOptions9 == 1 && clockMode == 11)){ requestWeatherUpdate(WEATHER_UPDATE_TIDES); }
  #endif
}    //end of Setup()

void shelfclock::runtime::setupStandalone() {
  #if SHELFCLOCK_STANDALONE
  setHostServices(shelfclock::standalone::hostServices);
  setupClockBeforeHost();
  setupStandaloneHost();
  setupClockTime();
  setupStandaloneOta();
  setupClockAfterHost();
  #endif
}

void shelfclock::runtime::setupHosted(HostServices &services) {
  setHostServices(services);
  setupClockBeforeHost();
  setupClockTime();
  setupClockAfterHost();
}

shelfclock::HomeCommandResult shelfclock::runtime::applyHostedHomeCommand(
    const HomeCommand &command) {
  HomeCommandResult result = shelfclock::applyHomeCommand(command);
  if (result.accepted) {
    registerUserDisplayActivity(command.type);
  }
  if (result.saveSettingsNow && saveclockSettings("generic")) {
    updateSettingsRequired = 0;
  }
  return result;
}

void shelfclock::runtime::requestNetworkRecovery() {
  networkRecoveryRequested = true;
  networkRecoveryAt = millis() + 1000;
}

static bool saveNetworkIdentity(const char *networkName, const char *ipAddress) {
  static const char *path = "/settings/network-state.json";
  static const char *tempPath = "/settings/network-state.tmp";
  StaticJsonDocument<256> identity;
  identity["ssid"] = networkName;
  identity["ip"] = ipAddress;

  File file = FileFS.open(tempPath, FILE_WRITE);
  if (!file) {
    Serial.println("Could not create the network identity temp file");
    return false;
  }

  const size_t expectedBytes = measureJson(identity);
  const size_t writtenBytes = serializeJson(identity, file);
  file.flush();
  file.close();
  if (writtenBytes != expectedBytes) {
    Serial.println("Network identity write was incomplete");
    FileFS.remove(tempPath);
    return false;
  }

  if (FileFS.exists(path) && !FileFS.remove(path)) {
    Serial.println("Could not replace the prior network identity");
    FileFS.remove(tempPath);
    return false;
  }
  if (!FileFS.rename(tempPath, path)) {
    Serial.println("Could not activate the new network identity");
    FileFS.remove(tempPath);
    return false;
  }
  return true;
}

static void checkForChangedNetworkIdentity() {
  if (networkIdentityChecked || !platformServices().networkConnected()) {
    return;
  }

  char networkName[33] = {0};
  char ipAddress[16] = {0};
  platformServices().formatNetworkName(networkName, sizeof(networkName));
  platformServices().formatIpAddress(ipAddress, sizeof(ipAddress));
  if (networkName[0] == '\0' || ipAddress[0] == '\0' || strcmp(ipAddress, "0.0.0.0") == 0) {
    return;
  }

  bool identityChanged = true;
  File file = FileFS.open("/settings/network-state.json", "r");
  if (file) {
    StaticJsonDocument<256> identity;
    const DeserializationError error = deserializeJson(identity, file);
    file.close();
    if (!error) {
      const char *savedNetworkName = identity["ssid"] | "";
      const char *savedIpAddress = identity["ip"] | "";
      identityChanged = strcmp(savedNetworkName, networkName) != 0 ||
                        strcmp(savedIpAddress, ipAddress) != 0;
    }
  }

  networkIdentityChecked = true;
  if (!identityChanged) {
    return;
  }

  Serial.printf("Network changed; displaying %s on %s\n", ipAddress, networkName);
  scrollOptions8 = true;
  const shelfclock::HomeCommandResult result = shelfclock::applyHomeCommand(
    shelfclock::HomeCommand(shelfclock::HomeCommandType::Scrolling)
  );
  if (!result.accepted || !saveclockSettings("generic")) {
    Serial.println("Network change mode could not be saved; it will be retried after reboot");
    return;
  }

  updateSettingsRequired = 0;
  if (!saveNetworkIdentity(networkName, ipAddress)) {
    Serial.println("Network identity could not be saved; it will be detected again after reboot");
  }
}

void Task1code(void * parameter) {
  unsigned long lastTime = 0;
  const unsigned long weatherTimerDelay = 30UL * 60UL * 1000UL;  // 30 min
  bool initialWeatherPending = true;
  while(true) {
    bool audioBusy = false;
    #if HAS_BUZZER
      audioBusy = serviceRtttlPlayback();
    #endif
    if (!audioBusy) {
      if (initialWeatherPending || (millis() - lastTime) > weatherTimerDelay) {
        getRemoteWeather();
        lastTime = millis();
        initialWeatherPending = false;
      }
      #if HAS_USWEATHER
        processRequestedWeatherUpdates();
      #endif
    }
    vTaskDelay((TickType_t)1);
  }
}

void getRemoteWeather() {
  #if HAS_USWEATHER
    if (platformServices().networkConnected()) {
      getNoaaWeather();
      outdoorTemp = noaaTemp;
      outdoorHumidity = noaaHumidity;
    } else {
      Serial.println("NOAA weather skipped: Wi-Fi down, using cached values.");
    }
    #endif

  #if HAS_ONLINEWEATHER
    if (platformServices().networkConnected() && apikey[0] != '\0' && latitude[0] != '\0' && longitude[0] != '\0') {
      HTTPClient http;
      String serverPath = String("http://api.openweathermap.org/data/2.5/weather?lat=") + latitude + String("&lon=") + longitude + String("&APPID=") + apikey + String("&units=imperial");
      Serial.println(serverPath);
      http.setConnectTimeout(5000);
      http.setTimeout(8000);
      if (!http.begin(serverPath.c_str())) {
        Serial.println("OpenWeatherMap begin failed, using cached values.");
        return;
      }
      int httpResponseCode = http.GET();
      if (httpResponseCode == HTTP_CODE_OK) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        DynamicJsonDocument payload(2048);
        DeserializationError err = deserializeJson(payload, http.getStream());
        if (!err) {
          JsonObject main = payload["main"].as<JsonObject>();
          if (!main["temp"].isNull()) {
            outdoorTemp = main["temp"].as<float>();
          }
          if (!main["humidity"].isNull()) {
            outdoorHumidity = main["humidity"].as<float>();
          }
        } else {
          Serial.println("OpenWeatherMap parse failed, using cached values.");
        }
      } else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
      }
      // Free resources
      http.end();
      Serial.printf("OpenWeatherMap.org → Temp: %f°F, Humidity: %f%%\n", outdoorTemp, outdoorHumidity);
    } else if (!platformServices().networkConnected()) {
      Serial.println("OpenWeatherMap skipped: Wi-Fi down, using cached values.");
    } else {
      Serial.println("OpenWeatherMap skipped: missing API key/coordinates.");
    }
  #endif
  
}

void shelfclock::runtime::serviceClock() {
  cancelScrollIfInterrupted();
  if (clockMode != observedClockMode || lightshowMode != observedLightshowMode) {
    chaseState.active = false;
    segmentPathState.active = false;
    observedClockMode = clockMode;
    observedLightshowMode = lightshowMode;
  }

  //Change Frequency so as to not use hard-coded delays
  unsigned long currentMillis = millis();  
  //run everything inside here every second
  if (((unsigned long)(currentMillis - prevTime) >= 500) || ((unsigned long)(currentMillis - prevTime) <= 0)) {  //deals with millis() having a rollover period after approximately 49.7 days
    prevTime = currentMillis;
    if(!platformServices().readLocalTime(timeinfo)){ 
      Serial.println("Failed to obtain time");
    }

    //setup time-passage trackers
    int secs = timeinfo.tm_sec;
    int currentTimeMin = timeinfo.tm_min;
    byte m1 = currentTimeMin / 10;
    byte m2 = currentTimeMin % 10;
    int currentTimeHour = timeinfo.tm_hour;
    int currentTimeDay = timeinfo.tm_mday;
    int currentTimeWeek = ((timeinfo.tm_yday + 7 - (timeinfo.tm_wday ? (timeinfo.tm_wday - 1) : 6)) / 7);
    int currentTimeMonth = timeinfo.tm_mon;

    checkSleepTimer();  //time to sleep?

    //if ((currentTimeHour == 23 && currentTimeMin == 11 && timeinfo.tm_sec == 0 && clockDisplayType != 1) || (currentTimeHour == 11 && currentTimeMin == 11 && timeinfo.tm_sec == 0)) { scroll("MAkE A WISH"); }  //at 1111 make a wish
 
    if (abs(currentTimeMin - previousTimeMin) >= 1) { //run every minute
      previousTimeMin = currentTimeMin; 
      randomMinPassed = 1;
      minutesUptime += 1; 
      GetBrightnessLevel(); 
      processSchedules(0);  //see if there's a timer
      if (scrollFrequency == 1 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 1 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every minute

    if ((m2 == 0 || m2 == 5) && (secs == 0)) { //run every 5 minutes
      if (scrollFrequency == 2 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 2 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every 5 minutes

    if ((m2 == 0) && (secs == 0)) { //run every 10 minutes
      if (scrollFrequency == 3 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 3 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every 10 minutes

    if (((m1 == 0 && m2 == 0) || (m1 == 1 && m2 == 5) || (m1 == 3 && m2 == 0) || (m1 == 4 && m2 == 5)) && (secs == 0)) { //run every 15 minutes
      if (scrollFrequency == 4 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 4 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every 15 minutes

    if (((m1 == 0 && m2 == 0) || (m1 == 3 && m2 == 0)) && (secs == 0)) { //run every 30 minutes
      if (scrollFrequency == 5 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 5 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every 30 minutes
      
    if (abs(currentTimeHour - previousTimeHour) >= 1) { //run every hour
      if (updateSettingsRequired == 1 && saveclockSettings("generic")) { updateSettingsRequired = 0; }  //if something was changed in settings, write those changes when the home page is loaded.
      previousTimeHour = currentTimeHour; 
      randomHourPassed = 1;
      hoursUptime += 1;
      #if HAS_USWEATHER
        if (spotlightsColorSettings == 5 ){ requestWeatherUpdate(WEATHER_UPDATE_RAIN); }
        if ((spotlightsColorSettings == 6) || (scrollOptions9 == 1 && clockMode == 11)){ requestWeatherUpdate(WEATHER_UPDATE_TIDES); }
      #endif
      if (scrollFrequency == 6 && (suspendType == 0 || isAsleep == 0) && scrollOverride == 1 && ((clockMode != 11) && (clockMode != 1) && (clockMode != 4))) {displayScrollMode();}
      if (scrollFrequency == 6 && randomSpectrumMode == 1 && clockMode == 9) {allBlank(); spectrumMode = random(11);}
      } //end of run every hour
      
    if (abs(currentTimeDay - previousTimeDay) >= 1) { 
        previousTimeDay = currentTimeDay; 
        randomDayPassed = 1; 
        #if !SHELFCLOCK_ISOLATION
        platformServices().configureNetworkTime(gmtOffset_sec, (daylightOffset_sec * DSTime), ntpServer);
        #endif
        #if HAS_USWEATHER
          if (spotlightsColorSettings == 5 ){ requestWeatherUpdate(WEATHER_UPDATE_RAIN); }
          if ((spotlightsColorSettings == 6) || (scrollOptions9 == 1 && clockMode == 11)){ requestWeatherUpdate(WEATHER_UPDATE_TIDES); }
        #endif
        #if HAS_RTC && !SHELFCLOCK_ISOLATION
          platformServices().writeRtc(timeinfo);
        #endif
        daysUptime = daysUptime + 1;
     }
    if (abs(currentTimeWeek - previousTimeWeek) >= 1) {
      previousTimeWeek = currentTimeWeek;
      randomWeekPassed = 1;
      #if HAS_USWEATHER
        if (spotlightsColorSettings == 5 ){ requestWeatherUpdate(WEATHER_UPDATE_RAIN); }
      #endif
    }
    
    if (abs(currentTimeMonth - previousTimeMonth) >= 1) { previousTimeMonth = currentTimeMonth; randomMonthPassed = 1;}

    if (realtimeMode == 0) {    //give autodim sensors some CPU time, update display
       //GetBrightnessLevel();        
       //FastLED.show();
    }
    // A queued scroll owns the digit LEDs until it completes or is interrupted.
    if (!scrollIsBusy()) {
      if ((suspendType == 0 || isAsleep == 0) && clockMode == 0) {
         displayTimeMode();
      }  else if (clockMode == 1) {
         displayCountdownMode();
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 2) {
         displayTemperatureMode();
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 3) {
         displayScoreboardMode();
      }  else if (clockMode == 4) {
         displayStopwatchMode();
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 5) {
         displayLightshowMode();
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 6) {
         //notused
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 7) {
         displayDateMode();
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 8) {
         displayHumidityMode();
      }  else if (clockMode == 9) {
        //spectrum mode
      }  else if (clockMode == 10) {
        //display off
      }  else if ((suspendType == 0 || isAsleep == 0) && clockMode == 11) {
         displayScrollMode();
      }
    }
    ShelfDownLights(); 
    randomMinPassed = 0; 
    randomHourPassed = 0; 
    randomDayPassed = 0; 
    randomWeekPassed = 0; 
    randomMonthPassed = 0;
    
    if (brightness != 10) {  //if not set to auto-dim just use user set brightness
      FastLED.setBrightness(brightness);
    } else if (brightness == 10) {  //auto-dim use the value from above
        #if HAS_PHOTOSENSOR
          FastLED.setBrightness(lightSensorValue);
        #endif  
    } 
  }

  if (!scrollIsBusy() && !timerEndState.active) {
    displayRealtimeMode();  //always run outside time loop for speed, but only really show when it's needed
  }
  serviceTimerEndEffect();
  serviceScroll();
}

void shelfclock::runtime::serviceStandalone() {
  #if SHELFCLOCK_STANDALONE
  shelfclock::standalone::serviceNetwork(server, Portal, standaloneNetworkStatus);
  checkForChangedNetworkIdentity();
  if (networkRecoveryRequested && static_cast<long>(millis() - networkRecoveryAt) >= 0) {
    networkRecoveryRequested = false;
    platformServices().requestNetworkRecovery();
  }
  if (firmwareRestartRequested && static_cast<long>(millis() - firmwareRestartAt) >= 0) {
    ESP.restart();
  }
  serviceClock();
  #endif
}

void shelfclock::runtime::serviceHosted() {
  if (platformServices().networkConnected()) {
    if (!hostedWebServerStarted) {
      server.begin();
      hostedWebServerStarted = true;
      Serial.println("ShelfClock web server started");
    }
    server.handleClient();
    checkForChangedNetworkIdentity();
  } else if (hostedWebServerStarted) {
    server.stop();
    hostedWebServerStarted = false;
    Serial.println("ShelfClock web server stopped for network recovery");
  }
  if (networkRecoveryRequested && static_cast<long>(millis() - networkRecoveryAt) >= 0) {
    networkRecoveryRequested = false;
    platformServices().requestNetworkRecovery();
  }
  if (firmwareRestartRequested && static_cast<long>(millis() - firmwareRestartAt) >= 0) {
    ESP.restart();
  }
  if (!firmwareUploadInProgress) {
    serviceClock();
  } else {
    platformServices().cooperateDuringSetup();
  }
}

void getsamplePhoto(){
#if HAS_PHOTOSENSOR
// one-time seed
if (!brightnessInited) {
  uint16_t seed = analogRead(PHOTORESISTER_PIN);
  brightnessSum = (long)seed * PHOTO_SAMPLES;
  for (int i = 0; i < PHOTO_SAMPLES; i++)
    photoresisterReadings[i] = seed;
  readIndex       = 0;
  brightnessInited = true;
}
// rolling-sum update: drop old, read new, add new
uint16_t oldVal = photoresisterReadings[readIndex];
uint16_t newVal = analogRead(PHOTORESISTER_PIN);
brightnessSum  += (long)newVal - oldVal;
photoresisterReadings[readIndex] = newVal;
readIndex = (readIndex + 1) % PHOTO_SAMPLES;
#endif 
}

void displayTimeMode() {  //main clock function
  currentMode = 0;
	if(!platformServices().readLocalTime(timeinfo)){ 
	  Serial.println("Failed to obtain time");
	}
 // printLocalTime();  //display time in monitor window
	int hour = timeinfo.tm_hour;
	int mins = timeinfo.tm_min;
	int secs = timeinfo.tm_sec;

  if (clockDisplayType != 1 && hour > 12){hour = hour - 12; }
  if (clockDisplayType != 1 && hour < 1){hour = hour + 12; }
	byte h1 = hour / 10;
	byte h2 = hour % 10;
	byte m1 = mins / 10;
	byte m2 = mins % 10;  
	byte s1 = secs / 10;
	byte s2 = secs % 10;
  
  if (ClockColorSettings == 0) { hourColor = CRGB(hourColorValue);  minColor = CRGB(minColorValue); }
  if (ClockColorSettings == 1) { hourColor = CRGB(hourColorValue);  minColor = hourColor; }
  if ((ClockColorSettings == 2 && pastelColors == 0)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { hourColor = CHSV(random(0, 255), 255, 255);  minColor = CHSV(random(0, 255), 255, 255);}
  if ((ClockColorSettings == 2 && pastelColors == 1)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { hourColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  minColor = CRGB(random(0, 255), random(0, 255), random(0, 255));}
  if ((ClockColorSettings == 3 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { hourColor = CHSV(random(0, 255), 255, 255);  minColor = hourColor;}
  if ((ClockColorSettings == 3 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { hourColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  minColor = hourColor;}
  if ((clockDisplayType == 3)) {          //Clock for Blinking Center Light
  	if (h1 > 0) {   // draw a special hour digit to maximize space
      tinyhourColor = hourColor;
      if (ClockColorSettings == 4 && pastelColors == 0){ tinyhourColor = CHSV(random(0, 255), 255, 255); }
      if (ClockColorSettings == 4 && pastelColors == 1){ tinyhourColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhourColor;}
      if (ClockColorSettings == 4 && pastelColors == 0){ tinyhourColor = CHSV(random(0, 255), 255, 255); }
      if (ClockColorSettings == 4 && pastelColors == 1){ tinyhourColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhourColor;}
  	} else {
  	    for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black;}
  	  }
      displayNumber(h2,5,hourColor);
      displayNumber(m1,2,minColor);
      displayNumber(m2,0,minColor); 
      BlinkDots();    
  }
  
  if (clockDisplayType == 2) {  //12-hour Space-Padded
    if (h1 < 1) { displayNumber(10,6,hourColor); }
    else { displayNumber(h1,6,hourColor); }
      displayNumber(h2,4,hourColor);
      displayNumber(m1,2,minColor);
      displayNumber(m2,0,minColor); 
	  BlinkDots();
  }
  if (clockDisplayType == 0) {  //center set and hour is less than 1 and no 0 is set, default
  if (h1 > 0) {
    tinyhourColor = hourColor;
    if (ClockColorSettings == 4 && pastelColors == 0){ tinyhourColor = CHSV(random(0, 255), 255, 255); }
    if (ClockColorSettings == 4 && pastelColors == 1){ tinyhourColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhourColor;}
    if (ClockColorSettings == 4 && pastelColors == 0){ tinyhourColor = CHSV(random(0, 255), 255, 255); }
    if (ClockColorSettings == 4 && pastelColors == 1){ tinyhourColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhourColor;}
  } else {for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black;}
   }
    	displayNumber(h2,5,hourColor);
    	displayNumber(m1,3,minColor);
    	displayNumber(m2,1,minColor); 
  }
  if (clockDisplayType == 1) {     //24-hour Military Time
    if (h1 < 1) { displayNumber(0,6,hourColor);}
    else  { displayNumber(h1,6,hourColor);}
    displayNumber(h2,4,hourColor);
    displayNumber(m1,2,minColor);
    displayNumber(m2,0,minColor); 
	  BlinkDots();
  }

  if (clockDisplayType == 4) {     //New Year Countdown
    int hour = timeinfo.tm_hour;
    int mins = timeinfo.tm_min;
    int secs = timeinfo.tm_sec;
    int mday = timeinfo.tm_mday;
    int mont = timeinfo.tm_mon + 1;
    int year = (timeinfo.tm_year +1900);
    int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int daysLeft = 0;
    if (((year % 4 == 0) && (year % 100 != 0)) || ((year % 100 != 0) && (year % 400 == 0))){daysLeft = daysLeft + 1;}  //leapyear?
    if (mont !=12) {for (int i=(mont+1); i<13; i++) { daysLeft = daysLeft + daysInMonth[i-1];}}
    daysLeft = daysLeft + (daysInMonth[mont-1]-mday);
    int hoursLeft = (daysLeft * 24) + (23 - hour);
    bool DST;  //start of DST adjustment algorithm
    int y = year-2000; // uses two digit year 
    int x = (y + y/4 + 2) % 7; // remainder will identify which day of month
    if(mont == 3 && mday == (14 - x) && hour >= 2){DST = 1;} //DST begins on 2nd Sunday of March @ 2:00 AM
    if((mont == 3 && mday > (14 - x)) || mont > 3){DST = 1;}
    if(mont == 11 && mday == (7 - x) && hour >= 2){DST = 0; }  //DST ends on 1st Sunday of Nov @ 2:00 AM
    if((mont == 11 && mday > (7 - x)) || mont > 11 || mont < 3){DST = 0;}
    if(DST == 1) {hoursLeft = hoursLeft + 1; }  //adjust for DST
    int minutesLeft = (hoursLeft * 60) + (59 - mins);
    int secondsLeft = (minutesLeft * 60) + (60 - secs);
    int inputNums = hoursLeft;
    if ((minutesLeft <= 9999) && (secondsLeft > 9999)) {inputNums = minutesLeft; hourColor = minColor;}
    if ((secondsLeft <= 9999) && (secondsLeft > 10)) {inputNums = secondsLeft; hourColor = colonColor;}
    if (secondsLeft <= 10) {inputNums = secondsLeft;FastLED.setBrightness(255);hourColor = CRGB::Red;}
    byte ledNum1 = inputNums / 1000;
    byte ledNum2 = (inputNums - (ledNum1 * 1000)) / 100;
    byte ledNum3 = ((inputNums - (ledNum1 * 1000)) - (ledNum2 * 100)) / 10;
    byte ledNum4 = inputNums % 10;
    
    if (inputNums >= 1000) {
      if (clearOldLeds != 1000){clearOldLeds = 1000; allBlank();}
      displayNumber(ledNum1,6,hourColor);
      displayNumber(ledNum2,4,hourColor);
      displayNumber(ledNum3,2,hourColor);
      displayNumber(ledNum4,0,hourColor); 
    }
    if ((inputNums >= 100) && (inputNums < 1000)) {
      if (clearOldLeds != 100){clearOldLeds = 100; allBlank();}
      displayNumber(ledNum2,5,hourColor);
      displayNumber(ledNum3,3,hourColor);
      displayNumber(ledNum4,1,hourColor); 
    }
    if ((inputNums > 0) && (inputNums < 100)) {
      if (clearOldLeds != 10){clearOldLeds = 10; allBlank();}
      displayNumber(ledNum3,4,hourColor);
      displayNumber(ledNum4,2,hourColor); 
    }
  //  if (mday == 1 && mont == 1 && hour == 0 && mins == 0 && secs <= 3) { happyNewYear();}
  //  if (mday == 7 && mont == 6 && hour == 16 && mins == 10 && secs <= 3) { happyNewYear();}  //for testing
    }
} // end of update clock


void displayDateMode() {  //main date function
  currentMode = 0;
  if(!platformServices().readLocalTime(timeinfo)){ 
    Serial.println("Failed to obtain time");
  }
  int mday = timeinfo.tm_mday;
  int mont = timeinfo.tm_mon + 1;
  int year = (timeinfo.tm_year +1900)-2000;
  byte d1 = mday / 10;
  byte d2 = mday % 10;
  byte m1 = mont / 10;
  byte m2 = mont % 10;  
  byte y1 = year / 10;
  byte y2 = year % 10;
  if (DateColorSettings == 0) { monthColor = CRGB(monthColorValue);  dayColor = CRGB(dayColorValue); separatorColor = CRGB(separatorColorValue);}
  if (DateColorSettings == 1) { monthColor = CRGB(monthColorValue);  dayColor = monthColor; separatorColor = CRGB(separatorColorValue);}
  if ((DateColorSettings == 2 && pastelColors == 0)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { monthColor = CHSV(random(0, 255), 255, 255);  dayColor = CHSV(random(0, 255), 255, 255);separatorColor = CHSV(random(0, 255), 255, 255);}
  if ((DateColorSettings == 2 && pastelColors == 1)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { monthColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  dayColor = CRGB(random(0, 255), random(0, 255), random(0, 255));separatorColor = CRGB(random(0, 255), random(0, 255), random(0, 255));}
  if ((DateColorSettings == 3 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { monthColor = CHSV(random(0, 255), 255, 255);  dayColor = monthColor; separatorColor = monthColor;}
  if ((DateColorSettings == 3 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { monthColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  dayColor = monthColor; separatorColor = monthColor;}

 if ((dateDisplayType == 5)) {    //With "." Separator (MM.DD)
  if (m1 > 0) {
    tinymonthColor = monthColor;
    if (DateColorSettings == 4 && pastelColors == 0){ tinymonthColor = CHSV(random(0, 255), 255, 255); }
    if (DateColorSettings == 4 && pastelColors == 1){ tinymonthColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinymonthColor;}
    if (DateColorSettings == 4 && pastelColors == 0){ tinymonthColor = CHSV(random(0, 255), 255, 255); }
    if (DateColorSettings == 4 && pastelColors == 1){ tinymonthColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinymonthColor;}
  } else {for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black;}
   }
    displayNumber(m2,5,monthColor);
    displayNumber(d1,2,dayColor);
    displayNumber(d2,0,dayColor); 
    if ((DateColorSettings == 4 ) && pastelColors == 0){ separatorColor = CHSV(random(0, 255), 255, 255); }
    if ((DateColorSettings == 4 ) && pastelColors == 1){ separatorColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(20*LEDS_PER_SEGMENT); i<(21*LEDS_PER_SEGMENT); i++) { LEDs[i] = separatorColor;}  //separator
  }
  
  if (dateDisplayType == 1) {    //Space-Padded (MMDD)
    if (m1 < 1) { displayNumber(10,6,monthColor); }
    else { displayNumber(m1,6,monthColor); }
      displayNumber(m2,4,monthColor);
      displayNumber(d1,2,dayColor);
      displayNumber(d2,0,dayColor); 
  }
  
  if (dateDisplayType == 2) {    //Center Dates (1MDD)
  if (m1 > 0) {
    tinymonthColor = monthColor;
    if (DateColorSettings == 4 && pastelColors == 0){ tinymonthColor = CHSV(random(0, 255), 255, 255); }
    if (DateColorSettings == 4 && pastelColors == 1){ tinymonthColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinymonthColor;}
    if (DateColorSettings == 4 && pastelColors == 0){ tinymonthColor = CHSV(random(0, 255), 255, 255); }
    if (DateColorSettings == 4 && pastelColors == 1){ tinymonthColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinymonthColor;}
  } else {for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black;}
   }
      displayNumber(m2,5,monthColor);
      displayNumber(d1,3,dayColor);
      displayNumber(d2,1,dayColor); 
  }
  
  if (dateDisplayType == 0) {    //Zero-Padded (MMDD)
    if (m1 < 1) { displayNumber(0,6,monthColor);}
    else  { displayNumber(m1,6,monthColor);}
     
      displayNumber(m2,4,monthColor);
      displayNumber(d1,2,dayColor);
      displayNumber(d2,0,dayColor); 
  }
  
  if (dateDisplayType == 4) {    //Just Numeric Day (DD)
  if (d1 < 1) {
      displayNumber(d2,3,dayColor);
  } else {
      displayNumber(d1,4,dayColor);
      displayNumber(d2,2,dayColor); 
  }
  }
  
  if (dateDisplayType == 3) {    //Just Day of Week (Sun)
  if (timeinfo.tm_wday == 1)    {displayNumber(78,5,dayColor);displayNumber(80,3,dayColor); displayNumber(79,1,dayColor);}  //mon
  if (timeinfo.tm_wday == 2)  {displayNumber(85,6,dayColor);displayNumber(54,4,dayColor); displayNumber(38,2,dayColor); displayNumber(52,0,dayColor);} //tUES
  if (timeinfo.tm_wday == 3) {displayNumber(88,5,dayColor);displayNumber(38,3,dayColor); displayNumber(69,1,dayColor);}  //wEd
  if (timeinfo.tm_wday == 4)  {displayNumber(85,6,dayColor);displayNumber(73,4,dayColor); displayNumber(86,2,dayColor); displayNumber(83,0,dayColor);}  //thur
  if (timeinfo.tm_wday == 5)    {displayNumber(39,5,dayColor);displayNumber(83,3,dayColor); displayNumber(42,1,dayColor);}  //FrI
  if (timeinfo.tm_wday == 6)  {displayNumber(52,5,dayColor);displayNumber(34,3,dayColor); displayNumber(85,1,dayColor);}  //SAt
  if (timeinfo.tm_wday == 0)    {displayNumber(52,5,dayColor);displayNumber(86,3,dayColor); displayNumber(79,1,dayColor);} //Sun
  }
  
  if (dateDisplayType == 6) {    //Just Year (YYYY)
  displayNumber(2,6,monthColor);
  displayNumber(0,4,monthColor);
  displayNumber(y1,2,dayColor);
  displayNumber(y2,0,dayColor); 
  } 
} // end of update date

void displayTemperatureMode() {   //miain temp function
  static int countFlip = 1;
  currentMode = 0;
  #if HAS_DHT
    const shelfclock::runtime::EnvironmentReading environment = platformServices().readEnvironment();
    float humidTemp = environment.humidity;
    float sensorTemp = environment.temperatureC;
    float f = environment.temperatureF;
    // float saturationVaporPressure = 6.1078 * pow(10, (7.5 * sensorTemp / (237.3 + sensorTemp)));  // Calculate saturation vapor pressure
    // float vaporPressure = saturationVaporPressure * (humidTemp / 100.0);  // Calculate vapor pressure
    // float absoluteHumidity = 217 * vaporPressure / (273.15 + sensorTemp);  // Calculate absolute humidity
    // float humidityRatio = 0.622 * vaporPressure / (101325 - vaporPressure);
    float heatIndex = -8.784695 + 1.61139411 * sensorTemp + 2.338549 * humidTemp + -0.14611605 * sensorTemp * humidTemp + -0.01230809 * pow(sensorTemp, 2) + -0.01642482 * pow(humidTemp, 2) + 0.00221173 * pow(sensorTemp, 2) * humidTemp + 0.00072546 * sensorTemp * pow(humidTemp, 2) + -0.00000358 * pow(sensorTemp, 2) * pow(humidTemp, 2);
    heatIndex += altitudeLocal * 0.0065;  // Adjust heat index based on altitude
    sensorTemp = heatIndex;
    if (isnan(humidTemp) || isnan(sensorTemp) || isnan(f)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
    float correctedTemp = sensorTemp + temperatureCorrection;
    if (temperatureSymbol == 39) {  correctedTemp = ((sensorTemp * 1.8000) + 32) + temperatureCorrection; }
  #else
    float h = 00.00;        // fake humidity
    float sensorTemp = 0;     // fake temperature
    float correctedTemp = sensorTemp;
  #endif
  #if HAS_USWEATHER || HAS_ONLINEWEATHER
    if (temperature_outdoor_enable == true) {   //if outdoor temp show both outdoor and indoor
      if (countFlip > 10) {
        if (temperatureSymbol != 39) {  
          correctedTemp = ((outdoorTemp - 32) / 1.8); 
        } else {
          correctedTemp = outdoorTemp;
        }
      }
      if (countFlip > 20) {
        #if !HAS_DHT   //if outdoor temp and no DHT
          if (temperatureSymbol != 39) {  
            correctedTemp = ((outdoorTemp - 32) / 1.8); 
          } else {
            correctedTemp = outdoorTemp;
          }
        #endif
        countFlip = 0;
      }
      countFlip++;
    }
    #endif
  byte t1 = 0;
  byte t2 = 0;
  int tempDecimal = correctedTemp * 10;
  if (correctedTemp >= 100) {
    int tempHundred = correctedTemp / 10;
    t1 = tempHundred / 10;
    t2 = tempHundred % 10;
    } else {
    t2 = int(correctedTemp) / 10;
    }
  byte t3 = int(correctedTemp) % 10;
  byte t4 = tempDecimal % 10;
  if (tempColorSettings == 0) {tempColor = CRGB(tempColorValue);  typeColor = CRGB(typeColorValue); degreeColor = CRGB(degreeColorValue);}
  if (tempColorSettings == 1) {tempColor = CRGB(tempColorValue);  typeColor = tempColor; degreeColor = CRGB(degreeColorValue);}
  if ((tempColorSettings == 2 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { tempColor = CHSV(random(0, 255), 255, 255);  typeColor = CHSV(random(0, 255), 255, 255);degreeColor = CHSV(random(0, 255), 255, 255);}
  if ((tempColorSettings == 2 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { tempColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  typeColor = CRGB(random(0, 255), random(0, 255), random(0, 255));degreeColor = CRGB(random(0, 255), random(0, 255), random(0, 255));}
  if ((tempColorSettings == 3 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { tempColor = CHSV(random(0, 255), 255, 255);  typeColor = tempColor; degreeColor = tempColor;}
  if ((tempColorSettings == 3 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { tempColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  typeColor = tempColor; degreeColor = tempColor;}
    
//0-Temperature with Degree and Type (79°F), 1-Temperature with just Type (79 F), 2-Temperature with just Degree (79°), 3-Temperature with Decimal (79.9), 4-Just Temperature (79)
  if ((tempDisplayType == 0) && (correctedTemp < 100)) {  //0-Temperature with Degree and Type (79°F) under 100 only
    displayNumber(t2,6,tempColor);
    displayNumber(t3,4,tempColor);
    displayNumber(26,2,degreeColor);
    displayNumber(temperatureSymbol,0,typeColor);
  }
  if ((tempDisplayType == 1) && (correctedTemp < 100)) {   // 1-Temperature with just Type (79 F) under 100
    displayNumber(t2,5,tempColor);
    displayNumber(t3,3,tempColor);
    displayNumber(temperatureSymbol,1,typeColor);
  }
  if (((tempDisplayType == 1) || (tempDisplayType == 0)) && (correctedTemp >= 100)) {   // 1-Temperature with just Type (79 F) over 100
    displayNumber(t1,6,tempColor);
    displayNumber(t2,4,tempColor);
    displayNumber(t3,2,tempColor);
    displayNumber(temperatureSymbol,0,typeColor);
  }
  if ((tempDisplayType == 2) && (correctedTemp < 100)) {  //2-Temperature with just Degree (79°) under 100
    displayNumber(t2,5,tempColor);
    displayNumber(t3,3,tempColor);
    displayNumber(26,1,degreeColor);
  }
  if ((tempDisplayType == 2) && (correctedTemp >= 100)) {  //2-Temperature with just Degree (79°) over 100
    displayNumber(t1,6,tempColor);
    displayNumber(t2,4,tempColor);
    displayNumber(t3,2,tempColor);
    displayNumber(26,0,degreeColor);
  }
  if ((tempDisplayType == 3) && (correctedTemp < 100)) {   //3-Temperature with Decimal (79.9) under 100
    displayNumber(t2,6,tempColor);
    displayNumber(t3,4,tempColor);
    displayNumber(t4,1,typeColor); 
    if (tempColorSettings == 4 && pastelColors == 0){ degreeColor = CHSV(random(0, 255), 255, 255); }
    if (tempColorSettings == 4 && pastelColors == 1){ degreeColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(12*LEDS_PER_SEGMENT); i<(13*LEDS_PER_SEGMENT); i++) { LEDs[i] = degreeColor;}  //period goes here
  }
  if ((tempDisplayType == 3) && (correctedTemp >= 100)) {   //3-Temperature with Decimal (79.9) over 100
    displayNumber(t2,5,tempColor);
    displayNumber(t3,3,tempColor);
    displayNumber(t4,0,typeColor);
    tinytempColor = tempColor;
    if (tempColorSettings == 4 && pastelColors == 0){ tinytempColor = CHSV(random(0, 255), 255, 255); }
    if (tempColorSettings == 4 && pastelColors == 1){ tinytempColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinytempColor;}  //1xx split across 2 for color reasons
    if (tempColorSettings == 4 && pastelColors == 0){ tinytempColor = CHSV(random(0, 255), 255, 255); }
    if (tempColorSettings == 4 && pastelColors == 1){ tinytempColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinytempColor;}  //1xx split across 2 for color reasons
    if (tempColorSettings == 4 && pastelColors == 0){ degreeColor = CHSV(random(0, 255), 255, 255); }
    if (tempColorSettings == 4 && pastelColors == 1){ degreeColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(10*LEDS_PER_SEGMENT); i<(11*LEDS_PER_SEGMENT); i++) { LEDs[i] = degreeColor;}  //period goes here
  }
  if ((tempDisplayType == 4) && (correctedTemp < 100)) {  //4-Just Temperature (79) under 100
    displayNumber(t2,4,tempColor);
    displayNumber(t3,2,typeColor);
  }
  if ((tempDisplayType == 4) && (correctedTemp >= 100)) {  //4-Just Temperature (79) over 100
    displayNumber(t1,5,degreeColor);
    displayNumber(t2,3,tempColor);
    displayNumber(t3,1,typeColor);
  } 
}//end of temp settings


void displayHumidityMode() {   //main humidity function
  static int countFlip = 1;
  currentMode = 0;
  #if HAS_DHT
    const shelfclock::runtime::EnvironmentReading environment = platformServices().readEnvironment();
    float sensorHumi = environment.humidity;
    float t = environment.temperatureC;
    float f = environment.temperatureF;

    if (isnan(sensorHumi) || isnan(t) || isnan(f)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
  #else
    float sensorHumi = 00.00;        // fake humidity
    float t = 00.00;     // fake temperature
  #endif
  #if HAS_USWEATHER || HAS_ONLINEWEATHER
    if (humidity_outdoor_enable == true) {  //if outdoor humid also show dht
      if (countFlip > 10) {
        sensorHumi = outdoorHumidity;
      }
      if (countFlip > 20) {  //no dht just show dht
        #if !HAS_DHT
          sensorHumi = outdoorHumidity;
        #endif
        countFlip = 0;
      }
      countFlip++;
    }
    #endif
  byte t1 = 0;
  byte t2 = 0;
  int humiDecimal = sensorHumi * 10;
  if (sensorHumi >= 100) {
    int humiHundred = sensorHumi / 10;
    t1 = humiHundred / 10;
    t2 = humiHundred % 10;
    } else {
    t2 = int(sensorHumi) / 10;
    }
  byte t3 = int(sensorHumi) % 10;
  byte t4 = humiDecimal % 10;
  if (humiColorSettings == 0) {humiColor = CRGB(humiColorValue);  humiSymbolColor = CRGB(humiSymbolColorValue); humiDecimalColor = CRGB(humiDecimalColorValue);}
  if (humiColorSettings == 1) {humiColor = CRGB(humiColorValue);  humiSymbolColor = humiColor; humiDecimalColor = CRGB(humiDecimalColorValue);}
  if ((humiColorSettings == 2 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { humiColor = CHSV(random(0, 255), 255, 255);  humiSymbolColor = CHSV(random(0, 255), 255, 255);humiDecimalColor = CHSV(random(0, 255), 255, 255); }
  if ((humiColorSettings == 2 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { humiColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  humiSymbolColor = CRGB(random(0, 255), random(0, 255), random(0, 255));humiDecimalColor = CRGB(random(0, 255), random(0, 255), random(0, 255));}
  if ((humiColorSettings == 3 && pastelColors == 0) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { humiColor = CHSV(random(0, 255), 255, 255);  humiSymbolColor = humiColor; humiDecimalColor = humiColor;}
  if ((humiColorSettings == 3 && pastelColors == 1) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { humiColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  humiSymbolColor = humiColor; humiDecimalColor = humiColor;}
  if ((humiDisplayType == 0) && (sensorHumi < 100)) {   //0-Humidity with Symbol (34 H) under 100
    displayNumber(t2,5,humiColor);
    displayNumber(t3,3,humiColor);
    displayNumber(41,1,humiSymbolColor); //H(umitidy
  }
  if ((humiDisplayType == 0) && (sensorHumi >= 100)) {   //0-Humidity with Symbol (34 H) over 100
    displayNumber(t1,6,humiColor);
    displayNumber(t2,4,humiColor);
    displayNumber(t3,2,humiColor);
    displayNumber(41,0,humiSymbolColor); //H(umitidy
  }
  if ((humiDisplayType == 1) && (sensorHumi < 100)) {   //1-Humidity with Decimal (34.9) under 100
    displayNumber(t2,6,humiColor);
    displayNumber(t3,4,humiColor);
    displayNumber(t4,1,humiSymbolColor);
    if (humiColorSettings == 4 && pastelColors == 0){ humiDecimalColor = CHSV(random(0, 255), 255, 255); }
    if (humiColorSettings == 4 && pastelColors == 1){ humiDecimalColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(12*LEDS_PER_SEGMENT); i<(13*LEDS_PER_SEGMENT); i++) { LEDs[i] = humiDecimalColor;}  //period goes here
  }
  if ((humiDisplayType == 1) && (sensorHumi >= 100)) {   //1-Humidity with Decimal (34.9) over 100
    displayNumber(t2,5,humiColor);
    displayNumber(t3,3,humiColor);
    displayNumber(t4,0,humiSymbolColor);
    tinyhumiColor = humiColor;
    if (humiColorSettings == 4 && pastelColors == 0){ tinyhumiColor = CHSV(random(0, 255), 255, 255); }
    if (humiColorSettings == 4 && pastelColors == 1){ tinyhumiColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(32*LEDS_PER_SEGMENT); i<(33*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhumiColor;}  //1xx split across 2 for color reasons
    if (humiColorSettings == 4 && pastelColors == 0){ tinyhumiColor = CHSV(random(0, 255), 255, 255); }
    if (humiColorSettings == 4 && pastelColors == 1){ tinyhumiColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(33*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = tinyhumiColor;} //1xx
    if (humiColorSettings == 4 && pastelColors == 0){ humiDecimalColor = CHSV(random(0, 255), 255, 255); }
    if (humiColorSettings == 4 && pastelColors == 1){ humiDecimalColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
    for (int i=(10*LEDS_PER_SEGMENT); i<(11*LEDS_PER_SEGMENT); i++) { LEDs[i] = humiDecimalColor;}   //period goes here
  }
  if ((humiDisplayType == 2) && (sensorHumi < 100)) {  //2-Just Humidity (79) under 100
    displayNumber(t2,4,humiColor);
    displayNumber(t3,2,humiSymbolColor);
  }
  if ((humiDisplayType == 2) && (sensorHumi >= 100)) {  //2-Just Humidity (79) over 100
    displayNumber(t1,5,humiDecimalColor);
    displayNumber(t2,3,humiColor);
    displayNumber(t3,1,humiSymbolColor);
  }
}//end of update humidity


void displayScrollMode(){   //scrollmode for displaying clock things not just text
  if (scrollIsBusy()) {
    return;
  }
  currentMode = 0;
  if (realtimeMode == 0) {
    if (!platformServices().readLocalTime(timeinfo)){ Serial.println("Failed to obtain time");  }
    char strTime[12];
    char strDate[12];
    char strYear[12];
    char strTemp[32];
    char strHumitidy[32];
    char strIPaddy[24];
    char DOW[12]; 
    int hour = timeinfo.tm_hour;
    int mins = timeinfo.tm_min;
    int mday = timeinfo.tm_mday;
    int mont = timeinfo.tm_mon + 1;
    int year = timeinfo.tm_year +1900;
    #if HAS_DHT
      const shelfclock::runtime::EnvironmentReading environment = platformServices().readEnvironment();
      float h = environment.humidity;
      float sensorTemp = environment.temperatureC;
      float f = environment.temperatureF;
      if (isnan(h) || isnan(sensorTemp) || isnan(f)) {  Serial.println(F("Failed to read from DHT sensor!"));  return;  }
    #else
      float h = 00.00;        // fake humidity
      float sensorTemp = 00.00;     // fake temperature
    #endif
    float correctedTemp = sensorTemp + temperatureCorrection;

    if (temperatureSymbol == 39) {  correctedTemp = ((sensorTemp * 1.8000) + 32) + temperatureCorrection; }
    if (timeinfo.tm_wday == 1)    {snprintf(DOW, sizeof(DOW), "%s", "Mon    ");}
    if (timeinfo.tm_wday == 2)    {snprintf(DOW, sizeof(DOW), "%s", "tUES    ");}
    if (timeinfo.tm_wday == 3)    {snprintf(DOW, sizeof(DOW), "%s", "WEd    ");}
    if (timeinfo.tm_wday == 4)    {snprintf(DOW, sizeof(DOW), "%s", "thur    ");}
    if (timeinfo.tm_wday == 5)    {snprintf(DOW, sizeof(DOW), "%s", "FrI    ");}
    if (timeinfo.tm_wday == 6)    {snprintf(DOW, sizeof(DOW), "%s", "SAt    ");}
    if (timeinfo.tm_wday == 0)    {snprintf(DOW, sizeof(DOW), "%s", "Sun    ");}
    snprintf(strTime, sizeof(strTime), "%.2d%.2d    ", hour, mins);  //1111
    snprintf(strDate, sizeof(strDate), "%.2d-%.2d    ", mont, mday);  //10-22
    snprintf(strYear, sizeof(strYear), "%d    ", year);  //2021
    #if HAS_USWEATHER || HAS_ONLINEWEATHER
      if (temperature_outdoor_enable == true && outdoorTemp != -500) {
        snprintf(strTemp, sizeof(strTemp), "%.1f : %.1f^F     ", correctedTemp, outdoorTemp);
      } else {
        snprintf(strTemp, sizeof(strTemp), "%.1f^F    ", correctedTemp );  //98_6 ^F
      }
      if (humidity_outdoor_enable == true && outdoorHumidity != -1) {
        snprintf(strHumitidy, sizeof(strHumitidy), "%.0f : %.0fH    ", h, outdoorHumidity);  //48_6 H
      } else {
        snprintf(strHumitidy, sizeof(strHumitidy), "%.0fH    ", h);  //48_6 H
      }
      #else
      snprintf(strTemp, sizeof(strTemp), "%.1f^F    ", correctedTemp );  //98_6 ^F
      snprintf(strHumitidy, sizeof(strHumitidy), "%.0fH    ", h);  //48_6 H
      #endif
    platformServices().formatIpAddress(strIPaddy, sizeof(strIPaddy));  //192_168_0_10
    String processedText;
    processedText.reserve(512);      // pre-allocates to avoid reallocs

    if (scrollOptions1) processedText += String(strTime)        + "  ";
    if (scrollOptions2) processedText += String(DOW)            + "  ";
    if (scrollOptions3) processedText += String(strDate)        + "  ";
    if (scrollOptions4) processedText += String(strYear)        + "  ";
    if (scrollOptions5) processedText += String(strTemp)        + "  ";
    if (scrollOptions6) processedText += String(strHumitidy)    + "  ";
    #if HAS_USWEATHER
      if (scrollOptions9) processedText += String(textTides)      + "  ";
      #endif
    if (scrollOptions7) processedText += scrollText             + "    ";
    if (scrollOptions8) processedText += String(strIPaddy)      + "  ";
    // if none selected, fall back to user scrollText
    if (!(scrollOptions1 || scrollOptions2 || scrollOptions3 || scrollOptions4 || scrollOptions5 || scrollOptions6 || scrollOptions7 || scrollOptions8 || scrollOptions9)) { processedText += scrollText;}
    scroll(processedText);
    if (breakOutSet || clockMode != 11) { return; }
    scroll(processedText);  
    }
}

static bool timerDeadlineReached(unsigned long now, unsigned long deadline) {
  return (long)(now - deadline) >= 0;
}

static bool activeTimerDeadlineReached() {
  if ((clockMode != 1 && clockMode != 4) || endCountDownMillis == 0) {
    return false;
  }

  return timerDeadlineReached(millis(), endCountDownMillis);
}

static bool scrollShouldAbort(int startingClockMode, int startingRealtimeMode) {
  return clockMode != startingClockMode || realtimeMode != startingRealtimeMode || activeTimerDeadlineReached();
}

void displayCountdownMode() {     //main countdown function
  breakOutSet = 0;
  if (timerEndState.active) {
    return;
  }
  if (countdownMilliSeconds == 0 && endCountDownMillis == 0) 
    return;
  unsigned long now = millis();
  if (timerDeadlineReached(now, endCountDownMillis)) { //timer ended
    endCountdown();
    countdownMilliSeconds = 0;
    endCountDownMillis = 0;
    return;
  }
  unsigned long restMillis = endCountDownMillis - now;
  unsigned long hours   = ((restMillis / 1000) / 60) / 60;
  unsigned long minutes = (restMillis / 1000) / 60;
  unsigned long seconds = restMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60); 
  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;  
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;
  CRGB countdownColor = CRGB(countdownColorValue); 
  if (restMillis <= 10000 && colorchangeCD == 1) {  //red mode last 10 seconds
    countdownColor = CRGB::Red;
  }
  if (hours > 0) {   // hh:mm
    displayNumber(h1,6,countdownColor); 
    displayNumber(h2,4,countdownColor);
    displayNumber(m1,2,countdownColor);
    displayNumber(m2,0,countdownColor);  
  } else {   // mm:ss   
    displayNumber(m1,6,countdownColor);
    displayNumber(m2,4,countdownColor);
    displayNumber(s1,2,countdownColor);
    displayNumber(s2,0,countdownColor);  
  }
}

void displayStopwatchMode() {     //main stopwatch timer function
  breakOutSet = 0;
  if (timerEndState.active) {
    return;
  }
  unsigned long now = millis();
  if (timerDeadlineReached(now, endCountDownMillis)) {  //timer ended
    CountUpMillis = 0;
    endCountDownMillis = 0;
    endCountdown();
    return;
  }
  unsigned long restMillis = now - CountUpMillis;
  unsigned long hours   = ((restMillis / 1000) / 60) / 60;
  unsigned long minutes = (restMillis / 1000) / 60;
  unsigned long seconds = restMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60); 
  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;  
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;
  CRGB countdownColor = CRGB(countdownColorValue); 
  if ((endCountDownMillis - now) <= 10000 && colorchangeCD == 1) {  //red mode at last 10 seconds
    countdownColor = CRGB::Red;
  } 
  if (hours > 0) { //show hours and minutes
    displayNumber(h1,6,countdownColor); 
    displayNumber(h2,4,countdownColor);
    displayNumber(m1,2,countdownColor);
    displayNumber(m2,0,countdownColor); 
  } else { //or show minutes and seconds  
    displayNumber(m1,6,countdownColor);
    displayNumber(m2,4,countdownColor);
    displayNumber(s1,2,countdownColor);
    displayNumber(s2,0,countdownColor);  
  }
}

void displayScoreboardMode() {  //main scoreboard function
  currentMode = 0;
  byte sl1 = scoreboardLeft / 10;
  byte sl2 = scoreboardLeft % 10;
  byte sr1 = scoreboardRight / 10;
  byte sr2 = scoreboardRight % 10;
  displayNumber(sl1,6,scoreboardColorLeft);
  displayNumber(sl2,4,scoreboardColorLeft);
  displayNumber(sr1,2,scoreboardColorRight);
  displayNumber(sr2,0,scoreboardColorRight);
}//end of update scoreboard

void displayLightshowMode() {
  currentMode = 0;
  //if (lightshowMode == 1) {Twinkles();}
  //if (lightshowMode == 2) {Rainbow();}
  //if (lightshowMode == 3) {GreenMatrix();}
  //if (lightshowMode == 4) {blueRain();}
  //if (lightshowMode == 5) {Fire2021();}
  //if (lightshowMode == 6) {Snake();}
  //if (lightshowMode == 7) {Cylon();}
}

void displayRealtimeMode(){   //main RealtimeModes function, always is running
  getsamplePhoto(); 
  #if HAS_SOUNDDETECTOR
    if ( (suspendType == 0 || isAsleep == 0) && clockMode == 9 && realtimeMode == 1) {SpectrumAnalyzer(); }
  #endif
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 0) {Chase();}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 1) {EVERY_N_MILLISECONDS(30) {Twinkles();FastLED.show();}}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 2) {Rainbow();FastLED.show();}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 3) {EVERY_N_MILLISECONDS(100) {GreenMatrix();}}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 4) {blueRain();FastLED.show();}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 5) {EVERY_N_MILLISECONDS(60) {Fire2021();FastLED.show();}}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 6) {EVERY_N_MILLISECONDS(getSlower) {Snake();FastLED.show();}}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 7) {EVERY_N_MILLISECONDS(150) {Cylon(); FastLED.show();}}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 8) {Orbit();}
  if ( (suspendType == 0 || isAsleep == 0) && clockMode == 5 && lightshowMode == 9) {FigureEight();}
}//end of RealtimeModes



#if HAS_SOUNDDETECTOR
void rainbowBars(int band, int barHeight) {
  int xStart = LEDS_PER_SEGMENT * 2 * band;
    for (int y = 0; y < SOUNDDETECTOR_BANDS_HEIGHT; y++) {
      if ( barHeight >= y)  
        LEDs[ANALYZER[xStart + y]] = CHSV(band * (255 / SOUNDDETECTOR_BANDS_WIDTH), 255, 255);
      else
        LEDs[ANALYZER[xStart + y]] = CRGB::Black;  
    }
}

void purpleBars(int band, int barHeight) {
  int xStart = LEDS_PER_SEGMENT * 2 * band;
    for (int y = 0; y < SOUNDDETECTOR_BANDS_HEIGHT; y++) {
      if ( barHeight >= y)  
        LEDs[ANALYZER[xStart + y]] = ColorFromPalette(purplePal, y * (255 / (barHeight + 1)));
      else
        LEDs[ANALYZER[xStart + y]] = CRGB::Black;  
    }
}

void fireBars(int band, int barHeight) {
  int xStart = LEDS_PER_SEGMENT * 2 * band;
    for (int y = 0; y < SOUNDDETECTOR_BANDS_HEIGHT; y++) {
      if ( barHeight >= y)  
        LEDs[ANALYZER[xStart + y]] = ColorFromPalette(heaterPal, y * (255 / (barHeight + 1)));
      else
        LEDs[ANALYZER[xStart + y]] = CRGB::Black;  
    }
}

void changingBars(int band, int barHeight) {
  int xStart = LEDS_PER_SEGMENT * 2 * band;
    for (int y = 0; y < SOUNDDETECTOR_BANDS_HEIGHT; y++) {
      if ( barHeight >= y)  
        LEDs[ANALYZER[xStart + y]] = CHSV(y * (255 / (LEDS_PER_SEGMENT*2)) + colorTimer, 255, 255);
      else
        LEDs[ANALYZER[xStart + y]] = CRGB::Black;  
    }
}

void centerBars(int band, int barHeight) {
    int xStart = LEDS_PER_SEGMENT * 2 * band;
    int totalHeight = LEDS_PER_SEGMENT * 2;
    // Clear the entire bar area first
    for (int y = 0; y < totalHeight; y++) {
        LEDs[ANALYZER[xStart + y]] = CRGB::Black;
    }
    if (barHeight % 2 == 0) barHeight--;
    int yStart = (totalHeight - barHeight) / 2;
    // Draw the bar
    for (int y = yStart; y < yStart + barHeight; y++) {
        int colorIndex = constrain((y - yStart) * (255 / barHeight), 0, 255);
        LEDs[ANALYZER[xStart + y]] = spectrumColor;
    }
}

int clampAnalyzerPeak(int peakHeight) {
    return constrain(peakHeight, 0, SOUNDDETECTOR_BANDS_HEIGHT - 1);
}

void whitePeak(int band) {
    int xStart = LEDS_PER_SEGMENT * 2 * band;
    int peakHeight = clampAnalyzerPeak(peak[band]);
    int previousPeakHeight = clampAnalyzerPeak(prevPeak[band]);
    // Clear the previous peak LED
    LEDs[ANALYZER[xStart + previousPeakHeight]] = CRGB::Black;
    // Draw the new peak
    LEDs[ANALYZER[xStart + peakHeight]] = CHSV(0, 0, 255);
    // Update the previous peak position
    prevPeak[band] = peakHeight;
}

void outrunPeak(int band) {
    int xStart = LEDS_PER_SEGMENT * 2 * band;
    int peakHeight = clampAnalyzerPeak(peak[band]);
    int previousPeakHeight = clampAnalyzerPeak(prevPeak[band]);
    // Clear the previous peak LED
    LEDs[ANALYZER[xStart + previousPeakHeight]] = CRGB::Black;
    // Draw the new peak
    LEDs[ANALYZER[xStart + peakHeight]] = ColorFromPalette(outrunPal, peakHeight * (255 / (LEDS_PER_SEGMENT * 2)));
    // Update the previous peak position
    prevPeak[band] = peakHeight;
}

void waterfall(int band) {
    int totalHeight = LEDS_PER_SEGMENT * 2;
    int xStart = totalHeight * band;
    // Move bar up
    for (int y = totalHeight - 1; y > 0; y--) {
        int indexTo = xStart + y;
        int indexFrom = xStart + y - 1;
        // Check for out-of-bounds access
        if (indexTo >= ANALYZER_SIZE || indexFrom >= ANALYZER_SIZE) {
            Serial.println("Index out of bounds!");
            continue; // Skip to avoid crashing
        }
        LEDs[ANALYZER[indexTo]] = LEDs[ANALYZER[indexFrom]];
    }
    // Map band value to hue
    int hue = constrain(map(SOUNDDETECTOR_bandValues[band], 0, SOUNDDETECTOR_BANDS_HEIGHT, 160, 0), 0, 160);
    // Draw bottom point
    LEDs[ANALYZER[xStart]] = CHSV(hue, 255, 255);
}

void i2sConfig() {
  #if SHELFCLOCK_STANDALONE
  const i2s_config_t i2s_config = {
    .mode               = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate        = SOUNDDETECTOR_SAMPLING_FREQ,
    .bits_per_sample    = i2s_bits_per_sample_t(SOUNDDETECTOR_BITS_PER_SAMPLE),
    .channel_format     = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // or STAND_MSB if you need MSB-first
    .intr_alloc_flags   = 0,
    .dma_buf_count      = 8,
    .dma_buf_len        = SOUNDDETECTOR_SAMPLES,
    .use_apll           = false
  };
  i2s_driver_install(SOUNDDETECTOR_I2S_PORT, &i2s_config, 0, NULL);
  #else
  i2s_chan_config_t channelConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(SOUNDDETECTOR_I2S_PORT, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = 8;
  channelConfig.dma_frame_num = SOUNDDETECTOR_SAMPLES;
  if (i2s_new_channel(&channelConfig, nullptr, &soundDetectorRxChannel) != ESP_OK) {
    soundDetectorRxChannel = nullptr;
    Serial.println("I2S channel setup failed");
    return;
  }
  #endif
  Serial.println("I2S Config Setup");
}

void i2sPins() {
  #if SHELFCLOCK_STANDALONE
  const i2s_pin_config_t pin_config = {
    .bck_io_num = SOUNDDETECTOR_I2S_SCK,
    .ws_io_num = SOUNDDETECTOR_I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = SOUNDDETECTOR_I2S_SD
  };
  i2s_set_pin(SOUNDDETECTOR_I2S_PORT, &pin_config);
  #else
  if (soundDetectorRxChannel == nullptr) {
    return;
  }
  const i2s_std_config_t standardConfig = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SOUNDDETECTOR_SAMPLING_FREQ),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = static_cast<gpio_num_t>(SOUNDDETECTOR_I2S_SCK),
      .ws = static_cast<gpio_num_t>(SOUNDDETECTOR_I2S_WS),
      .dout = I2S_GPIO_UNUSED,
      .din = static_cast<gpio_num_t>(SOUNDDETECTOR_I2S_SD),
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  if (i2s_channel_init_std_mode(soundDetectorRxChannel, &standardConfig) != ESP_OK ||
      i2s_channel_enable(soundDetectorRxChannel) != ESP_OK) {
    Serial.println("I2S pin setup failed");
    return;
  }
  #endif
  Serial.println("I2S Pins Setup");
}

static esp_err_t readI2sSamples(void *buffer, size_t bufferSize, size_t *bytesRead) {
  #if SHELFCLOCK_STANDALONE
  return i2s_read(SOUNDDETECTOR_I2S_PORT, buffer, bufferSize, bytesRead, (TickType_t)0);
  #else
  if (soundDetectorRxChannel == nullptr) {
    *bytesRead = 0;
    return ESP_ERR_INVALID_STATE;
  }
  return i2s_channel_read(soundDetectorRxChannel, buffer, bufferSize, bytesRead, 0);
  #endif
}

int i2sWaveformRead() {
    static int16_t sBuffer[8]; // Buffer for I2S data
    size_t bytesIn = 0;
    float mean = 0;
    // Perform I2S read
    esp_err_t result = readI2sSamples(&sBuffer, sizeof(sBuffer), &bytesIn);
    if (result != ESP_OK) {
        return 0; // Return 0 if read fails
    }
    // Calculate mean value of the samples
    int samples_read = bytesIn / sizeof(int16_t); // Calculate number of samples read
    if (samples_read > 0) {
        for (int i = 0; i < samples_read; ++i) {
         if (sBuffer[i] < 0) { mean += (sBuffer[i]*-1); } else { mean += sBuffer[i]; }
        }
        mean /= samples_read; // Compute average
    }
    // Map the mean value from its range to 30-1023
    // Assuming the mean ranges from -4096 to 4096 (16-bit signed)
    if (mean < 30) mean = 0;
    int scaledValue = map(mean, 30, 4096, 0, 1023);
    scaledValue = constrain(mean, 0, 1023); // Ensure the value is within 0-4095
   //   Serial.println(scaledValue);
    return scaledValue;
}

void readAndProcessAudio() {
    static double vReal[SOUNDDETECTOR_SAMPLES];
    static double vImag[SOUNDDETECTOR_SAMPLES];
    int16_t buffer[SOUNDDETECTOR_SAMPLES];
    size_t bytesIn = 0;
    static double AGC_amp = 1.0; // Initialize AGC amplitude
    const double agcSpeed = 0.1;  // AGC adaptation speed (adjust as needed)
    const double noiseFloor = 500.0; // Set this value to your noise floor threshold

    // Reset SOUNDDETECTOR_bandValues for each loop iteration
    memset(SOUNDDETECTOR_bandValues, 0, sizeof(SOUNDDETECTOR_bandValues));

    // Read audio data (non-blocking)
    readI2sSamples(&buffer, sizeof(buffer), &bytesIn);
    if (bytesIn < sizeof(buffer)) {
        // Not enough data read, fill the rest with zeros
        memset(&buffer[bytesIn / sizeof(int16_t)], 0, sizeof(buffer) - bytesIn);
    }

    // Convert samples to double
    for (int i = 0; i < SOUNDDETECTOR_SAMPLES; i++) {
        vReal[i] = (double)buffer[i];
        vImag[i] = 0.0;
    }

    // Perform FFT
    FFT.Windowing(vReal, SOUNDDETECTOR_SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.Compute(vReal, vImag, SOUNDDETECTOR_SAMPLES, FFT_FORWARD);
    FFT.ComplexToMagnitude(vReal, vImag, SOUNDDETECTOR_SAMPLES);

    // Apply dynamic range compression and map FFT bins to frequency bands
    double maxCompressedMagnitude = 0.0;

    for (int i = 1; i < (SOUNDDETECTOR_SAMPLES / 2); i++) {
        int bandIndex = -1;

        if (i >= 1 && i <= 2) bandIndex = 0;          // 62.5 Hz - 125 Hz
        else if (i >= 3 && i <= 4) bandIndex = 1;     // 187.5 Hz - 250 Hz
        else if (i >= 5 && i <= 6) bandIndex = 2;     // 312.5 Hz - 375 Hz
        else if (i >= 7 && i <= 8) bandIndex = 3;     // 437.5 Hz - 500 Hz
        else if (i >= 9 && i <= 16) bandIndex = 4;    // 562.5 Hz - 1,000 Hz
        else if (i >= 17 && i <= 52) bandIndex = 5;   // 1,062.5 Hz - 2,000 Hz
        else if (i >= 53 && i <= 56) bandIndex = 6;   // 2,062.5 Hz - 3,000 Hz
        else if (i >= 57 && i <= 100) bandIndex = 7;   // 3,062.5 Hz - 4,000 Hz

        if (bandIndex != -1) {
            // Apply noise floor threshold
            if (vReal[i] < noiseFloor) {
                vReal[i] = 0.0; // Ignore values below the noise floor
            } else {
                // Apply dynamic range compression using logarithmic scaling
                double magnitude = vReal[i];
                double compressedMagnitude = log10(magnitude - noiseFloor + 1.0); // +1 to avoid log(0)

                // Sum the compressed magnitudes into the band
                SOUNDDETECTOR_bandValues[bandIndex] += compressedMagnitude;

                // Update maxCompressedMagnitude
                if (compressedMagnitude > maxCompressedMagnitude) {
                    maxCompressedMagnitude = compressedMagnitude;
                }
            }
        }
    }

    // Update AGC amplitude towards the current maximum compressed magnitude
    AGC_amp = (1.0 - agcSpeed) * AGC_amp + agcSpeed * maxCompressedMagnitude;

    // Map compressed magnitudes to bar heights
    for (int band = 0; band < SOUNDDETECTOR_BANDS_WIDTH; band++) {
        double magnitude = SOUNDDETECTOR_bandValues[band];

        // Scale using AGC amplitude
        double scaledMagnitude = magnitude / (AGC_amp + 1e-6); // Avoid division by zero

        // Map to bar height
        int barHeight = (int)(scaledMagnitude * SOUNDDETECTOR_BANDS_HEIGHT);

        // Constrain barHeight
        barHeight = constrain(barHeight, 0, SOUNDDETECTOR_BANDS_HEIGHT);

        // Store barHeight in SOUNDDETECTOR_bandValues[band]
        SOUNDDETECTOR_bandValues[band] = barHeight;
    }
}

void SpectrumAnalyzer() {    //mostly from github.com/justcallmekoko/Arduino-FastLED-Music-Visualizer/blob/master/music_visualizer.ino
  currentMode = 0;
  if (spectrumMode < 12) {
	  const TProgmemRGBPalette16 FireColors = {0xFFFFCC, 0xFFFF99, 0xFFFF66, 0xFFFF33, 0xFFFF00, 0xFFCC00, 0xFF9900, 0xFF6600, 0xFF3300, 0xFF3300, 0xFF0000, 0xCC0000, 0x990000, 0x660000, 0x330000, 0x110000};
	  const TProgmemRGBPalette16 FireColors2 = {0xFFFF99, 0xFFFF66, 0xFFFF33, 0xFFFF00, 0xFFCC00, 0xFF9900, 0xFF6600, 0xFF3300, 0xFF3300, 0xFF0000, 0xCC0000, 0x990000, 0x660000, 0x330000, 0x110000};
	  const TProgmemRGBPalette16 FireColors3 = {0xFFFF66, 0xFFFF33, 0xFFFF00, 0xFFCC00, 0xFF9900, 0xFF6600, 0xFF3300, 0xFF3300, 0xFF0000, 0xCC0000, 0x990000, 0x660000, 0x330000, 0x110000};
	  int audio_input = i2sWaveformRead();
	  if (audio_input > 0) {
   //   Serial.println("spectrum running");
		SOUNDDETECTOR_pre_react = (SPECTRUM_PIXELS * audio_input) / 1023L; // TRANSLATE AUDIO LEVEL TO NUMBER OF LEDs
		if (SOUNDDETECTOR_pre_react > SOUNDDETECTOR_react) // ONLY ADJUST LEVEL OF LED IF LEVEL HIGHER THAN CURRENT LEVEL
		  SOUNDDETECTOR_react = SOUNDDETECTOR_pre_react;
	   }
	  for(int i = SPECTRUM_PIXELS - 1; i >= 0; i--) {
      int fake =  i * LEDS_PER_SEGMENT;
      int fireChoice = random(4);
      if (i < SOUNDDETECTOR_react)
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        if (spectrumColorSettings == 0) { spectrumColor = CRGB(spectrumColorValue); }
        if (spectrumColorSettings == 1) { spectrumColor = CHSV((255/SPECTRUM_PIXELS)*i, 255, 255);}
        if (spectrumColorSettings == 2) { spectrumColor = colorWheel((i * 256 / 50 + colorWheelPosition) % 256);}
        if (spectrumColorSettings == 3 && fireChoice >= 2) { spectrumColor = ColorFromPalette( FireColors, (255/SPECTRUM_PIXELS)*i, 255, LINEARBLEND);}
        if (spectrumColorSettings == 3 && fireChoice == 0) { spectrumColor = ColorFromPalette( FireColors2, (255/SPECTRUM_PIXELS)*i, 255, LINEARBLEND);}
        if (spectrumColorSettings == 3 && fireChoice == 1) { spectrumColor = ColorFromPalette( FireColors3, (255/SPECTRUM_PIXELS)*i, 255, LINEARBLEND);}
        if (spectrumColorSettings == 4) { spectrumColor = ColorFromPalette( OceanColors_p, (255/SPECTRUM_PIXELS)*i, 255, LINEARBLEND);}
        if (spectrumColorSettings == 5) { spectrumColor = ColorFromPalette( ForestColors_p, (255/SPECTRUM_PIXELS)*i, 255, LINEARBLEND);}
        if (spectrumColorSettings == 6) { spectrumColor = colorWheel2(((255) + colorWheelPositionTwo) % 256);}
        if (spectrumMode == 0) {LEDs[FAKE_LEDs_C_BMUP[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 1) {LEDs[FAKE_LEDs_C_CMOT[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 2) {LEDs[FAKE_LEDs_C_BLTR[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 3) {LEDs[FAKE_LEDs_C_TLBR[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 4) {LEDs[FAKE_LEDs_C_VERT[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 5) {LEDs[FAKE_LEDs_C_TMDN[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 6) {LEDs[FAKE_LEDs_C_CSIN[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 7) {LEDs[FAKE_LEDs_C_BRTL[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 8) {LEDs[FAKE_LEDs_C_TRBL[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 9) {LEDs[FAKE_LEDs_C_OUTS[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 10) {LEDs[FAKE_LEDs_C_VERT2[s+((fake))]] = spectrumColor;}
        if (spectrumMode == 11) {LEDs[FAKE_LEDs_C_OUTS2[s+((fake))]] = spectrumColor;}
      }
      else
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        if (spectrumBackgroundSettings == 0) { spectrumBackgroundColor = CRGB(spectrumBackgroundColorValue); }
        if (spectrumBackgroundSettings == 1) { spectrumBackgroundColor = colorWheel((i * 256 / 50 + colorWheelPosition) % 256);}
        if (spectrumBackgroundSettings == 2) { spectrumBackgroundColor = colorWheel2(((255) + colorWheelPositionTwo) % 256);}
        if (spectrumMode == 0) {LEDs[FAKE_LEDs_C_BMUP[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 1) {LEDs[FAKE_LEDs_C_CMOT[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 2) {LEDs[FAKE_LEDs_C_BLTR[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 3) {LEDs[FAKE_LEDs_C_TLBR[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 4) {LEDs[FAKE_LEDs_C_VERT[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 5) {LEDs[FAKE_LEDs_C_TMDN[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 6) {LEDs[FAKE_LEDs_C_CSIN[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 7) {LEDs[FAKE_LEDs_C_BRTL[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 8) {LEDs[FAKE_LEDs_C_TRBL[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 9) {LEDs[FAKE_LEDs_C_OUTS[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 10) {LEDs[FAKE_LEDs_C_VERT2[s+((fake))]] = spectrumBackgroundColor;}
        if (spectrumMode == 11) {LEDs[FAKE_LEDs_C_OUTS2[s+((fake))]] = spectrumBackgroundColor;}
      }    
	  }
	  FastLED.show();                              // Increment the Hue to get the Rainbow
	  colorWheelPosition = colorWheelPosition - colorWheelSpeed; // SPEED OF COLOR WHEEL
	  if (colorWheelPosition < 0) // RESET COLOR WHEEL
		colorWheelPosition = 255;
	  SOUNDDETECTOR_decay_check++;
	  if (SOUNDDETECTOR_decay_check > SOUNDDETECTOR_decay) {
      SOUNDDETECTOR_decay_check = 0;
      if (SOUNDDETECTOR_react > 0)
        SOUNDDETECTOR_react--;
	  }
	}// end spectrumMode < 12
  else {

    if (spectrumMode == 15) {
      spectrumColor = CRGB(spectrumColorValue);  //spectrum selected color
      if (spectrumColorSettings == 2) { spectrumColor = colorWheel((colorWheelPositionTwo * 256 / 50 + colorWheelPosition) % 256);}  //cycle fast
      if (spectrumColorSettings == 6) { spectrumColor = colorWheel2((colorWheelPositionTwo));}  //cycle slow
      colorWheelPosition = colorWheelPosition - colorWheelSpeed; // SPEED OF COLOR WHEEL
      if (colorWheelPosition < 0){ colorWheelPosition = 255; } // RESET COLOR WHEEL
    }

    // Reset SOUNDDETECTOR_bandValues[]
    for (int i = 0; i<SOUNDDETECTOR_BANDS_WIDTH; i++){
      SOUNDDETECTOR_bandValues[i] = 0;
    }

    readAndProcessAudio();

    // Process the FFT data into bar heights
    for (byte band = 0; band < SOUNDDETECTOR_BANDS_WIDTH; band++) {

      // Scale the bars for the display
      int barHeight = SOUNDDETECTOR_bandValues[band] ;     if (barHeight > SOUNDDETECTOR_BANDS_HEIGHT) {
        SOUNDDETECTOR_Amplitude += ((barHeight - SOUNDDETECTOR_BANDS_HEIGHT) /8);
        barHeight = SOUNDDETECTOR_BANDS_HEIGHT;
      }

      // Small amount of averaging between frames
      barHeight = ((oldBarHeights[band] * 1) + barHeight) / 2;

      // Move peak up
      if (barHeight > peak[band]) {
        peak[band] = min(SOUNDDETECTOR_BANDS_HEIGHT, barHeight);
      }

      if (spectrumMode < 12 + 7)
		    buttonPushCounter = spectrumMode - 12;

      // Draw bars
      switch (buttonPushCounter) {
        case 0:
          rainbowBars(band, barHeight);
          break;
        case 1:
          // No bars on this one
          break;
        case 2:
          purpleBars(band, barHeight);
          break;
        case 3:
          centerBars(band, barHeight);
          break;
        case 4:
          changingBars(band, barHeight);
          break;
        case 5:
          fireBars(band, barHeight);
          break;
        case 6:
          waterfall(band);
          break;
      }

      // Draw peaks
      switch (buttonPushCounter) {
        case 0:
          whitePeak(band);
          break;
        case 1:
          outrunPeak(band);
          break;
        case 2:
          whitePeak(band);
          break;
        case 3:
          // No peaks
          break;
        case 4:
          // No peaks
          break;
        case 5:
          // No peaks
          break;
        case 6:
          // No peaks
          break;
      }

      // Save oldBarHeights for averaging later
      oldBarHeights[band] = barHeight;
    }

	// Decay peak
	EVERY_N_MILLISECONDS(60) {
		for (byte band = 0; band < SOUNDDETECTOR_BANDS_WIDTH; band++) {
			if (peak[band] > 0) {
				peak[band] -= 1;
			} else {
				peak[band] = 0; // Ensure it doesn't go negative
			}
		}
	}
/*
	Serial.print("Peak values: ");
	for (byte band = 0; band < SOUNDDETECTOR_BANDS_WIDTH; band++) {
		Serial.print(peak[band]);
		Serial.print(" ");
	}
	Serial.println();
*/
    // Used in some of the patterns
    EVERY_N_MILLISECONDS(10) {
      colorTimer++;
    }

    EVERY_N_SECONDS(1) {
      if (SOUNDDETECTOR_Amplitude > 500) {
        SOUNDDETECTOR_Amplitude -= (SOUNDDETECTOR_Amplitude /150);
      }
    }

    EVERY_N_SECONDS(10) {
		// Auto Switch mode
      if (spectrumMode >= 12 + 7) buttonPushCounter = (buttonPushCounter + 1) % 7;
      allBlank();
    }

    FastLED.show();
  } // end else spectrumMode > 12
}
#endif

static void finishTimerEndEffect() {
  timerEndState.active = false;
  timerEndState.framesRemaining = 0;
  clockMode = currentMode;
  realtimeMode = currentReal;
  if (!breakOutSet) {
    scrollAfterSong("tIMEr Ended      tIMEr Ended", 0);
  }
  allBlank();
}

static void serviceTimerEndEffect() {
  if (!timerEndState.active) {
    return;
  }
  if (clockMode != 1 && clockMode != 4) {
    timerEndState.active = false;
    timerEndState.framesRemaining = 0;
    return;
  }

  CRGB color = colorWheel((205 / 50 + colorWheelPosition) % 256);
  displayNumber(38,5,color);  //E 38
  displayNumber(79,3,color);  //n 79
  displayNumber(69,1,color);  //d 69
  FastLED.show();
  colorWheelPosition++;

  if (timerEndState.framesRemaining > 0) {
    timerEndState.framesRemaining--;
  }
  if (timerEndState.framesRemaining == 0) {
    finishTimerEndEffect();
  }
}

void endCountdown() {  //countdown timer has reached 0, sound alarm and display End
  Serial.println("timer ends");
  breakOutSet = 0;
  FastLED.setBrightness(255);
  uint32_t alarmSongRequestId = 0;
  allBlank();
  #if HAS_BUZZER
  if (useAudibleAlarm == 1) { //alarm, ok
    if(!platformServices().readLocalTime(timeinfo)){ Serial.println("Failed to obtain time"); }
    refreshSpecialAudibleAlarmFromCurrentTime();
    String songName = alarmSongPath(specialAudibleAlarm);  // Get the song path from the selected alarm name
    Serial.print("alarmName ");
    Serial.println(specialAudibleAlarm);
    Serial.print("songName ");
    Serial.println(songName);
    CRGB color = colorWheel((205 / 50 + colorWheelPosition) % 256);
    displayNumber(38,5,color);  //E 38
    displayNumber(79,3,color);  //n 79
    displayNumber(69,1,color);  //d 69
    FastLED.show();
    colorWheelPosition++;
    alarmSongRequestId = playRTTTLsong(songName, 1);
  } else {  //no alarm, flash lights one frame per loop
    timerEndState.active = true;
    timerEndState.framesRemaining = 1000;
    return;
  }
  #else
    timerEndState.active = true;
    timerEndState.framesRemaining = 1000;
    return;
  #endif
  clockMode = currentMode;
  realtimeMode = currentReal;
  if (!breakOutSet) {scrollAfterSong("tIMEr Ended      tIMEr Ended", alarmSongRequestId);}
  allBlank();
}

CRGB colorWheel(int pos) {   //color wheel for mostly spectrum analyzer
  CRGB color (0,0,0);
  if(pos < 85) {
    color.g = 0;
    color.r = ((float)pos / 85.0f) * 255.0f;
    color.b = 255 - color.r;
  } else if(pos < 170) {
    color.g = ((float)(pos - 85) / 85.0f) * 255.0f;
    color.r = 255 - color.g;
    color.b = 0;
  } else if(pos < 256) {
    color.b = ((float)(pos - 170) / 85.0f) * 255.0f;
    color.g = 255 - color.b;
    color.r = 1;
  }
  return color;
}

CRGB colorWheel2(int pos) {   //color wheel for things not the spectrum analyzer
  CRGB color (0,0,0);
  if(pos < 85) {
    color.g = 0;
    color.r = ((float)pos / 85.0f) * 255.0f;
    color.b = 255 - color.r;
  } else if(pos < 170) {
    color.g = ((float)(pos - 85) / 85.0f) * 255.0f;
    color.r = 255 - color.g;
    color.b = 0;
  } else if(pos < 256) {
    color.b = ((float)(pos - 170) / 85.0f) * 255.0f;
    color.g = 255 - color.b;
    color.r = 1;
  }
  return color;
}

static void clearScrollQueue() {
  scrollSequence.cancel();
  pendingScrolls.clear();
  pendingScrollIndex = 0;
}

static void startNextScroll() {
  if (scrollSequence.active()) {
    return;
  }
  if (pendingScrollIndex >= pendingScrolls.size()) {
    clearScrollQueue();
    return;
  }

  const PendingScroll &request = pendingScrolls[pendingScrollIndex];
  #if HAS_BUZZER
  if (!songRequestIsComplete(request.waitForSongRequest)) {
    return;
  }
  #endif
  activeScrollClockMode = request.startingClockMode;
  activeScrollRealtimeMode = request.startingRealtimeMode;
  scrollColor = CRGB(scrollColorValue);
  if (scrollColorSettings == 1 && pastelColors == 0) {
    scrollColor = CHSV(random(0, 255), 255, 255);
  }
  if (scrollColorSettings == 1 && pastelColors == 1) {
    scrollColor = CRGB(random(0, 255), random(0, 255), random(0, 255));
  }
  scrollSequence.begin(request.text.c_str(), millis());
}

static bool scrollIsBusy() {
  return scrollSequence.active() || pendingScrollIndex < pendingScrolls.size();
}

static void cancelScrollIfInterrupted() {
  if (!scrollSequence.active() ||
      !scrollShouldAbort(activeScrollClockMode, activeScrollRealtimeMode)) {
    return;
  }

  breakOutSet = 1;
  clearScrollQueue();
  allBlank();
  allBlank();
}

static void serviceScroll() {
  startNextScroll();
  cancelScrollIfInterrupted();
  if (!scrollSequence.active()) {
    return;
  }

  const unsigned long now = millis();
  if (!scrollSequence.frameDue(now)) {
    return;
  }

  if (!scrollSequence.hasFrame()) {
    scrollSequence.cancel();
    pendingScrollIndex++;
    allBlank();
    allBlank();
    startNextScroll();
    return;
  }

  for (int index = 0; index < SEGMENTS_LEDS; index++) {
    LEDs[index] = CRGB::Black;
  }
  for (size_t position = 0; position < shelfclock::ScrollSequence::kVisibleGlyphs; position++) {
    const uint16_t glyph = scrollSequence.glyph(position);
    if (glyph != 96) {
      displayNumber(glyph, 6 - position, scrollColor);
    }
  }
  FastLED.show();
  scrollSequence.advance(now);
}

static void scrollAfterSong(String IncomingString, uint32_t songRequestId) {
  Serial.println("Scroll Text: " + IncomingString);
  breakOutSet = 0;
  chaseState.active = false;
  if (IncomingString.length() > shelfclock::ScrollSequence::kMaxTextLength) {
    IncomingString = "ArE U A HAckEr";
  }

  PendingScroll request;
  request.text = IncomingString;
  request.startingClockMode = clockMode;
  request.startingRealtimeMode = realtimeMode;
  request.waitForSongRequest = songRequestId;
  pendingScrolls.push_back(request);
  startNextScroll();
}

void scroll(String IncomingString) {
  scrollAfterSong(IncomingString, 0);
}


void printLocalTime() {  //what could this do
  #if HAS_RTC
    struct tm rtcTime = {};
    platformServices().readRtc(rtcTime);
    char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    char monthsOfTheYear[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (!platformServices().rtcLostPower()) {
      Serial.print("DS3231 Time: ");
      Serial.print(daysOfTheWeek[rtcTime.tm_wday]);
      Serial.print(", ");
      Serial.print(monthsOfTheYear[rtcTime.tm_mon]);
      Serial.print(" ");
      Serial.print(rtcTime.tm_mday, DEC);
      Serial.print(" ");
      Serial.print(rtcTime.tm_year + 1900, DEC);
      Serial.print(' ');
      Serial.print(rtcTime.tm_hour, DEC);
      Serial.print(':');
      Serial.print(rtcTime.tm_min, DEC);
      Serial.print(':');
      Serial.print(rtcTime.tm_sec, DEC);
      Serial.println();
    }
  #endif
  if(!platformServices().readLocalTime(timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "Local Time: %A, %B %d %Y %H:%M:%S");
}


void checkSleepTimer(){  //controls suspend mode
  #if HAS_BUZZER
  if(!isRtttlPlayingSafe()) {  // don't allow chimes to keep it awake. 
    if (suspendType != 0) {sleepTimerCurrent++;}  //sleep enabled? add one to timer
      #if HAS_SOUNDDETECTOR
 /*       int audio_input1 = i2sWaveformRead(); 
        if (audio_input1 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
        int audio_input2 = i2sWaveformRead(); 
        if (audio_input2 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
        int audio_input3 = i2sWaveformRead(); 
        if (audio_input3 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
        int audio_input4 = i2sWaveformRead(); 
        if (audio_input4 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
        int audio_input5 = i2sWaveformRead();
        if (audio_input5 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
        */
        SOUNDDETECTOR_averageAudioInput = i2sWaveformRead();
        if (SOUNDDETECTOR_averageAudioInput > 50) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      #endif
    if ((suspendType != 0) && sleepTimerCurrent >= (suspendFrequency * 60)) {
      sleepTimerCurrent = 0; 
      isAsleep = 1; 
      allBlank(); 
	    if (suspendType == 2) {shelfBlank();}//if suspend all leds in on, blank shelflights
    }  //sleep enabled, been some amount of time, go to sleep
  }
  #else
    #if HAS_SOUNDDETECTOR
      if (suspendType != 0) {sleepTimerCurrent++;}  //sleep enabled? add one to timer
      int audio_input1 = i2sWaveformRead();
      if (audio_input1 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      int audio_input2 = i2sWaveformRead();
      if (audio_input2 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      int audio_input3 = i2sWaveformRead(); 
      if (audio_input3 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      int audio_input4 = i2sWaveformRead();
      if (audio_input4 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      int audio_input5 = i2sWaveformRead(); 
      if (audio_input5 > 100) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      SOUNDDETECTOR_averageAudioInput = (audio_input1 + audio_input2 + audio_input3 + audio_input4 + audio_input5) / 5;
      if (SOUNDDETECTOR_averageAudioInput > 50) {sleepTimerCurrent = 0; isAsleep = 0;} //try it with the real sensor, the digital one was tripping false positives just as much
      if ((suspendType != 0) && sleepTimerCurrent >= (suspendFrequency * 60)) {
        sleepTimerCurrent = 0; 
        isAsleep = 1; 
        allBlank(); 
	      if (suspendType == 2) {shelfBlank();}//if suspend all leds in on, blank shelflights
      }  //sleep enabled, been some amount of time, go to sleep
    #endif
  #endif
}




void GetBrightnessLevel() {
  #if HAS_PHOTOSENSOR
  getsamplePhoto(); 
    // 1) instant average in O(1)
    uint16_t avgReading = brightnessSum / PHOTO_SAMPLES;
  
    //Serial.print("avgReading: ");
    //Serial.println(avgReading);
    // 2) map to your 10 discrete levels
    const int NUM_LEVELS = 10;
    static const int sensorThresholds[NUM_LEVELS] = {
      0,   200,  400,  700,  900,
      1200,2000, 3185, 3640, 4095
    };
    static const int brightnessLevels[NUM_LEVELS] = {
      255, 150, 100, 75, 60,
       50,  40,  30, 20, 10
    };
  
    int lvl = 0;
    for (int i = NUM_LEVELS - 1; i >= 0; i--) {
      if (avgReading >= sensorThresholds[i]) {
        lvl = i;
        break;
      }
    }
    lightSensorValue = brightnessLevels[lvl];
  
    // 3) apply (user override if brightness != 10)
    FastLED.setBrightness(brightness != 10 ? brightness : lightSensorValue);
  #else
    FastLED.setBrightness(brightness);
  #endif
  }


  void updateRainForecast() {
    #if HAS_USWEATHER
    if (!platformServices().networkConnected()) {
      Serial.println("Rain forecast skipped: Wi-Fi down, using cached values.");
      return;
    }
    if (latitude[0] == '\0' || longitude[0] == '\0') {
      Serial.println("Rain forecast skipped: missing coordinates.");
      return;
    }
  
    // — find last Monday —
    time_t now = time(NULL);
    struct tm todayInfo;
    localtime_r(&now, &todayInfo);
    int daysSinceMonday = (todayInfo.tm_wday + 6) % 7; // Mon→0…Sun→6
    time_t lastMonday = now - daysSinceMonday * 86400;
  
    // format start/end dates
    char startDate[11], endDate[11];
    {
      struct tm mInfo;
      localtime_r(&lastMonday, &mInfo);
      strftime(startDate, sizeof(startDate), "%Y-%m-%d", &mInfo);
    }
    {
      time_t endTime = lastMonday + 13 * 86400;  // 14 days total
      struct tm eInfo;
      localtime_r(&endTime, &eInfo);
      strftime(endDate, sizeof(endDate), "%Y-%m-%d", &eInfo);
    }
  
    // build the URL (https://api.open-meteo.com/v1/forecast?latitude=47.070808&longitude=-122.804500&daily=precipitation_probability_max0)
    String apiUrl = String("https://api.open-meteo.com/v1/forecast")
                  + "?latitude="  + String(latitude, 9)
                  + "&longitude=" + String(longitude, 9)
                  + "&daily=precipitation_probability_max,rain_sum,showers_sum,snowfall_sum"
                  + "&start_date=" + startDate
                  + "&&end_date="   + endDate
                  + "&timezone=auto";
  
    Serial.println("Fetching rain chance Mon→Sun+1wk:");
    Serial.println(apiUrl);
  
    // fetch JSON
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(apiUrl)) {
      Serial.println("Rain forecast begin failed, using cached values.");
      return;
    }
    int code = http.GET();
    if (code != 200) {
      Serial.printf("HTTP GET failed: %d\n", code);
      http.end();
      return;
    }
    String payload = http.getString();
    http.end();
  
    // parse into a heap‐allocated doc (no stack overflow)
    DynamicJsonDocument doc(4*1024);
    auto err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("JSON parse failed: ");
      Serial.println(err.f_str());
      return;
    }
  
    // extract probabilities
    JsonArray probs = doc["daily"]["precipitation_probability_max"];
    JsonArray rain = doc["daily"]["rain_sum"];
    JsonArray showers = doc["daily"]["showers_sum"];
    JsonArray snow = doc["daily"]["snowfall_sum"];
    if (probs.size() != 14 || rain.size() != 14 || showers.size() != 14 || snow.size() != 14) {
      Serial.println("Incomplete precipitation forecast, using cached values.");
      return;
    }
    const int days = 14;
    int nextRainForecast[14];
    uint8_t nextPrecipitationType[14] = {};
  
     // map 50–100% → brightness 20–255, below 50% = off
  for (int i = 0; i < days; i++) {
    int pct = constrain(probs[i].as<int>(), 0, 100);
    if (pct < 50) {// no LED if under 50%
      nextRainForecast[i] = 0;               
    } else {
      nextRainForecast[i] = map(pct, 50, 100, 20, 255);
    }
    if (rain[i].is<float>() && showers[i].is<float>() && snow[i].is<float>()) {
      const bool hasRain = rain[i].as<float>() > 0 || showers[i].as<float>() > 0;
      const bool hasSnow = snow[i].as<float>() > 0;
      nextPrecipitationType[i] = hasSnow ? (hasRain ? PRECIP_MIXED : PRECIP_SNOW)
                                        : (hasRain ? PRECIP_RAIN : PRECIP_UNKNOWN);
    }
  }
  
    // debug print
    for (int i = 0; i < days; i++) {
      Serial.printf("Day %2d: %3d%% → bri %3d\n",
                    i, probs[i].as<int>(), nextRainForecast[i]);
    }
    
  // --- Adjust the order of the LED array to match your wiring ---
  // Required order: 13,12,11,10,9,8,7,0,1,2,3,4,5,6
  int tempHolder[14];
  uint8_t typeHolder[14];
  static const uint8_t idxMap[14] = {
      7,8,9,10,11,12,13,   // new[0..6]
      6, 5, 4, 3, 2,1,0    // new[7..13]
  };
  for (int i = 0; i < 14; i++) {
      tempHolder[i] = nextRainForecast[idxMap[i]];
      typeHolder[i] = nextPrecipitationType[idxMap[i]];
  }
  portENTER_CRITICAL(&rainForecastMux);
  memcpy(rainForecast, tempHolder, sizeof(tempHolder));
  memcpy(precipitationType, typeHolder, sizeof(typeHolder));
  portEXIT_CRITICAL(&rainForecastMux);

    #endif
  }
  

  void getNoaaWeather() {
    #if HAS_USWEATHER
    if (!platformServices().networkConnected()) {
      Serial.println("NOAA weather skipped: Wi-Fi down, using cached values.");
      return;
    }
    if (latitude[0] == '\0' || longitude[0] == '\0') {
      Serial.println("NOAA weather skipped: missing coordinates.");
      return;
    }
    // 1) build the DWML XML URL (https://forecast.weather.gov/MapClick.php?lat=47.070&lon=-122.804&unit=0&lg=english&FcstType=dwml)
    float latF = atof(latitude);
    float lonF = atof(longitude);
    String url = String("https://forecast.weather.gov/MapClick.php") + "?lat=" + String(latF, 3) + "&lon=" + String(lonF, 3) + "&unit=0&lg=english&FcstType=dwml";
    Serial.println(url);
    // 2) start the request
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(url)) {
      Serial.println("NOAA begin failed, using cached values.");
      return;
    }
    http.setUserAgent(
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
      "AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/115.0.0.0 Safari/537.36"
    );
  //  http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" );
  //  http.addHeader("Accept-Language", "en-US,en;q=0.9");
    http.addHeader("Referer",      "https://forecast.weather.gov/");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      Serial.printf("HTTP GET failed: %d\n", code);
      http.end();
      return;
    }
    // 3) stream-parse the XML
    WiFiClient* in = http.getStreamPtr();
    char buf[12];
    float parsedTemp = noaaTemp;
    float parsedHumidity = noaaHumidity;
    bool tempFound = false;
    bool humidityFound = false;
    // — parse current “apparent” temperature —
    const char* tMarker = "<temperature type=\"apparent\"";
    if ( in->find(tMarker) && in->find("<value>") ) {
      size_t i = 0;
      while (in->available() && i < sizeof(buf)-1) {
        char c = in->read();
        if (c == '<') break;
        buf[i++] = c;
      }
      buf[i] = '\0';
      parsedTemp = atof(buf);
      tempFound = true;
    } else {
      Serial.println("temp element not found");
    }
    // — parse current “relative” humidity —
    const char* hMarker = "<humidity type=\"relative\"";
    if ( in->find(hMarker) && in->find("<value>") ) {
      size_t i = 0;
      while (in->available() && i < sizeof(buf)-1) {
        char c = in->read();
        if (c == '<') break;
        buf[i++] = c;
      }
      buf[i] = '\0';
      parsedHumidity = atof(buf);
      humidityFound = true;
    } else {
      Serial.println("humidity element not found");
    }
    if (tempFound) {
      noaaTemp = parsedTemp;
    } else {
      Serial.println("Keeping cached NOAA temperature.");
    }
    if (humidityFound) {
      noaaHumidity = parsedHumidity;
    } else {
      Serial.println("Keeping cached NOAA humidity.");
    }
    // 4) report and clean up
    Serial.printf("NOAA → Temp: %.1f°F, Humidity: %.1f%%\n", noaaTemp, noaaHumidity);
    http.end();
    #endif
  }
  




  void fetchTides() {
    #if HAS_USWEATHER
    if (!platformServices().networkConnected()) {
      Serial.println("Tide fetch skipped: Wi-Fi down, using cached values.");
      dailyPollSuccess = false;
      dailyPollFailed++;
      return;
    }
    HTTPClient http;
    bool hourlyOk   = false;
    bool highLowOk  = false;
    float nextTideHeight[48];
    memcpy(nextTideHeight, tideHeight, sizeof(nextTideHeight));
    char nextTextTides[sizeof(textTides)];
    snprintf(nextTextTides, sizeof(nextTextTides), "%s", textTides);
  
    // 1) build base URL
    String baseURL = String("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter")
      + "?station="  + stationID
      + "&product=predictions"
      + "&datum=MLLW"
      + "&time_zone=lst_ldt"
      + "&units=english"
      + "&application=DataAPI_Sample"
      + "&format=json";
  
    // 2) compute today’s YYYYMMDD
    time_t now = time(nullptr);
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char todayStr[9];
    strftime(todayStr, sizeof(todayStr), "%Y%m%d", &tmInfo);
  
    // 3) fetch next 48h
    String url48 = baseURL + "&begin_date=" + todayStr + "&range=48&interval=h";
    Serial.println("Polling next 48 hours of tide predictions…");
    Serial.println(url48);
  
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    int httpCode = -1;
    if (!http.begin(url48)) {
      Serial.println("Tide hourly begin failed, using cached values.");
    } else {
      http.setUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/115.0.0.0 Safari/537.36"
      );
      http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" );
      http.addHeader("Accept-Language", "en-US,en;q=0.9");
      http.addHeader("Referer",      "https://tidesandcurrents.noaa.gov/");
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        // allocate on heap
        DynamicJsonDocument doc(8192);
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (!err) {
          JsonArray preds = doc["predictions"].as<JsonArray>();
          for (size_t i = 0; i < preds.size() && i < 48; i++) {
            nextTideHeight[i] = preds[i]["v"].as<float>();
          }
          hourlyOk = true;
        } else {
          Serial.print("JSON parse error: "); Serial.println(err.c_str());
        }
      } else {
        Serial.printf("HTTP GET failed: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    }
  
    // 4) fetch today's high/low
    String hiloURL = baseURL + "&date=today&interval=hilo";
    Serial.println("Polling today's high/low tide data…");
    Serial.println(hiloURL);
  
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(hiloURL)) {
      Serial.println("Tide high/low begin failed, using cached values.");
    } else {
      http.setUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/115.0.0.0 Safari/537.36"
      );
      http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" );
      http.addHeader("Accept-Language", "en-US,en;q=0.9");
      http.addHeader("Referer",      "https://tidesandcurrents.noaa.gov/");
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        DynamicJsonDocument doc2(2048);
        DeserializationError err2 = deserializeJson(doc2, http.getStream());
        if (!err2) {
          JsonArray hlo = doc2["predictions"].as<JsonArray>();
          String text = "";
          for (JsonObject p : hlo) {
            const char* type = p["type"];
            const char* tFull = p["t"];
            if (!type || !tFull || strlen(tFull) < 16) {
              continue;
            }
            char tstr[6];
            // grab "HH:MM"
            memcpy(tstr, tFull + 11, 5);
            tstr[5] = '\0';
            float v = p["v"].as<float>();
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %s %.1f ft     ", type, tstr, v);
            text += buf;
          }
          if (text.length() > 0) {
            text.toCharArray(nextTextTides, sizeof(nextTextTides));
            highLowOk = true;
          } else {
            Serial.println("High/low JSON had no usable rows, using cached values.");
          }
        } else {
          Serial.print("JSON parse error: "); Serial.println(err2.c_str());
        }
      } else {
        Serial.printf("HTTP GET failed: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    }
  
    // 5) combine results
    dailyPollSuccess = (hourlyOk && highLowOk);
    if (dailyPollSuccess) {
      memcpy(tideHeight, nextTideHeight, sizeof(nextTideHeight));
      snprintf(textTides, sizeof(textTides), "%s", nextTextTides);
      dailyPollFailed = 0;
      Serial.println("Daily poll succeeded. 48h data:");
      for (int i = 0; i < 48; i++) {
        Serial.printf("Hour %02d: %.2f ft\n", i, tideHeight[i]);
      }
    } else {
      Serial.println("Daily poll failed.");
      dailyPollFailed++;
    }
    #endif
  }

void processCurrentTide() {
  #if HAS_USWEATHER
  // If the daily poll previously failed, try to poll again (up to a limited number of retries).
  if (!dailyPollSuccess && dailyPollFailed < 3) {
    Serial.println("Daily poll failed last time. Retrying poll...");
    fetchTides();
  }
  
  // Get the current hour from the RTC.
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);
  int startIndex = timeinfo->tm_hour;
  // if we’re not exactly on the hour, show the *next* hour block
  if (timeinfo->tm_min > 0 || timeinfo->tm_sec > 0) {
    startIndex++;
  }
  // clamp so we don’t overrun the 48-hour buffer
  if (startIndex > 48 - 14) {
    startIndex = 48 - 14;
  }

  // find local min/max and their indices as before
  float localMin = tideHeight[startIndex], localMax = tideHeight[startIndex];
  int localMinIndex = startIndex, localMaxIndex = startIndex;
  for (int i = startIndex; i < startIndex + 14; i++) {
    if (tideHeight[i] < localMin) { localMin = tideHeight[i]; localMinIndex = i; }
    if (tideHeight[i] > localMax) { localMax = tideHeight[i]; localMaxIndex = i; }
  }
  bool rising = (localMinIndex < localMaxIndex);

  for (int i = 0; i < 14; i++) {
    int idx = startIndex + i;
    float range = (localMax != localMin) ? (localMax - localMin) : 1;
    float norm  = (tideHeight[idx] - localMin) / range;

    // simple gradient: 0=blue, 1=red
    uint8_t red  = norm * 255;
    uint8_t blue = (1.0 - norm) * 255;

    // brightness floor (min→52, max→255)
    uint8_t rawB = ((idx == localMinIndex) || (idx == localMaxIndex)) ? 255 : 20;
    uint8_t bright = map(rawB, 0, 255, 52, 255);

    // apply brightness
    red  = (red  * bright) / 255;
    blue = (blue * bright) / 255;

    tideColor[i] = (red << 16) | blue;
  }

 

  // --- Adjust the order of the LED array to match your wiring ---
  uint32_t tempHolder[14];
  #if TEST_CLOCK
    static const uint8_t idxMap[14] = {
      0, 1, 2, 3, 4, 5, 6,   // new[0..6]
      7, 8, 9, 10, 11, 12, 13    // new[7..13]
    };
  #else
    static const uint8_t idxMap[14] = {
      7,8,9,10,11,12,13,   // new[0..6] for real clock if you start at bottom right and go left and up then right clockwise around the clock
      6, 5, 4, 3, 2,1,0    // new[7..13] modifiy to fit however you've wired the shelflights
    };
  #endif
  for (int i = 0; i < 14; i++) {
      tempHolder[i] = tideColor[idxMap[i]];
  }
  memcpy(tideColor, tempHolder, sizeof(tempHolder));
  

  // Debug: Print the 14-hour LED mapping.
  Serial.println("\n14-Hour LED Display Mapping:");
  for (int i = 0; i < 14; i++) {
    int hourIdx = startIndex + i;
    Serial.printf("LED %02d (Hour %02d): Tide: %.2f ft -> Color: #%06X\n",
                  i, hourIdx, tideHeight[hourIdx], tideColor[i]);
  }
#endif
}


void displayNumber(uint16_t number, byte segment, CRGB color) {   //main digit rendering (except when scrolling)
  // segments from left to right: 6, 5, 4, 3, 2, 1, 0
  uint16_t startindex = 0;
 //   Serial.println(startindex);
  switch (segment) {
    case 0:
      startindex = 0;
      break;
    case 1:
      startindex = (LEDS_PER_DIGIT * 1);
      break;
    case 2:
      startindex = (LEDS_PER_DIGIT * 2);
      break;
    case 3:
      startindex = (LEDS_PER_DIGIT * 3);
      break;    
    case 4:
      startindex = (LEDS_PER_DIGIT * 4);
      break;    
    case 5:
      startindex = (LEDS_PER_DIGIT * 5);
      break;    
    case 6:
      startindex = (LEDS_PER_DIGIT * 6);
      break;    
  }

  for (byte i=0; i<SEGMENTS_PER_NUMBER; i++){                // 7 segments
    if (fakeclockrunning == 0 && (((ClockColorSettings == 4 && clockMode == 0) || (DateColorSettings == 4 && clockMode == 7) || (tempColorSettings == 4 && clockMode == 2) || (humiColorSettings == 4 && clockMode == 8)) && pastelColors == 0))  { color = CHSV(random(0, 255), 255, 255);}
    if (fakeclockrunning == 0 && (((ClockColorSettings == 4 && clockMode == 0) || (DateColorSettings == 4 && clockMode == 7) || (tempColorSettings == 4 && clockMode == 2) || (humiColorSettings == 4 && clockMode == 8)) && pastelColors == 1)) { color = CRGB(random(0, 255), random(0, 255), random(0, 255));}
    for (byte j=0; j<LEDS_PER_SEGMENT; j++ ){              // 7 LEDs per segment
      yield();
      LEDs[FAKE_LEDs[i * LEDS_PER_SEGMENT + j + startindex]] = ((numbers[number] & 1 << i) == 1 << i) ? color : alternateColor;
    }
  }
} //end of displayNumber

void allTest() {   //tests all non-shelf LEDs to black
  //  Serial.println("alltest function");
  for (int i=0; i<SEGMENTS_LEDS + 14; i++) {
    LEDs[i] = CRGB::Red;
    FastLED.show();
  }
  delay(1000);
  
  for (int i=0; i<SEGMENTS_LEDS + 14; i++) {
    LEDs[i] = CRGB::Green;
    FastLED.show();
  }
  delay(1000);

  for (int i=0; i<SEGMENTS_LEDS + 14; i++) {
    LEDs[i] = CRGB::Blue;
    FastLED.show();
  }
  delay(1000);

  for (int i=0; i<SEGMENTS_LEDS + 14; i++) {
    LEDs[i] = CRGB::White;
    FastLED.show();
    delay(100);
  }
  delay(1000);

  for (int i=0; i<SEGMENTS_LEDS + 14; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
}

void allBlank() {   //clears all non-shelf LEDs to black
  //  Serial.println("allblank function");
  for (int i=0; i<SEGMENTS_LEDS; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
  GetBrightnessLevel();
}  // end of all-blank

void shelfBlank() {   //clears all shelf LEDs to black
  for (int i=SEGMENTS_LEDS; i<NUM_LEDS; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
}  // end of shelf-blank


void fakeClock(int loopy) {  //flashes 12:00 like all old clocks did
  fakeclockrunning = 1;
  for (int i=0; i<loopy; i++) {
      for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Red;}  // 1x:xx
      displayNumber(2,5,CRGB::Red);  // x2:xx
      for (int i=(25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1); i<(25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1); i++) { LEDs[i] = CRGB::Black; }  // xx:xx
      for (int i=(20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1); i<(20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1); i++) { LEDs[i] = CRGB::Black; } // xx:xx
      displayNumber(0,2,CRGB::Red); // xx:0x
      displayNumber(0,0,CRGB::Red);  // xx:x0
      FastLED.show();
      delay(500);
      for (int i=(32*LEDS_PER_SEGMENT); i<(34*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black;}
      displayNumber(2,5,CRGB::Black);
      for (int i=(25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1); i<(25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1); i++) { LEDs[i] = CRGB::Red; }
      for (int i=(20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1); i<(20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1); i++) { LEDs[i] = CRGB::Red; }
      displayNumber(0,2,CRGB::Black);
      displayNumber(0,0,CRGB::Black); 
      FastLED.show();
      delay(500);
   }
  for (int i=(25*LEDS_PER_SEGMENT); i<(26*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black; }
  for (int i=(20*LEDS_PER_SEGMENT); i<(21*LEDS_PER_SEGMENT); i++) { LEDs[i] = CRGB::Black; }
  fakeclockrunning = 0;  // so the digit render knows not to apply the rainbow colors
}  //end of fakeClock




void ShelfDownLights() {  //turns on the drop lights on the underside of each shelf
int currentHour  = timeinfo.tm_hour;
 //   Serial.println("ShelfDownLights function");
 if ((suspendType != 2 || isAsleep == 0) && useSpotlights == 1) {  //not sleeping? suposed to be running?
  unsigned long currentMillis = millis();  
  if (currentMillis - prevTime2 >= 250) {  //run everything inside here every quarter second
    #if HAS_USWEATHER
    int forecastBrightness[14];
    uint8_t forecastType[14];
    if (spotlightsColorSettings == 5) {
      portENTER_CRITICAL(&rainForecastMux);
      memcpy(forecastBrightness, rainForecast, sizeof(forecastBrightness));
      memcpy(forecastType, precipitationType, sizeof(forecastType));
      portEXIT_CRITICAL(&rainForecastMux);
    }
    #endif
    for (int i=SEGMENTS_LEDS; i<NUM_LEDS; i++) {
        if (spotlightsColorSettings == 0){ spotlightsColor = CRGB(spotlightsColorValue);  LEDs[i] = spotlightsColor;}
        if ((spotlightsColorSettings == 1 && pastelColors == 0)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { spotlightsColor = CHSV(random(0, 255), 255, 255);  LEDs[i] = spotlightsColor;}
        if ((spotlightsColorSettings == 1 && pastelColors == 1)  && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { spotlightsColor = CRGB(random(0, 255), random(0, 255), random(0, 255));  LEDs[i] = spotlightsColor;}
        if (spotlightsColorSettings == 2 ){ LEDs[i] = colorWheel2(((i-SEGMENTS_LEDS)  * 18 + colorWheelPositionTwo) % 256); }
        if (spotlightsColorSettings == 3 ){ LEDs[i] = colorWheel2(((255) + colorWheelPositionTwo) % 256); }
        if (spotlightsColorSettings == 4 ){
          int seed = random(2500);         // A random number. Higher number => fewer twinkles. Use random16() for values >255.
          if (seed < 30) {
            CRGB color = CRGB::Black;
            if (pastelColors == 0){ color = CHSV(random(0, 255), 255, 255); }
            if (pastelColors == 1){ color = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
                  {              
                    LEDs[SEGMENTS_LEDS+random((NUM_LEDS-SEGMENTS_LEDS))] = color;
                  }
            }
            for (int j=SEGMENTS_LEDS; j<NUM_LEDS; j++) {
              LEDs[j].fadeToBlackBy(1);
              }
         }
        #if HAS_USWEATHER
          if (spotlightsColorSettings == 5) {
            const int day = i - SEGMENTS_LEDS;
            const uint8_t brightness = forecastBrightness[day];
            switch (forecastType[day]) {
              case PRECIP_SNOW: LEDs[i] = CRGB(brightness, brightness, brightness); break;
              case PRECIP_MIXED: LEDs[i] = CRGB(brightness / 2, 0, brightness); break;
              default: LEDs[i] = CRGB(0, 0, brightness); break;
            }
          }
          if (spotlightsColorSettings == 6 ){ LEDs[i] = tideColor[(i-SEGMENTS_LEDS)]; } // Use tideColor for the current hour and next 13 hours
          #endif
    }
    colorWheelPositionTwo = colorWheelPositionTwo - 1; // SPEED OF 2nd COLOR WHEEL
    if (colorWheelPositionTwo < 0) {colorWheelPositionTwo = 255;} // RESET 2nd COLOR WHEEL 
    prevTime2 = currentMillis;
    FastLED.show();
  }
 } else if (useSpotlights == 0) {  //or turn them all off
  for (int i=SEGMENTS_LEDS; i<NUM_LEDS; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
 }
 
}//end of ShelfDownLights 




void BlinkDots() {  //displays the dots in the middle of the time (colon, period, bar)
  if (ClockColorSettings == 0 || ClockColorSettings == 1) {colonColor = CRGB(colonColorValue);}
    if (((ClockColorSettings == 2) || (ClockColorSettings == 4)) && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255));}
    if (ClockColorSettings == 3 && ( (ColorChangeFrequency == 0 ) || (ColorChangeFrequency == 1 && randomMinPassed == 1) || (ColorChangeFrequency == 2 && randomHourPassed == 1) || (ColorChangeFrequency == 3 && randomDayPassed == 1) || (ColorChangeFrequency == 4 && randomWeekPassed == 1) || (ColorChangeFrequency == 5 && randomMonthPassed == 1) )) { colonColor = hourColor;}
    if (dotsOn) {

    if (colonType == 0) {   //small colon
      if (clockDisplayType == 1 || clockDisplayType == 2) {     //for 12 or 24-hour Military Time
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=17*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2; i<17*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1; i++) { LEDs[i] = colonColor;}
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=19*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2; i<19*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1; i++) { LEDs[i] = colonColor;}        
      }
      else {       //for non 12 or 24-hour Military Time
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1; i<25*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1; i++) { LEDs[i] = colonColor;}
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2-1; i<20*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1; i++) { LEDs[i] = colonColor;}
      }
    }
    if (colonType == 1) {  //big bar
      if (clockDisplayType == 3) {     //anything but 12 or 24-hour Military Time
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=25*LEDS_PER_SEGMENT; i<26*LEDS_PER_SEGMENT; i++) { LEDs[i] = colonColor;}
        if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
        if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
        for (int i=20*LEDS_PER_SEGMENT; i<21*LEDS_PER_SEGMENT; i++) { LEDs[i] = colonColor;}
      }
    }
    if (colonType == 2) {  //small period
      if (ClockColorSettings == 4 && pastelColors == 0){ colonColor = CHSV(random(0, 255), 255, 255); }
      if (ClockColorSettings == 4 && pastelColors == 1){ colonColor = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
      if (clockDisplayType == 1 || clockDisplayType == 2) {     //for 12 or 24-hour Military Time
        for (int i=17*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2; i<17*LEDS_PER_SEGMENT+LEDS_PER_SEGMENT/2+1; i++) { LEDs[i] = colonColor;}
      }
      else {   //big period for non 12 or 24-hour Military Time
	      for (int i=20*LEDS_PER_SEGMENT; i<21*LEDS_PER_SEGMENT; i++) { LEDs[i] = colonColor;}
      }
    }


  } else {  //hide all dots, blinking
    if (clockDisplayType == 1 || clockDisplayType == 2) {     //12 or 24-hour Military Time
      for (int i=17*LEDS_PER_SEGMENT; i<18*LEDS_PER_SEGMENT; i++) { LEDs[i] = CRGB::Black;}
      for (int i=19*LEDS_PER_SEGMENT; i<20*LEDS_PER_SEGMENT; i++) { LEDs[i] = CRGB::Black;}
    }
    else {
	    for (int i=25*LEDS_PER_SEGMENT; i<26*LEDS_PER_SEGMENT; i++) { LEDs[i] = CRGB::Black;}
	    for (int i=20*LEDS_PER_SEGMENT; i<21*LEDS_PER_SEGMENT; i++) { LEDs[i] = CRGB::Black;}
    }

    #if HAS_PHOTOSENSOR
    getsamplePhoto();
  #endif


  }
  dotsOn = !dotsOn;  
}//end of shelf and gaps

/* void happyNewYear() {  
  CRGB color = CRGB::Red; 
  allBlank();
  breakOutSet = 0;
  int year = (timeinfo.tm_year +1900)-2000;
  byte y1 = year / 10;
  byte y2 = year % 10;
//  #if HAS_BUZZER
 // rtttl::begin(BUZZER_PIN, sounds.getSongByName("auldlang"));
//  while( !rtttl::done() && !breakOutSet )
 // {
 //   rtttl::play();
 //   color = colorWheel((205 / 50 + colorWheelPosition) % 256);
////    displayNumber(2,6,color);  //2
//    displayNumber(0,4,color);  //0
//    displayNumber(y1,2,color);  //Y
////    displayNumber(y2,0,color);  //Y
//    FastLED.show();
//    colorWheelPosition++;
//    server.handleClient();   
//  }
//  #else
    color = colorWheel((205 / 50 + colorWheelPosition) % 256);
    displayNumber(2,6,color);  //2
    displayNumber(0,4,color);  //0
    displayNumber(y1,2,color);  //Y
    displayNumber(y2,0,color);  //Y
    FastLED.show();
    colorWheelPosition++;
    server.handleClient();  
 // #endif
  if (!breakOutSet) {scroll("HAPPy nEW yEAr");}
  allBlank();
}
*/
static int chaseLedIndex(int mapping, int mappedIndex) {
  switch (mapping) {
    case 0: return FAKE_LEDs_C_BLTR[mappedIndex];
    case 1: return FAKE_LEDs_C_BRTL[mappedIndex];
    case 2: return FAKE_LEDs_C_BMUP[mappedIndex];
    case 3: return FAKE_LEDs_C_TMDN[mappedIndex];
    case 4: return FAKE_LEDs_C_CMOT[mappedIndex];
    case 5: return FAKE_LEDs_C_TLBR[mappedIndex];
    case 6: return FAKE_LEDs_C_CSIN[mappedIndex];
    case 7: return FAKE_LEDs_C_TRBL[mappedIndex];
    case 8: return FAKE_LEDs_C_OUTS[mappedIndex];
    case 9: return FAKE_LEDs_C_VERT[mappedIndex];
    case 10: return FAKE_LEDs_C_OUTS2[mappedIndex];
    case 11: return FAKE_LEDs_C_VERT2[mappedIndex];
    default: return FAKE_LEDs_C_BLTR[mappedIndex];
  }
}

static void beginChaseCycle() {
  chaseState.active = true;
  chaseState.forward = true;
  chaseState.position = 0;
  chaseState.mapping = random(0, 7);
  if (pastelColors == 0) {
    lightchaseColorOne = CHSV(random(0, 255), 255, 255);
    lightchaseColorTwo = CHSV(random(0, 255), 255, 255);
  }
  if (pastelColors == 1) {
    lightchaseColorOne = CRGB(random(0, 255), random(0, 255), random(0, 255));
    lightchaseColorTwo = CRGB(random(0, 255), random(0, 255), random(0, 255));
  }
}

void Chase() {   //lightshow chase mode
  if (!chaseState.active) {
    beginChaseCycle();
  }

  const int fake = chaseState.position * LEDS_PER_SEGMENT;
  const CRGB color = chaseState.forward ? lightchaseColorOne : lightchaseColorTwo;
  for (byte s = 0; s < LEDS_PER_SEGMENT; s++) {
    LEDs[chaseLedIndex(chaseState.mapping, s + fake)] = color;
  }
  FastLED.show();

  if (chaseState.forward) {
    if (chaseState.position >= SPECTRUM_PIXELS - 1) {
      chaseState.forward = false;
    } else {
      chaseState.position++;
    }
  } else if (chaseState.position <= 0) {
    chaseState.active = false;
  } else {
    chaseState.position--;
  }
} //end of chase

static void setDigitSegment(byte digitColumn, byte segmentIndex, const CRGB &color) {
  const int startIndex = digitColumn * LEDS_PER_DIGIT + segmentIndex * LEDS_PER_SEGMENT;
  for (byte led = 0; led < LEDS_PER_SEGMENT; led++) {
    LEDs[FAKE_LEDs[startIndex + led]] = color;
  }
}

static void clearAnimatedDigits() {
  static const byte digitColumns[] = {6, 4, 2, 0};
  for (byte digit = 0; digit < sizeof(digitColumns); digit++) {
    for (byte segment = 0; segment < SEGMENTS_PER_NUMBER; segment++) {
      setDigitSegment(digitColumns[digit], segment, CRGB::Black);
    }
  }
}

static CRGB segmentPathColor(uint32_t now) {
  static const CRGB palette[] = {
      CRGB::Red, CRGB::Orange, CRGB::Yellow, CRGB::Green,
      CRGB::Cyan, CRGB::Blue, CRGB::Purple};
  static const uint32_t transitionMs = 10000;
  const uint32_t elapsed = now - segmentPathState.startedAt;
  const byte colorIndex = (elapsed / transitionMs) % (sizeof(palette) / sizeof(palette[0]));
  const byte nextColorIndex = (colorIndex + 1) % (sizeof(palette) / sizeof(palette[0]));
  const uint8_t blendAmount = static_cast<uint8_t>(((elapsed % transitionMs) * 255UL) / transitionMs);
  return blend(palette[colorIndex], palette[nextColorIndex], blendAmount);
}

static void beginSegmentPath(uint32_t now) {
  segmentPathState.active = true;
  segmentPathState.startedAt = now;
  segmentPathState.lastFrameAt = now - 180;
  segmentPathState.frame = 0;
  allBlank();
}

static void renderSegmentPath(const byte *path, byte pathLength, bool pulseCenter) {
  static const byte digitColumns[] = {6, 4, 2, 0};
  static const uint16_t frameIntervalMs = 180;
  const uint32_t now = millis();
  if (!segmentPathState.active) {
    beginSegmentPath(now);
  }
  if (static_cast<uint32_t>(now - segmentPathState.lastFrameAt) < frameIntervalMs) {
    return;
  }
  segmentPathState.lastFrameAt = now;

  clearAnimatedDigits();
  const CRGB color = segmentPathColor(now);
  for (byte digit = 0; digit < sizeof(digitColumns); digit++) {
    const byte pathIndex = (segmentPathState.frame + digit) % pathLength;
    setDigitSegment(digitColumns[digit], path[pathIndex], color);
  }

  if (pulseCenter) {
    const byte pulsePhase = segmentPathState.frame % 18;
    uint8_t pulseBrightness = 0;
    if (pulsePhase == 0 || pulsePhase == 2) {
      pulseBrightness = 96;
    } else if (pulsePhase == 1) {
      pulseBrightness = 255;
    }
    if (pulseBrightness > 0) {
      CRGB pulseColor = color;
      pulseColor.nscale8_video(pulseBrightness);
      for (byte digit = 0; digit < sizeof(digitColumns); digit++) {
        setDigitSegment(digitColumns[digit], 6, pulseColor);
      }
    }
  }

  FastLED.show();
  segmentPathState.frame++;
}

void Orbit() {
  // Top, upper-right, lower-right, bottom, lower-left, upper-left.
  static const byte path[] = {4, 5, 0, 1, 2, 3};
  renderSegmentPath(path, sizeof(path), false);
}

void FigureEight() {
  // Trace the upper and lower loops, crossing through the center segment.
  static const byte path[] = {4, 5, 6, 2, 1, 0, 6, 3};
  renderSegmentPath(path, sizeof(path), false);
}


 
void Twinkles() {
#if HAS_SOUNDDETECTOR
  int audio_input = i2sWaveformRead();
  int Level = map(audio_input, 0, 1023, 50, 210);
  if (audio_input < 100){  Level = 50;  }
  if (audio_input > 1023){  Level = 210;  }
#else
  int Level = 100;  //set to default if no audio board
#endif
  int seed = random(Level);         // A random number. Higher number => fewer twinkles. Use random16() for values >255.
  int i = random(SPECTRUM_PIXELS);         // A random number. Higher number => fewer twinkles. Use random16() for values >255.
  if (seed > 46) {
    CRGB color = CRGB::Black;
    if (pastelColors == 0){ color = CHSV(random(0, 255), 255, 255); }
    if (pastelColors == 1){ color = CRGB(random(0, 255), random(0, 255), random(0, 255)); }
          int fake =  i * LEDS_PER_SEGMENT;
          for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
            LEDs[FAKE_LEDs_C_BLTR[s+((fake))]] = color;
          }
  }
  
  for (int j = 0; j < SPECTRUM_PIXELS; j++) {
          int fake =  j * LEDS_PER_SEGMENT;
          for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
            LEDs[FAKE_LEDs_C_BLTR[s+((fake))]].fadeToBlackBy(20);
          }
  }
  if (clockMode != 5) { allBlank(); }
} // twinkles



void Rainbow() {
  //fill_gradient_RGB(LEDs, NUM_LEDS, CRGB::Red, CRGB::Yellow, CRGB::Green, CRGB::Blue);
    for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
      int fake =  j * LEDS_PER_SEGMENT;
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        LEDs[FAKE_LEDs_C_BMUP[s+((fake))]] = colorWheel(((255) + colorWheelPosition) % 256);
      }
    }
  EVERY_N_MILLISECONDS(150) {
    colorWheelPosition = colorWheelPosition - colorWheelSpeed; // SPEED OF COLOR WHEEL
  if (colorWheelPosition < 0) // RESET COLOR WHEEL
    colorWheelPosition = 255;
  }
  if (clockMode != 5) { allBlank(); }
} //end of Rainbow



void GreenMatrix() {
  EVERY_N_MILLISECONDS(65) {
    updateMatrix();
    FastLED.show();
 }
  EVERY_N_MILLISECONDS(30) {
    changeMatrixpattern();
  }
} //loop

void changeMatrixpattern () {
  int rand1 = random16 (SPECTRUM_PIXELS);
  int rand2 = random16 (SPECTRUM_PIXELS);
  if ((greenMatrix[rand1] == 1) && (greenMatrix[rand2] == 0) )   //simple get two random dot 1 and 0 and swap it,
  {
    greenMatrix[rand1] = 0;  //this will not change total number of dots
    greenMatrix[rand2] = 1;
  }
  if (clockMode != 5) { allBlank(); }
} //changeMatrixpattern

void initGreenMatrix() {                               //init array of dots. run once
  for (int i = 0; i < SPECTRUM_PIXELS; i++) {
    if (random8(20) == 0) {
      greenMatrix[i] = 1;  //random8(20) number of dots. decrease for more dots
    }
    else {
      greenMatrix[i] = 0;
    }
  }
} //initGreenMatrix

void updateMatrix() {
    for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
      byte layer = greenMatrix[((j + lightshowSpeed + random8(2) + SPECTRUM_PIXELS) % SPECTRUM_PIXELS)];   //fake scroll based on shift coordinate
      // random8(2) add glitchy look
      if (layer) {
          int fake =  j * LEDS_PER_SEGMENT;
          for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
            //LEDs[FAKE_LEDs_C_RAIN[s+((fake))]] = CHSV(110, 255, 255);
            LEDs[FAKE_LEDs_C_RAIN[s+((fake))]] = CRGB::Green;
          }
      }
    }
  lightshowSpeed ++;
    for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
      int fake =  j * LEDS_PER_SEGMENT;
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        LEDs[FAKE_LEDs_C_RAIN[s+((fake))]].fadeToBlackBy( 60 );
      }
    }
} //updateMatrix



void blueRain() {
#if HAS_SOUNDDETECTOR
    int audio_input = i2sWaveformRead(); 
  int Level = map(audio_input, 0, 1023, 0, 400);
  if (audio_input < 100){  Level = 0;  }
  if (audio_input > 1023){  Level = 400;  }
#else
  int Level = 400;  //set to default if no sound board
#endif
  //  Serial.println(Level);
    int delayRain = 400-Level;
  EVERY_N_MILLIS_I( thistimer, 400 ) { // initial period = 100ms
thistimer.setPeriod(delayRain);
    updaterain();
 //   FastLED.show();
  }
  if (clockMode != 5) { allBlank(); }
} //loop

void raininit() {                               //init array of dots. run once
  for (int i = 0; i < SPECTRUM_PIXELS; i++) {
    if (random8(24) == 0) {  //30?
      rain[i] = 1;  //random8(20) number of dots. decrease for more dots
    }
    else {
      rain[i] = 0;
    }
  }
} //raininit

void updaterain() {
    for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
      byte layer = rain[((j + lightshowSpeed + 2 + SPECTRUM_PIXELS) % SPECTRUM_PIXELS)];   //fake scroll based on shift coordinate
      // random8(2) add glitchy look
      if (layer) {
          int fake =  j * LEDS_PER_SEGMENT;
          for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
            //LEDs[FAKE_LEDs_C_RAIN[s+((fake))]] = CHSV(110, 255, 255);
            LEDs[FAKE_LEDs_C_RAIN[s+((fake))]] = CRGB::Blue;
          }
      }
    }
  lightshowSpeed ++;
  for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
      int fake =  j * LEDS_PER_SEGMENT;
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        LEDs[FAKE_LEDs_C_RAIN[s+((fake))]].fadeToBlackBy( 128 );
      }
  }

}

void Fire2021() {
// Array of temperature readings at each simulation cell
  static byte heat[SPECTRUM_PIXELS];

  // Step 1.  Cool down every cell a little
    for( int i = 0; i < SPECTRUM_PIXELS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, 30));  //55-cooling
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= SPECTRUM_PIXELS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' of heat to the bottom center 3
    if( random8() < 128 ) {  //128-sparkling
      int y = random8(0,2);
      heat[y] = qadd8( heat[y], random8(155,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < SPECTRUM_PIXELS; j++) {
      CRGB color = HeatColor( heat[j]);
      int fake =  j * LEDS_PER_SEGMENT;
      for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
        LEDs[FAKE_LEDs_C_FIRE[s+((fake))]] = color;
      }

    }
  if (clockMode != 5) { allBlank(); }
}



void Snake() {  //real random snake mode with random food changing its color
  int move = 0;
  int fadeby = 130;
  int pickOne = random(4);
  //Serial.print("Snake goes ");
  //Serial.println(pickOne);
  //just write out every possible move, that's all, oh and make it random
  if (snakePosition == 0 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 4; move = 1;} else {snakePosition = 5; move = 1;}}
  if (snakePosition == 0 && snakeLastDirection == 2 && move == 0)  {snakeLastDirection = 3; snakePosition = 1; move = 1;}
  if (snakePosition == 1 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 3; move = 1;} else {snakePosition = 2; move = 1;}}
  if (snakePosition == 1 && snakeLastDirection == 1 && move == 0)  {snakeLastDirection = 0; snakePosition = 0; move = 1;}
  if (snakePosition == 2 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 9; move = 1;} else {snakePosition = 10; move = 1;}}
  if (snakePosition == 2 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 3; move = 1;} else {snakePosition = 1; move = 1;}}
  if (snakePosition == 3 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 4; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 8; move = 1;} else {snakePosition = 7; move = 1;}}
  if (snakePosition == 3 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 1; move = 1;} else {snakeLastDirection = 3; snakePosition = 2; move = 1;}}
  if (snakePosition == 4 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 0; move = 1;} else {snakeLastDirection = 0; snakePosition = 5; move = 1;}}
  if (snakePosition == 4 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 7; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 3; move = 1;} else {snakePosition = 8; move = 1;}}
  if (snakePosition == 5 && snakeLastDirection == 0 && move == 0)  {snakeLastDirection = 3; snakePosition = 6; move = 1;}
  if (snakePosition == 5 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 3; snakePosition = 4; move = 1;} else {snakeLastDirection = 2; snakePosition = 0; move = 1;}}
  if (snakePosition == 6 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 7; move = 1;} else {snakePosition = 15; move = 1;}}
  if (snakePosition == 6 && snakeLastDirection == 1 && move == 0)  {snakeLastDirection = 2; snakePosition = 5; move = 1;}
  if (snakePosition == 7 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 6; move = 1;} else {snakeLastDirection = 3; snakePosition = 15; move = 1;}} 
  if (snakePosition == 7 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 4; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 8; move = 1;} else {snakePosition = 3; move = 1;}}
  if (snakePosition == 8 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 3; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 7; move = 1;} else {snakePosition = 4; move = 1;}}
  if (snakePosition == 8 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 14; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 9; move = 1;} else {snakePosition = 13; move = 1;}}
  if (snakePosition == 9 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 8; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 13; move = 1;} else {snakePosition = 14; move = 1;}}
  if (snakePosition == 9 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 2; move = 1;} else {snakeLastDirection = 3; snakePosition = 10; move = 1;}}
  if (snakePosition == 10 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 12; move = 1;} else {snakePosition = 11; move = 1;}}
  if (snakePosition == 10 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 9; move = 1;} else {snakePosition = 2; move = 1;}}
  if (snakePosition == 11 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 19; move = 1;} else {snakePosition = 20; move = 1;}}
  if (snakePosition == 11 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 12; move = 1;} else {snakePosition = 10; move = 1;}}
  if (snakePosition == 12 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 13; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 18; move = 1;} else {snakePosition = 17; move = 1;}}
  if (snakePosition == 12 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 10; move = 1;} else {snakeLastDirection = 3; snakePosition = 11; move = 1;}}
  if (snakePosition == 13 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 9; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 14; move = 1;} else {snakePosition = 8; move = 1;}}
  if (snakePosition == 13 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 17; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 12; move = 1;} else {snakePosition = 18; move = 1;}}
  if (snakePosition == 14 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 15; move = 1;} else {snakeLastDirection = 3; snakePosition = 16; move = 1;}} 
  if (snakePosition == 14 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 8; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 13; move = 1;} else {snakePosition = 9; move = 1;}}
  if (snakePosition == 15 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 14; move = 1;} else {snakePosition = 16; move = 1;}}
  if (snakePosition == 15 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 7; move = 1;} else {snakeLastDirection = 1; snakePosition = 6; move = 1;}}
  if (snakePosition == 16 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 17; move = 1;} else {snakePosition = 25; move = 1;}}
  if (snakePosition == 16 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 14; move = 1;} else {snakeLastDirection = 1; snakePosition = 15; move = 1;}}
  if (snakePosition == 17 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 16; move = 1;} else {snakeLastDirection = 3; snakePosition = 25; move = 1;}} 
  if (snakePosition == 17 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 13; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 18; move = 1;} else {snakePosition = 12; move = 1;}}
  if (snakePosition == 18 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 12; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 17; move = 1;} else {snakePosition = 13; move = 1;}}
  if (snakePosition == 18 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 24; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 19; move = 1;} else {snakePosition = 23; move = 1;}}
  if (snakePosition == 19 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 18; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 23; move = 1;} else {snakePosition = 24; move = 1;}}
  if (snakePosition == 19 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 11; move = 1;} else {snakeLastDirection = 3; snakePosition = 20; move = 1;}}
  if (snakePosition == 20 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 22; move = 1;} else {snakePosition = 21; move = 1;}}
  if (snakePosition == 20 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 19; move = 1;} else {snakePosition = 11; move = 1;}}
  if (snakePosition == 21 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 29; move = 1;} else {snakePosition = 30; move = 1;}}
  if (snakePosition == 21 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 22; move = 1;} else {snakePosition = 20; move = 1;}}
  if (snakePosition == 22 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 23; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 28; move = 1;} else {snakePosition = 27; move = 1;}}
  if (snakePosition == 22 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 20; move = 1;} else {snakeLastDirection = 3; snakePosition = 21; move = 1;}}
  if (snakePosition == 23 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 19; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 24; move = 1;} else {snakePosition = 18; move = 1;}}
  if (snakePosition == 23 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 27; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 22; move = 1;} else {snakePosition = 28; move = 1;}}
  if (snakePosition == 24 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 25; move = 1;} else {snakeLastDirection = 3; snakePosition = 26; move = 1;}} 
  if (snakePosition == 24 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 18; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 23; move = 1;} else {snakePosition = 19; move = 1;}}
  if (snakePosition == 25 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 24; move = 1;} else {snakePosition = 26; move = 1;}}
  if (snakePosition == 25 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 17; move = 1;} else {snakeLastDirection = 1; snakePosition = 16; move = 1;}}
  if (snakePosition == 26 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 27; move = 1;} else {snakePosition = 34; move = 1;}}
  if (snakePosition == 26 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 24; move = 1;} else {snakeLastDirection = 1; snakePosition = 25; move = 1;}}
  if (snakePosition == 27 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 26; move = 1;} else {snakeLastDirection = 3; snakePosition = 34; move = 1;}} 
  if (snakePosition == 27 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 23; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 28; move = 1;} else {snakePosition = 22; move = 1;}}
  if (snakePosition == 28 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 22; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 27; move = 1;} else {snakePosition = 23; move = 1;}}
  if (snakePosition == 28 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 33; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 29; move = 1;} else {snakePosition = 32; move = 1;}}
  if (snakePosition == 29 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 28; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 32; move = 1;} else {snakePosition = 33; move = 1;}}
  if (snakePosition == 29 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 21; move = 1;} else {snakeLastDirection = 3; snakePosition = 30; move = 1;}}
  if (snakePosition == 30 && snakeLastDirection == 3 && move == 0)  {snakeLastDirection = 0; snakePosition = 31; move = 1;}
  if (snakePosition == 30 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 0) {snakeLastDirection = 0; snakePosition = 29; move = 1;} else {snakePosition = 21; move = 1;}}
  if (snakePosition == 31 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 32; move = 1;} else {snakePosition = 36; move = 1;}}
  if (snakePosition == 31 && snakeLastDirection == 2 && move == 0)  {snakeLastDirection = 1; snakePosition = 30; move = 1;}
  if (snakePosition == 32 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 29; move = 1;} else if (pickOne == 3) {snakeLastDirection = 0; snakePosition = 33; move = 1;} else {snakePosition = 28; move = 1;}}
  if (snakePosition == 32 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 0; snakePosition = 36; move = 1;} else if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 31; move = 1;}}
  if (snakePosition == 33 && snakeLastDirection == 0 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 34; move = 1;} else {snakeLastDirection = 3; snakePosition = 35; move = 1;}} 
  if (snakePosition == 33 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 28; move = 1;} else if (pickOne == 3) {snakeLastDirection = 3; snakePosition = 32; move = 1;} else {snakePosition = 29; move = 1;}}
  if (snakePosition == 34 && snakeLastDirection == 3 && move == 0)  {if (pickOne == 3) {snakeLastDirection = 2; snakePosition = 33; move = 1;} else {snakePosition = 35; move = 1;}}
  if (snakePosition == 34 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 27; move = 1;} else {snakeLastDirection = 1; snakePosition = 26; move = 1;}}
  if (snakePosition == 35 && snakeLastDirection == 3 && move == 0)  {snakeLastDirection = 2; snakePosition = 36; move = 1;}
  if (snakePosition == 35 && snakeLastDirection == 1 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 2; snakePosition = 33; move = 1;} else {snakeLastDirection = 1; snakePosition = 34; move = 1;}}
  if (snakePosition == 36 && snakeLastDirection == 0 && move == 0)  {snakeLastDirection = 1; snakePosition = 35; move = 1;}
  if (snakePosition == 36 && snakeLastDirection == 2 && move == 0)  {if (pickOne == 1) {snakeLastDirection = 1; snakePosition = 32; move = 1;} else {snakePosition = 31; move = 1;}}
  if (snakePosition == foodSpot) { oldsnakecolor = spotcolor;  snakeWaiting = 1; foodSpot = 40;}  //did snake find the food, change snake color
  if (snakeWaiting > 0) {snakeWaiting = snakeWaiting + 1;} //counting while waiting
#if HAS_SOUNDDETECTOR
  int audio_input = i2sWaveformRead(); 
  int Level = map(audio_input, 0, 1023, 1, 10);
  if (audio_input < 100){  Level = 1;  }
  if (audio_input > 1023){  Level = 10;  }
#else
  int Level = 10;
#endif

  if (snakeWaiting > random((30/Level),(600/Level))) {snakeWaiting = 0; foodSpot = random(SPECTRUM_PIXELS-1); spotcolor = CHSV(random(0, 255), 255, 255); if (getSlower > 3){  getSlower =  getSlower / 3;  }}  //waiting time is up, pick new spot, new color, reset speed
  if (getSlower > 1000){  getSlower = 1000;  }
  int fake =  foodSpot * LEDS_PER_SEGMENT;   //draw food
  for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
    if (foodSpot != 40)     {  LEDs[FAKE_LEDs_C_BRTL[s+((fake))]] = spotcolor; }  //draw food, but not while waiting
  }
  int fake2 =  snakePosition * LEDS_PER_SEGMENT;   //draw snake
  for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
    LEDs[FAKE_LEDs_C_BRTL[s+((fake2))]] = oldsnakecolor;
  } 
  for (byte j = 0; j < SPECTRUM_PIXELS; j++) {  //slowly erase snake
    int fake3 =  j * LEDS_PER_SEGMENT;
    for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
      LEDs[FAKE_LEDs_C_BRTL[s+((fake3))]].fadeToBlackBy( fadeby );
     }
  }
  getSlower = getSlower + 40;
  if (clockMode != 5) { allBlank(); }
} //snake

void Cylon() {
  int fake = 0;
  const uint8_t CYLON[12] = {4,8,13,18,23,28,32,28,23,18,13,8};
  if (cylonPosition >=12) { cylonPosition = 0;}
  fake = CYLON[cylonPosition] * LEDS_PER_SEGMENT;
  for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){LEDs[FAKE_LEDs_SNAKE[s+((fake))]] = CRGB::Red;}
  cylonPosition++;
  if (cylonPosition >=12) { cylonPosition = 0;}
  for (byte j = 0; j < SPECTRUM_PIXELS; j++) {
    int fake =  j * LEDS_PER_SEGMENT;
    for (byte s=0; s<LEDS_PER_SEGMENT; s++ ){              // 7 LEDs per segment
      LEDs[FAKE_LEDs_SNAKE[s+((fake))]].fadeToBlackBy( 120 );
    }
  }
  if (clockMode != 5) { allBlank(); }
} //Cylon


#if HAS_BUZZER
bool songPathLoaded(const char *songPath) {
  if (songPath == nullptr || songPath[0] == '\0') {
    return false;
  }

  int songCapacity = sizeof(filesArray) / sizeof(filesArray[0]);
  for (int i = 0; i < totalSongs && i < songCapacity; i++) {
    if (strcmp(filesArray[i], songPath) == 0) {
      return true;
    }
  }

  return false;
}

void getListOfSongs() {
  totalSongs = 0;
  listOfSong.clear();
  memset(songsArray, 0, sizeof(songsArray));
  memset(filesArray, 0, sizeof(filesArray));
  File dir = FileFS.open("/songs");    //open root of sound directory
  if (!dir || !dir.isDirectory()) {
    Serial.println("No songs directory!");
    return;
  }
  String allowedChars = "0123456789 _.-=#,:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqwerstuvwxyz\n\r";
  while (true) {
    platformServices().cooperateDuringSetup();
    File entry =  dir.openNextFile();
    if (! entry) { break; }
    if (!entry.isDirectory()) {   //make sure its a file and not a directory
      char processedText[255];
      int pathLength = snprintf(processedText, sizeof(processedText), "/songs/%s", entry.name());  //build full pathname
      entry.close();
      if (pathLength < 0 || pathLength >= (int)sizeof(processedText)) {
        Serial.println("Song path is too long!");
        continue;
      }
      File file = FileFS.open(processedText, "r");  //open each song
      if(!file){Serial.println("No Saved Data!"); continue;}
      String output;
      bool fileBad = false;
      while (file.available()) {     //read song file contents
        char intRead = file.read();
        if (allowedChars.indexOf(intRead) != -1){ output += intRead; } else { Serial.print(processedText); Serial.print(" contains non-RTTTL characters! "); Serial.println(intRead); fileBad = 1; break;}
        if (output.length() > 511) {Serial.print(processedText); Serial.println(" file is too big!"); fileBad = 1; break;}
      }
      file.close();
      if (!fileBad) {
            char *token;
            char SongName[512];
            const char *delimiter =":";
            if (output.length() > 511) {Serial.print(processedText); Serial.println(" file is too big!"); deleteFile(FileFS, processedText); continue;}
            output.toCharArray(SongName, sizeof(SongName));    //throw whole contents into variable
            token = strtok(SongName, delimiter);    //keep only the contents before first :
            if (token == NULL || strlen(token) == 0) {Serial.print(processedText); Serial.println(" Missing RTTTL song name!"); deleteFile(FileFS, processedText); continue;}
            if (strlen(token) > 32) {Serial.print(processedText); Serial.println(" Song name too long!"); deleteFile(FileFS, processedText); continue;}
            if (strlen(processedText) > 40) {Serial.print(processedText); Serial.println(" Filename too long!");Serial.print(strlen(processedText)); deleteFile(FileFS, processedText); continue;}
            File file = FileFS.open(processedText, "r");  //open each song
            if(!file){Serial.println("No Saved Data!"); continue;}
            char buffer[1024]; // Adjust the buffer size as per your requirement
            memset(buffer, 0, sizeof(buffer));
            size_t bytesRead = file.readBytes(buffer, sizeof(buffer) - 1);
            file.close();  // Close the file
            if (bytesRead == 0) {   // Check if any bytes were read
              Serial.print(processedText);
              Serial.println("Empty RTTTL file!");
              deleteFile(FileFS, processedText);
              continue;
            }
            if (validate_rtttl(buffer)) { // Validate the RTTTL contents
              Serial.print(processedText);
              Serial.println(" is valid!");
              if (totalSongs >= (int)(sizeof(songsArray) / sizeof(songsArray[0]))) {
                Serial.print(processedText);
                Serial.println(" song list full; skipping");
                continue;
              }
              strncpy(songsArray[totalSongs], token, sizeof(songsArray[totalSongs]) - 1);         //throw song title from file into array
              songsArray[totalSongs][sizeof(songsArray[totalSongs]) - 1] = '\0';
              strncpy(filesArray[totalSongs], processedText, sizeof(filesArray[totalSongs]) - 1);  //throw full path and name into array
              filesArray[totalSongs][sizeof(filesArray[totalSongs]) - 1] = '\0';
              listOfSong[songsArray[totalSongs]] = filesArray[totalSongs]; 
              totalSongs++;
            } else {
              Serial.print(processedText); 
              Serial.println(" Invalid RTTTL format!");
              deleteFile(FileFS, processedText); 
              //   break;
            }
      } else {
        deleteFile(FileFS, processedText);
      }
    } else {
      entry.close();
    }
  }
  Serial.print("Total Songs found: ");
  Serial.println(totalSongs);  //tell the human a human file count
  serializeJson(listOfSong, Serial);
  Serial.println(" ");
  Serial.println(sizeof(listOfSong));
  listDir(FileFS, "/songs/", 0);
  return;
}

bool is_valid_attribute(int value, const int* valid_values, int array_size) {
  for (int i = 0; i < array_size; i++) {
    if (valid_values[i] == value) return true;
  }
  return false;
}

bool validate_rtttl(char* rtttl) {
  // Parsing variables
  int duration, octave, beats;
  char delimiter;
  // Check format and extract metadata
  if (sscanf(rtttl, "%*[^:]:d=%d,o=%d,b=%d%c", &duration, &octave, &beats, &delimiter) != 4 ||
      !is_valid_attribute(duration, valid_durations, sizeof(valid_durations) / sizeof(valid_durations[0])) ||
      !is_valid_attribute(octave, valid_octaves, sizeof(valid_octaves) / sizeof(valid_octaves[0])) ||
      !is_valid_attribute(beats, valid_beats, sizeof(valid_beats) / sizeof(valid_beats[0])) ||
      delimiter != ':') {
    Serial.println(rtttl);
    Serial.println(" Invalid metadata");
    return false; // Invalid format or metadata
  }
  // Locate the start of notes section
  const char* pos = strchr(strchr(rtttl, ':') + 1, ':') + 1;
  // Validate notes
while (*pos && *pos != '\0') {
  char token[7];
  sscanf(pos, "%6[^,]%c", token, &delimiter);
  // Validate the token against the allowed patterns
  bool isValidToken = false;
  if (strlen(token) == 1) {
    if (strchr("abcdefghp", token[0])) {
      isValidToken = true;
    }
  } else if (strlen(token) == 2) {
    if ((strchr("abcdefghp", token[0]) && token[1] == '#') ||
        (strchr("abcdefghp", token[0]) && token[1] == '.') ||
        (strchr("abcdefghp", token[0]) && strchr("12345678", token[1])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]))) {
      isValidToken = true;
    }
  } else if (strlen(token) == 3) {
    if ((strchr("abcdefghp", token[0]) && strchr("12345678", token[1]) && token[2] == '.') ||
        (strchr("abcdefghp", token[0]) && token[1] == '.'&& strchr("12345678", token[2])) ||
        (strchr("abcdefghp", token[0]) && token[1] == '#' && token[2] == '.') ||
        (strchr("abcdefghp", token[0]) && token[1] == '#' && strchr("12345678", token[2])) ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '#') ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '.') ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && strchr("12345678", token[2]))) {
      isValidToken = true;
    }
  } else if (strlen(token) == 4) {
    if ((strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '#' && strchr("12345678", token[3])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '#' && token[3] == '.') ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '.') ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && strchr("12345678", token[3])) ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '#') ||
        (strchr("abcdefghp", token[0]) && token[1] == '#' && strchr("12345678", token[2]) && token[3] == '.') ||
        (strchr("abcdefghp", token[0]) && token[1] == '#' && token[2] == '.' && strchr("12345678", token[3])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '.' && strchr("12345678", token[3])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && strchr("12345678", token[2]) && token[3] == '.')) {
      isValidToken = true;
    }
  } else if (strlen(token) == 5) {
    if ((strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '#' && token[4] == '.') ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '#'  && token[3] == '.'&& strchr("12345678", token[4])) ||
        (strchr("1248", token[0]) && strchr("abcdefghp", token[1]) && token[2] == '#' && strchr("12345678", token[3]) && token[4] == '.') ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '.' && strchr("12345678", token[4])) ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && strchr("12345678", token[3]) && token[4] == '.') ||
        (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '#' && strchr("12345678", token[4]))) {
      isValidToken = true;
    }
  } else if (strlen(token) == 6) {
    if ((strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '#' && strchr("12345678", token[4]) && token[5] == '.') ||
    (strchr("13", token[0]) && strchr("26", token[1]) && strchr("abcdefghp", token[2]) && token[3] == '#' && token[4] == '.' && strchr("12345678", token[5]))) {
      isValidToken = true;
    }
  } 
  if (isValidToken) {
  //  Serial.println(token);
  } else {
    Serial.print(token);
    Serial.println(" Invalid note format");
    return false; // Invalid note format
  }
  pos = strchr(pos, ',');
  if (pos == NULL) {
    return true; // RTTTL file is valid
    break; // Exit the loop if no more comma found
  }
  pos += 1; // Move to the next note after the comma
}
  return true; // RTTTL file is valid
}
#endif


void writeFile(fs::FS &fs, const char * path, const char * message){
   Serial.printf("Writing file: %s\r\n", path);
   File file = fs.open(path, FILE_WRITE);
   if(!file){
      Serial.println("− failed to open file for writing");
      return;
   }
   if(file.print(message)){
      Serial.println("− file written");
   }else {
      Serial.println("− frite failed");
   }
}

#if HAS_BUZZER
uint32_t playRTTTLsong(String goforit, int chimeNumber)
{
  uint32_t finalRequestId = 0;
  int repeats = chimeNumber < 1 ? 1 : chimeNumber;
  for (int repeat = 0; repeat < repeats; repeat++) {
    uint32_t requestId = queueSongByPath(goforit);
    if (requestId == 0) {
      break;
    }
    finalRequestId = requestId;
  }
  return finalRequestId;
}
#endif

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
   Serial.printf("Listing directory: %s\r\n", dirname);
   File root = fs.open(dirname);
   if(!root){
      Serial.println("− failed to open directory");
      return;
   }
   if(!root.isDirectory()){
      Serial.println(" − not a directory");
      return;
   }
   File file = root.openNextFile();
   while(file){
      platformServices().cooperateDuringSetup();
      if(file.isDirectory()){
         Serial.print("  DIR : ");
         Serial.println(file.name());
         if(levels){
            listDir(fs, file.name(), levels -1);
         }
      } else {
         Serial.print("  FILE: ");
         Serial.print(file.name());
         Serial.print("\tSIZE: ");
         Serial.println(file.size());
      }
      file = root.openNextFile();
   }
}


void deleteFile(fs::FS &fs, const char * path){
   Serial.printf("Deleting file: %s\r\n", path);
   if(fs.remove(path)){
      Serial.println("− file deleted");
   } else {
      Serial.println("− delete failed");
   }
}





void processSchedules(bool alarmType) {
    int fileChanged = 0;
    int hour = timeinfo.tm_hour;
    int mins = timeinfo.tm_min;
    int minsSixty = 101;  //prime number
    if (mins == 0) {minsSixty = 60;}
    else if (mins == 45) {minsSixty = 45;}
    else if (mins == 30) {minsSixty = 30;}
    else if (mins == 15) {minsSixty = 15;}
    int dayOfweek = timeinfo.tm_wday;
    int mday = timeinfo.tm_mday;
    int mont = timeinfo.tm_mon + 1;
    int years = (timeinfo.tm_year +1900);
    int currentEOM = 0;
    #if HAS_BUZZER
      refreshSpecialAudibleAlarmForDate(mont, mday, years);
    #endif
    //  Serial.println("testing...................");
    int object_count = jsonScheduleData.size();
    for (int i = 0; i < (object_count); i++) {
      JsonObject jsonObj = jsonScheduleData[i];
      int scheduleType = (int)jsonObj["scheduleType"];
      int mon = (int)jsonObj["mon"];
      int tue = (int)jsonObj["tue"];
      int wed = (int)jsonObj["wed"];
      int thu = (int)jsonObj["thu"];
      int fri = (int)jsonObj["fri"];
      int sat = (int)jsonObj["sat"];
      int sun = (int)jsonObj["sun"];
      int dailyType = (int)jsonObj["dailyType"];
      int dom = (int)jsonObj["dom"];
      int hours = (int)jsonObj["hours"];
      int minutes = (int)jsonObj["minutes"];
      String song = jsonObj["song"].as<String>();
      String title = jsonObj["title"].as<String>();
      int hourlyType = (int)jsonObj["hourlyType"];
      int gongmode = (int)jsonObj["gongmode"];
      int daymode = (int)jsonObj["daymode"];
      int year = (int)jsonObj["year"];
      int month = (int)jsonObj["month"];
      int day = (int)jsonObj["day"];
      int recurring = (int)jsonObj["recurring"];
      int preset = jsonObj.containsKey("preset") ? (int)jsonObj["preset"] : 0;
      String uid = jsonObj["uid"].as<String>();
      uint32_t scheduleSongRequestId = 0;
/*
    "scheduleType": "3",
    "dom": "00",
    "date": "2023-05-18",
    "hours": "99",
    "minutes": "99",
    "song": "MacGyver",
    "title": "Scott's Birthday",
    "mon": "0",
    "tue": "0",
    "wed": "0",
    "thu": "0",
    "fri": "0",
    "sat": "0",
    "sun": "0",
    "dailyType": "9",
    "hourlyType": "99",
    "gongmode": "0",
    "daymode": "0",
    "year": "9999",
    "month": "05",
    "day": "18",
    "recurring": "0",
    "uid": "00000009999000099990518"
  

      Serial.println("-----local--------");
      Serial.print("record: ");
      Serial.print(i);
      Serial.print(" of ");
      Serial.println(object_count - 1);
      Serial.print("dayOfweek: ");
      Serial.println(dayOfweek);
      Serial.print("mont: ");
      Serial.println(mont);
      Serial.print("mday: ");
      Serial.println(mday);
      Serial.print("hour: ");
      Serial.println(hour);
      Serial.print("mins: ");
      Serial.println(mins);
      Serial.print("minsSixty: ");
      Serial.println(minsSixty);
      Serial.println("-----timers---------");
      Serial.print("scheduleType: ");
      Serial.println(scheduleType);
      Serial.print("dom: ");
      Serial.println(dom);
      Serial.print("hours: ");
      Serial.println(hours);
      Serial.print("minutes: ");
      Serial.println(minutes);
      Serial.print("song: ");
      Serial.println(song);
      Serial.print("title: ");
      Serial.println(title);
      Serial.println("----dow-------");
      Serial.println(mon);
      Serial.println(tue);
      Serial.println(wed);
      Serial.println(thu);
      Serial.println(fri);
      Serial.println(sat);
      Serial.println(sun);
      Serial.print("dailyType: ");
      Serial.println(dailyType);
      Serial.print("hourlyType: ");
      Serial.println(hourlyType);
      Serial.print("gongmode: ");
      Serial.println(gongmode);
      Serial.print("daymode: ");
      Serial.println(daymode);
      Serial.print("year: ");
      Serial.println(year);
      Serial.print("month: ");
      Serial.println(month);
      Serial.print("day: ");
      Serial.println(day);
      Serial.print("recurring: ");
      Serial.println(recurring);
      Serial.println("----------------");
      Serial.println("");
*/

   if (scheduleType == 0)  {   //0 = daily schedule type
      if ((dayOfweek == 0 && sun == 1) || (dayOfweek == 1 && mon == 1) || (dayOfweek == 2 && tue == 1) || (dayOfweek == 3 && wed == 1) || (dayOfweek == 4 && thu == 1) || (dayOfweek == 5 && fri == 1) || (dayOfweek == 6 && sat == 1)) {   //day of week match?
        if (((daymode == 1) && ((hour >= 8) && (hour <=19 ))) || (daymode == 0)) {  //datlight mode is right or not on?
          if (((dailyType == 1) && scheduleTimeMatches(hours, minutes, hour, mins)) || (((hourlyType == 15) && (minsSixty % 15 == 0)) || ((hourlyType == 30) && (minsSixty % 30 == 0)) || ((hourlyType == 60) && (minsSixty % 60 == 0))) || ((dailyType == 0) && ((hourlyType == 99) && (minsSixty % 60 == 0)))) {  //specific time or hourly type is right, or hourlytype 15,30,60?        
            #if HAS_BUZZER
              if (gongmode && (dailyType == 0) && (hourlyType == 99) && (minsSixty % 60 == 0)){  //hourly bongs should always run as secondary because of the uid file name
                // Makes sure the bongs are between 1 and 12. 
                int numberofChimes = hour;
                if (numberofChimes == 0) numberofChimes = 12;
                else if (numberofChimes > 12) numberofChimes -= 12;
                scheduleSongRequestId = playRTTTLsong(listOfSong[song], numberofChimes);
                }  else {
                scheduleSongRequestId = playRTTTLsong(listOfSong[song], 1);
                }
            #endif
            applyScheduledPreset(preset);
           // if (!breakOutSet) {scroll(title);}
          //  allBlank();
          }
        }
      }
   }  //end of daily schedule type


   if (scheduleType == 1)  {   //1 = Monthly schedule type
    if (mont == 2) {    // Function to calculate the number of days in a given month and year
      if (years % 4 == 0) {
        if (years % 100 == 0) {
          if (years % 400 == 0) {
            currentEOM = 29;
          } else {
            currentEOM = 28;
          }
        } else {
          currentEOM = 29;
        }
      } else {
        currentEOM = 28;
      }
    } else if (mont == 4 || mont == 6 || mont == 9 || mont == 11) {
      currentEOM = 30;
    } else {
      currentEOM = 31;
    }

    if ((dom == mday) || (dom == 34 && (mday == currentEOM)) || (dom == 33 && (mday == (currentEOM-1))) || (dom == 32 && (mday == (currentEOM-2))))  {    // Check DOM and EOM
      if (scheduleTimeMatches(hours, minutes, hour, mins)) {  //time, some minute all day, sloppy top of hour
            #if HAS_BUZZER
              scheduleSongRequestId = playRTTTLsong(listOfSong[song], 1);
            #endif
            applyScheduledPreset(preset);
              if (!breakOutSet) {scrollAfterSong(title, scheduleSongRequestId);} 
              allBlank(); 
      }
    }
   }  //end of Monthly schedule type

   if (scheduleType == 2)  {   //2 = Date schedule type
        if (mont == month && mday == day && hours == hour && minutes == mins && (recurring || year == years)) {  //check month/day/hour/min/year
          if (!recurring) { //no recurring? delete after the matching year fires
            char processedName[40];
            char processedText[64];
            strcpy(processedText, "/scheduler/");
            strcpy(processedName, jsonObj["uid"]);
            strcat(processedText, processedName);
            strcat(processedText, ".json");
            Serial.println(uid);
            Serial.println(processedText);
            //FileFS.remove(processedText);
            deleteFile(FileFS, processedText);  //delete non-recurring schedule
            fileChanged = 1;
          }
            #if HAS_BUZZER
              scheduleSongRequestId = playRTTTLsong(listOfSong[song], 1);
            #endif
          applyScheduledPreset(preset);
          if (!breakOutSet) {scrollAfterSong(title, scheduleSongRequestId);} 
          allBlank(); 
        }
   }  //end of Date schedule type


   if (scheduleType == 3)  {   //3 = Special Date schedule type
     if (mont == month && mday == day) {
         // Serial.println("special alarm");
          //fix in future, play song
        if (hour == 12 && mins == 0) {
          #if HAS_BUZZER
            scheduleSongRequestId = playRTTTLsong(listOfSong[song], 1);
          #endif
          applyScheduledPreset(preset);
          if (!breakOutSet) {scrollAfterSong(title, scheduleSongRequestId);} 
          allBlank(); 
        } 
     } 
   }  //end of Special Date schedule type

   if (scheduleType == 4)  {   //4 = New Years schedule type
    if (mont == 01 && mday == 01 && hour == 0 && mins == 0) {
            #if HAS_BUZZER
              scheduleSongRequestId = playRTTTLsong("/settings/auldlang.rttl", 1);
            #endif
          applyScheduledPreset(preset);
          if (!breakOutSet) {scrollAfterSong(title, scheduleSongRequestId);} 
          allBlank(); 
    }
   }  //end of New Years schedule type

//  Serial.println("testing...................");
}
  if (fileChanged) { createSchedulesArray(); }
}


void createSchedulesArray() {
  JsonArray dataArray = jsonScheduleData.to<JsonArray>(); // create a JSON array to store the data
  File dir = FileFS.open("/scheduler"); // open the directory containing the text files
  while (true) { // loop through each file in the directory
    platformServices().cooperateDuringSetup();
    File file = dir.openNextFile();
    if (!file) break; // end the loop when there are no more files
    String fileName = file.name();
    if (fileName.endsWith(".json")) { // only read files that have a .txt extension
      String fileContent = file.readString(); // read the contents of the file
      JsonObject fileData = dataArray.createNestedObject(); // create a JSON object to store the file data
      DynamicJsonDocument jsonScheduleDataTemp(1024);
      DeserializationError error = deserializeJson(jsonScheduleDataTemp, fileContent);
      if (error) {
        Serial.println("Failed to parse JSON file");
        return;
      }
      // Get the values from the JSON object
      JsonObject jsonObj = jsonScheduleDataTemp.as<JsonObject>();
      int scheduleType = jsonObj["scheduleType"];
      int mon = jsonObj["mon"];
      int tue = jsonObj["tue"];
      int wed = jsonObj["wed"];
      int thu = jsonObj["thu"];
      int fri = jsonObj["fri"];
      int sat = jsonObj["sat"];
      int sun = jsonObj["sun"];
      int dailyType = jsonObj["dailyType"];
      int dom = jsonObj["dom"];
      int hours = jsonObj["hours"];
      int minutes = jsonObj["minutes"];
      String song = jsonObj["song"];
      String title = jsonObj["title"];
      int hourlyType = jsonObj["hourlyType"];
      int gongmode = jsonObj["gongmode"];
      int daymode = jsonObj["daymode"];
      int year = jsonObj["year"];
      int month = jsonObj["month"];
      int day = jsonObj["day"];
      int recurring = jsonObj["recurring"];
      int preset = jsonObj.containsKey("preset") ? (int)jsonObj["preset"] : 0;
      String uid = jsonObj["uid"];
      /* Display the values for testing
      Serial.print("scheduleType = ");
      Serial.println(scheduleType);
      Serial.print("mon = ");
      Serial.println(mon);
      Serial.print("tue = ");
      Serial.println(tue);
      Serial.print("wed = ");
      Serial.println(wed);
      Serial.print("thu = ");
      Serial.println(thu);
      Serial.print("fri = ");
      Serial.println(fri);
      Serial.print("sat = ");
      Serial.println(sat);
      Serial.print("sun = ");
      Serial.println(sun);
      Serial.print("dailyType = ");
      Serial.println(dailyType);
      Serial.print("dom = ");
      Serial.println(dom);
      Serial.print("hours = ");
      Serial.println(hours);
      Serial.print("minutes = ");
      Serial.println(minutes);
      Serial.print("song = ");
      Serial.println(song);
      Serial.print("title = ");
      Serial.println(title);
      Serial.print("hourlyType = ");
      Serial.println(hourlyType);
      Serial.print("gongmode = ");
      Serial.println(gongmode);
      Serial.print("daymode = ");
      Serial.println(daymode);
      Serial.print("year = ");
      Serial.println(year);
      Serial.print("month = ");
      Serial.println(month);
      Serial.print("day = ");
      Serial.println(day);
      Serial.print("recurring = ");
      Serial.println(recurring);
      Serial.print("uid = ");
      Serial.println(uid);
      */
      fileData["scheduleType"] = scheduleType;
      fileData["mon"] = mon;
      fileData["tue"] = tue;
      fileData["wed"] = wed;
      fileData["thu"] = thu;
      fileData["fri"] = fri;
      fileData["sat"] = sat;
      fileData["sun"] = sun;
      fileData["dailyType"] = dailyType;
      fileData["dom"] = dom;
      fileData["hours"] = hours;
      fileData["minutes"] = minutes;
      fileData["song"] = song;
      fileData["title"] = title;
      fileData["hourlyType"] = hourlyType;
      fileData["gongmode"] = gongmode;
      fileData["daymode"] = daymode;
      fileData["year"] = year;
      fileData["month"] = month;
      fileData["day"] = day;
      fileData["recurring"] = recurring;
      fileData["preset"] = preset;
      fileData["uid"] = uid;
    }
    file.close();
  }
  serializeJson(jsonScheduleData, Serial); // output the entire JSON array to the console
  Serial.println("");
  #if HAS_BUZZER
    refreshSpecialAudibleAlarmFromCurrentTime();
  #endif
}






static bool loadSettingsJsonDocument(const char *settingsPath, DynamicJsonDocument &settingsDoc, String &fileContent) {
  File file = FileFS.open(settingsPath);
  if (!file) {
    Serial.println("Settings file open failed: " + String(settingsPath));
    return false;
  }

  fileContent = file.readString();
  file.close();

  settingsDoc.clear();
  DeserializationError error = deserializeJson(settingsDoc, fileContent);
  if (error) {
    Serial.println("Failed to parse JSON file: " + String(settingsPath));
    return false;
  }

  return true;
}

static bool tryRecoverSettingsCandidate(const String &candidatePath, const char *activePath, const String &fileType,
                                        DynamicJsonDocument &settingsDoc, String &fileContent) {
  if (!loadSettingsJsonDocument(candidatePath.c_str(), settingsDoc, fileContent)) {
    return false;
  }

  Serial.println("Recovering settings from " + candidatePath);
  if (FileFS.exists(activePath) && !FileFS.remove(activePath)) {
    Serial.println("Could not remove invalid active settings; using recovered settings without promotion.");
    return true;
  }

  if (FileFS.rename(candidatePath.c_str(), activePath)) {
    Serial.println("Recovered settings to " + String(activePath));
  } else {
    Serial.println("Could not promote recovered settings; using recovered settings for this boot.");
  }

  return true;
}

static bool recoverSettingsFile(const String &fileType, const char *activePath,
                                DynamicJsonDocument &settingsDoc, String &fileContent) {
  File dir = FileFS.open("/settings");
  if (!dir || !dir.isDirectory()) {
    Serial.println("Settings recovery failed: cannot open /settings");
    if (dir) dir.close();
    return false;
  }

  String backupPrefix = "clockSettings-" + fileType + ".json.";
  String tempPrefix = "clockSettings-" + fileType + ".tmp.";
  std::vector<String> tempCandidates;
  std::vector<String> backupCandidates;

  File entry = dir.openNextFile();
  while (entry) {
    String full = entry.name();
    int slash = full.lastIndexOf('/');
    String fname = (slash >= 0) ? full.substring(slash + 1) : full;

    if (fname.startsWith(tempPrefix)) {
      tempCandidates.push_back(String("/settings/") + fname);
    } else if (fname.startsWith(backupPrefix)) {
      backupCandidates.push_back(String("/settings/") + fname);
    }

    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  for (auto &path : tempCandidates) {
    if (tryRecoverSettingsCandidate(path, activePath, fileType, settingsDoc, fileContent)) {
      return true;
    }
  }

  for (auto &path : backupCandidates) {
    if (tryRecoverSettingsCandidate(path, activePath, fileType, settingsDoc, fileContent)) {
      return true;
    }
  }

  Serial.println("Settings recovery failed: no valid temp or backup file found");
  return false;
}

void getclockSettings(String fileType) {
  char new_filename[64];
  int activePathLength = snprintf(new_filename, sizeof(new_filename), "/settings/clockSettings-%s.json", fileType.c_str()); //build filename and path from argument
  if (activePathLength < 0 || activePathLength >= (int)sizeof(new_filename)) {
    Serial.println("Settings path too long. Giving up.");
    return;
  }
  Serial.print("Opening ");
  Serial.println(new_filename);
  DynamicJsonDocument jsonDocu(8192);
  String fileContent;
  if (!loadSettingsJsonDocument(new_filename, jsonDocu, fileContent)) {
    Serial.println("Trying settings recovery");
    if (!recoverSettingsFile(fileType, new_filename, jsonDocu, fileContent)) {
      return;
    }
  }

  JsonArray dataArray = jsonDoc.to<JsonArray>(); // create a JSON array to store the data
  JsonObject fileData = dataArray.createNestedObject(); // create a JSON object to store the file data
  Serial.println(fileContent);
  // Set the values from the JSON object
  JsonObject jsonObj = jsonDocu.as<JsonObject>();
  ClockColorSettings = jsonObj["ClockColorSettings"].as<byte>();
  ColorChangeFrequency = jsonObj["ColorChangeFrequency"].as<byte>();
  DSTime = jsonObj["DSTime"].as<bool>();
  DateColorSettings = jsonObj["DateColorSettings"].as<byte>();
  brightness = jsonObj["brightness"].as<byte>();
  loadJsonColor(jsonObj["countdownColor"], countdownColorValue, countdownColor);
  clockDisplayType = jsonObj["clockDisplayType"].as<byte>();
  clockMode = jsonObj["clockMode"].as<byte>();
  colonType = jsonObj["colonType"].as<int>();
  colorchangeCD = jsonObj["colorchangeCD"].as<bool>();
  dateDisplayType = jsonObj["dateDisplayType"].as<byte>();
  gmtOffset_sec = jsonObj["gmtOffset_sec"].as<long>();
  humiColorSettings = jsonObj["humiColorSettings"].as<byte>();
  humiDisplayType = jsonObj["humiDisplayType"].as<byte>();
  lightshowMode = jsonObj["lightshowMode"].as<int>();
  pastelColors = jsonObj["pastelColors"].as<byte>();
  loadJsonColor(jsonObj["spotlightsColor"], spotlightsColorValue, spotlightsColor);
  loadJsonColor(jsonObj["humiColor"], humiColorValue, humiColor);
  loadJsonColor(jsonObj["humiSymbolColor"], humiSymbolColorValue, humiSymbolColor);
  loadJsonColor(jsonObj["humiDecimalColor"], humiDecimalColorValue, humiDecimalColor);
  loadJsonColor(jsonObj["scoreboardColorLeft"], scoreboardColorLeftValue, scoreboardColorLeft);
  loadJsonColor(jsonObj["scoreboardColorRight"], scoreboardColorRightValue, scoreboardColorRight);
  loadJsonColor(jsonObj["spectrumColor"], spectrumColorValue, spectrumColor);
  loadJsonColor(jsonObj["scrollColor"], scrollColorValue, scrollColor);
  loadJsonColor(jsonObj["spectrumBackgroundColor"], spectrumBackgroundColorValue, spectrumBackgroundColor);
  loadJsonColor(jsonObj["hourColor"], hourColorValue, hourColor);
  loadJsonColor(jsonObj["minColor"], minColorValue, minColor);
  loadJsonColor(jsonObj["colonColor"], colonColorValue, colonColor);
  loadJsonColor(jsonObj["dayColor"], dayColorValue, dayColor);
  loadJsonColor(jsonObj["monthColor"], monthColorValue, monthColor);
  loadJsonColor(jsonObj["separatorColor"], separatorColorValue, separatorColor);
  loadJsonColor(jsonObj["tempColor"], tempColorValue, tempColor);
  loadJsonColor(jsonObj["typeColor"], typeColorValue, typeColor);
  loadJsonColor(jsonObj["degreeColor"], degreeColorValue, degreeColor);
  randomSpectrumMode = jsonObj["randomSpectrumMode"].as<byte>();
  realtimeMode = jsonObj["realtimeMode"].as<int>();
  scrollColorSettings = jsonObj["scrollColorSettings"].as<int>();
  scrollFrequency = jsonObj["scrollFrequency"].as<int>();
  scrollOptions1 = jsonObj["scrollOptions1"].as<bool>();
  scrollOptions2 = jsonObj["scrollOptions2"].as<bool>();
  scrollOptions3 = jsonObj["scrollOptions3"].as<bool>();
  scrollOptions4 = jsonObj["scrollOptions4"].as<bool>();
  scrollOptions5 = jsonObj["scrollOptions5"].as<bool>();
  scrollOptions6 = jsonObj["scrollOptions6"].as<bool>();
  scrollOptions7 = jsonObj["scrollOptions7"].as<bool>();
  scrollOptions8 = jsonObj["scrollOptions8"].as<bool>();
  scrollOptions9 = jsonObj["scrollOptions9"].as<bool>();
  scrollOverride = jsonObj["scrollOverride"].as<bool>();
  scrollText = jsonObj["scrollText"].as<String>();
  #if HAS_USWEATHER
    if (!jsonObj["stationID"].isNull()) {
      copyJsonCString(jsonObj["stationID"], stationID, sizeof(stationID));
    }
    #endif
  spectrumBackgroundSettings = jsonObj["spectrumBackgroundSettings"].as<byte>();
  spectrumColorSettings = jsonObj["spectrumColorSettings"].as<byte>();
  spectrumMode = jsonObj["spectrumMode"].as<int>();
  spotlightsColorSettings = jsonObj["spotlightsColorSettings"].as<int>();
  suspendFrequency = jsonObj["suspendFrequency"].as<int>();
  suspendType = jsonObj["suspendType"].as<byte>();
  tempColorSettings = jsonObj["tempColorSettings"].as<byte>();
  tempDisplayType = jsonObj["tempDisplayType"].as<byte>();
  temperatureCorrection = jsonObj["temperatureCorrection"].as<int>();
  temperatureSymbol = jsonObj["temperatureSymbol"].as<byte>();
  #if HAS_BUZZER
  useAudibleAlarm = jsonObj["useAudibleAlarm"].as<bool>();
  if (!jsonObj["defaultAudibleAlarm"].isNull()) {
    copyAlarmName(defaultAudibleAlarm, sizeof(defaultAudibleAlarm), jsonObj["defaultAudibleAlarm"].as<const char*>());
    refreshSpecialAudibleAlarmFromCurrentTime();
  }
  #endif
  useSpotlights = jsonObj["useSpotlights"].as<bool>();
  #if HAS_ONLINEWEATHER
    if (!jsonObj["apikey"].isNull()) {
      copyJsonCString(jsonObj["apikey"], apikey, sizeof(apikey));
    }
    #endif
    #if HAS_ONLINEWEATHER || HAS_USWEATHER
    humidity_outdoor_enable = jsonObj["humidity_outdoor_enable"].as<bool>();
    temperature_outdoor_enable = jsonObj["temperature_outdoor_enable"].as<bool>();
    if (!jsonObj["latitude"].isNull()) {
      copyJsonCString(jsonObj["latitude"], latitude, sizeof(latitude));
    }
    if (!jsonObj["longitude"].isNull()) {
      copyJsonCString(jsonObj["longitude"], longitude, sizeof(longitude));
    }
    #endif
}  



static int countSettingsBackups(const String &fileType) {
  const char *folder = "/settings";
  File dir = FileFS.open(folder);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  int count = 0;
  String backupPrefix = "clockSettings-" + fileType + ".json.";

  File entry = dir.openNextFile();
  while (entry) {
    String full = entry.name();
    int slash = full.lastIndexOf('/');
    String fname = (slash >= 0) ? full.substring(slash + 1) : full;
    if (fname.startsWith(backupPrefix)) {
      count++;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return count;
}

static void cleanupSettingsFiles(const String &fileType, const String &preservePath,
                                 bool removeBackups, bool removeTemps) {
  const char *folder = "/settings";
  File dir = FileFS.open(folder);
  if (!dir || !dir.isDirectory()) {
    Serial.println("cleanup: cannot open /settings");
    return;
  }

  String backupPrefix = "clockSettings-" + fileType + ".json.";
  String tempPrefix = "clockSettings-" + fileType + ".tmp.";
  std::vector<String> toDelete;

  // 1) gather names
  File entry = dir.openNextFile();
  while (entry) {
    String full = entry.name();  
    // strip leading path, if any
    int slash = full.lastIndexOf('/');
    String fname = (slash >= 0) ? full.substring(slash + 1) : full;

    if ((removeBackups && fname.startsWith(backupPrefix)) || (removeTemps && fname.startsWith(tempPrefix))) {
      String path = String(folder) + "/" + fname;
      if (preservePath.length() == 0 || path != preservePath) {
        toDelete.push_back(path);
      }
    }
    entry.close();                  // close this file descriptor
    entry = dir.openNextFile();
  }
  dir.close();                      // close the directory now

  // 2) remove them once everything’s closed
  for (auto &path : toDelete) {
    Serial.println("Removing old settings artifact: " + path);
    if (!FileFS.remove(path)) {
      Serial.println("failed to remove: " + path);
    }
  }
}

static void cleanupOldSettingsExcept(const String &fileType, const String &preservePath) {
  cleanupSettingsFiles(fileType, preservePath, true, true);
}

static void cleanupStaleSettingsTemps(const String &fileType, const String &preservePath) {
  cleanupSettingsFiles(fileType, preservePath, false, true);
}

static void resetSettingsBackupsIfPoolFull(const String &fileType) {
#if SETTINGS_BACKUP_RETAIN_COUNT > 0
  int backupCount = countSettingsBackups(fileType);
  if (backupCount >= SETTINGS_BACKUP_RETAIN_COUNT) {
    Serial.println("Settings backup pool full; clearing randomized backups.");
    cleanupSettingsFiles(fileType, "", true, false);
  }
#endif
}

static bool writeSettingsTempFile(const char *tempName, DynamicJsonDocument &doc) {
  File file = FileFS.open(tempName, FILE_WRITE);
  if (!file) {
    Serial.println("Temp settings open failed.");
    return false;
  }

  size_t expectedBytes = measureJson(doc);
  size_t writtenBytes = serializeJson(doc, file);
  file.flush();
  file.close();
  if (writtenBytes != expectedBytes) {
    Serial.println("Settings temp write incomplete.");
    FileFS.remove(tempName);
    return false;
  }

  return true;
}

void cleanupOldSettings(const String &fileType) {
  cleanupOldSettingsExcept(fileType, "");
}

bool saveclockSettings(String fileType) {
  // build names
  char newName[64], oldName[64], tempName[64];
  int backupPathLength = snprintf(newName, sizeof(newName), "/settings/clockSettings-%s.json.%05d",
          fileType.c_str(), random(0, 65535));
  int activePathLength = snprintf(oldName, sizeof(oldName), "/settings/clockSettings-%s.json", fileType.c_str());
  int tempPathLength = snprintf(tempName, sizeof(tempName), "/settings/clockSettings-%s.tmp.%05d",
          fileType.c_str(), random(0, 65535));
  if (backupPathLength < 0 || backupPathLength >= (int)sizeof(newName) ||
      activePathLength < 0 || activePathLength >= (int)sizeof(oldName) ||
      tempPathLength < 0 || tempPathLength >= (int)sizeof(tempName)) {
    Serial.println("Settings path too long. Giving up.");
    return false;
  }

  // Ensure settings directory exists (prevents creation errors on fresh FS)
  {
    File dir = FileFS.open("/settings");
    if (!dir || !dir.isDirectory()) {
      if (!FileFS.mkdir("/settings")) {
        Serial.println("Settings directory create failed. Giving up.");
        if (dir) dir.close();
        return false;
      }
    }
    if (dir) dir.close();
  }
  resetSettingsBackupsIfPoolFull(fileType);
  cleanupStaleSettingsTemps(fileType, "");

  /*
  // If an existing preset file is present, back it up first; otherwise skip rename to avoid errors
  if (FileFS.exists(oldName)) {
    Serial.println("Backing up existing to " + String(newName));
    if (!FileFS.rename(oldName, newName)) {
      Serial.println("Rename failed (low space?), cleaning old backups…");
      cleanupOldSettings(fileType);
      // retry once
      FileFS.rename(oldName, newName);
    }
  } else {
    Serial.println("No existing preset, creating new: " + String(oldName));
  }

  */
  // 2) serialize your JSON into a heap doc
  DynamicJsonDocument doc(8192);
  JsonObject clockSettings = doc.to<JsonObject>();
  // Add the values to the JSON object
  clockSettings["gmtOffset_sec"] = gmtOffset_sec;
  clockSettings["DSTime"] = DSTime;
  clockSettings["countdownColor"] = countdownColorValue; 
  clockSettings["spotlightsColor"] = spotlightsColorValue; 
  clockSettings["hourColor"] = hourColorValue; 
  clockSettings["minColor"] = minColorValue; 
  clockSettings["colonColor"] = colonColorValue; 
  clockSettings["dayColor"] = dayColorValue; 
  clockSettings["monthColor"] = monthColorValue; 
  clockSettings["separatorColor"] = separatorColorValue; 
  clockSettings["tempColor"] = tempColorValue; 
  clockSettings["typeColor"] = typeColorValue; 
  clockSettings["degreeColor"] = degreeColorValue; 
  clockSettings["humiColor"] = humiColorValue; 
  clockSettings["humiSymbolColor"] = humiSymbolColorValue; 
  clockSettings["humiDecimalColor"] = humiDecimalColorValue; 
  clockSettings["scoreboardColorLeft"] = scoreboardColorLeftValue; 
  clockSettings["scoreboardColorRight"] = scoreboardColorRightValue; 
  clockSettings["spectrumColor"] = spectrumColorValue; 
  clockSettings["scrollColor"] = scrollColorValue; 
  clockSettings["spectrumBackgroundColor"] = spectrumBackgroundColorValue; 
  clockSettings["clockMode"] = clockMode;
  clockSettings["pastelColors"] = pastelColors;
  clockSettings["temperatureSymbol"] = temperatureSymbol;
  clockSettings["ClockColorSettings"] = ClockColorSettings;
  clockSettings["DateColorSettings"] = DateColorSettings;
  clockSettings["tempColorSettings"] = tempColorSettings;
  clockSettings["humiColorSettings"] = humiColorSettings;
  clockSettings["tempDisplayType"] = tempDisplayType;
  clockSettings["humiDisplayType"] = humiDisplayType;
  clockSettings["temperatureCorrection"] = temperatureCorrection;
  clockSettings["colonType"] = colonType;
  clockSettings["ColorChangeFrequency"] = ColorChangeFrequency;
  clockSettings["scrollText"] = scrollText;
  #if HAS_USWEATHER
    clockSettings["stationID"] = stationID;
    #endif
  clockSettings["clockDisplayType"] = clockDisplayType;
  clockSettings["dateDisplayType"] = dateDisplayType;
  clockSettings["colorchangeCD"] = colorchangeCD;
  #if HAS_BUZZER
  clockSettings["useAudibleAlarm"] = useAudibleAlarm;
  clockSettings["defaultAudibleAlarm"] = defaultAudibleAlarm;
  #endif
  clockSettings["spectrumMode"] = spectrumMode;
  clockSettings["realtimeMode"] = realtimeMode;
  clockSettings["spectrumColorSettings"] = spectrumColorSettings;
  clockSettings["spectrumBackgroundSettings"] = spectrumBackgroundSettings;
  clockSettings["spotlightsColorSettings"] = spotlightsColorSettings;
  clockSettings["brightness"] = brightness;
  clockSettings["useSpotlights"] = useSpotlights;
  clockSettings["scrollColorSettings"] = scrollColorSettings;
  clockSettings["scrollFrequency"] = scrollFrequency;
  clockSettings["randomSpectrumMode"] = randomSpectrumMode;
  clockSettings["scrollOverride"] = scrollOverride;
  clockSettings["scrollOptions1"] = scrollOptions1;
  clockSettings["scrollOptions2"] = scrollOptions2;
  clockSettings["scrollOptions3"] = scrollOptions3;
  clockSettings["scrollOptions4"] = scrollOptions4;
  clockSettings["scrollOptions5"] = scrollOptions5;
  clockSettings["scrollOptions6"] = scrollOptions6;
  clockSettings["scrollOptions7"] = scrollOptions7;
  clockSettings["scrollOptions8"] = scrollOptions8;
  clockSettings["scrollOptions9"] = scrollOptions9;
  clockSettings["lightshowMode"] = lightshowMode;
  clockSettings["suspendFrequency"] = suspendFrequency;
  clockSettings["suspendType"] = suspendType;
  #if HAS_ONLINEWEATHER
    clockSettings["apikey"] = apikey;
    #endif
    #if HAS_ONLINEWEATHER || HAS_USWEATHER
    clockSettings["humidity_outdoor_enable"] = humidity_outdoor_enable;
    clockSettings["temperature_outdoor_enable"] = temperature_outdoor_enable;
    clockSettings["latitude"] = latitude;
    clockSettings["longitude"] = longitude;
    #endif

  // 3) write replacement to a random temp file first so the active settings survive failed writes.
  if (!writeSettingsTempFile(tempName, doc)) {
    Serial.println("Temp settings write failed, cleaning backups and retrying.");
    cleanupOldSettings(fileType);
    if (!writeSettingsTempFile(tempName, doc)) {
      Serial.println("Still cannot write temp settings file. Giving up.");
      return false;
    }
  }

  bool hadExistingSettings = FileFS.exists(oldName);
  if (hadExistingSettings) {
    Serial.println("Backing up existing to " + String(newName));
    if (!FileFS.rename(oldName, newName)) {
      Serial.println("Backup rename failed, cleaning old backups and retrying.");
      cleanupOldSettingsExcept(fileType, tempName);
      if (!FileFS.rename(oldName, newName)) {
        Serial.println("Still cannot back up existing settings. Keeping existing settings.");
        FileFS.remove(tempName);
        return false;
      }
    }
  } else {
    Serial.println("No existing preset, creating new: " + String(oldName));
  }

  if (!FileFS.rename(tempName, oldName)) {
    Serial.println("Temp settings rename failed.");
    if (hadExistingSettings) {
      Serial.println("Restoring previous settings backup.");
      FileFS.rename(newName, oldName);
    }
    FileFS.remove(tempName);
    return false;
  }

  cleanupStaleSettingsTemps(fileType, "");
  Serial.println("Settings saved successfully. Backup pool retained.");
  return true;

/*
// 3) write it out
File file = FileFS.open(oldName, FILE_WRITE);
if (!file) {
  Serial.println("Open for write failed, cleaning backups and retrying…");
  cleanupOldSettings(fileType);
  file = FileFS.open(oldName, FILE_WRITE);
  if (!file) {
    Serial.println("Still can’t open file. Giving up.");
    return;
  }
}

serializeJson(doc, file);
file.close();
Serial.println("Settings saved successfully. Old backups (if any) removed.");
*/
}


static void populateCapabilityFlags(JsonObject json) {
  json["HAS_DHT"] = HAS_DHT;
  json["HAS_RTC"] = HAS_RTC;
  json["HAS_SOUNDDETECTOR"] = HAS_SOUNDDETECTOR;
  json["HAS_BUZZER"] = HAS_BUZZER;
  json["HAS_PHOTOSENSOR"] = HAS_PHOTOSENSOR;
  json["HAS_ONLINEWEATHER"] = HAS_ONLINEWEATHER;
  json["HAS_USWEATHER"] = HAS_USWEATHER;
}

static void populateHomeJson(JsonObject json) {
  const shelfclock::HomeState state = shelfclock::getHomeState();
  json["clockMode"] = state.clockMode;
  json["realtimeMode"] = state.realtimeMode;
  json["scoreboardLeft"] = state.scoreboardLeft;
  json["scoreboardRight"] = state.scoreboardRight;
  json["lightshowMode"] = state.lightshowMode;
  json["spectrumMode"] = state.spectrumMode;
  populateCapabilityFlags(json);
}

static void populateSettingsJson(JsonObject json) {
  json["pastelColors"] = pastelColors;
  json["ColorChangeFrequency"] = ColorChangeFrequency;
  json["rangeBrightness"] = brightness;
  json["suspendType"] = suspendType;
  json["suspendFrequency"] = suspendFrequency;
  json["useSpotlights"] = useSpotlights;
  json["spotlightsColor"] = spotlightsColorValue;
  json["spotlightsColorSettings"] = spotlightsColorSettings;
  json["ClockDisplayType"] = clockDisplayType;
  json["colonType"] = colonType;
  json["gmtOffset_sec"] = gmtOffset_sec;
  json["DSTime"] = DSTime;
  json["ClockColorSettings"] = ClockColorSettings;
  json["hourColor"] = hourColorValue;
  json["minColor"] = minColorValue;
  json["colonColor"] = colonColorValue;
  json["dateDisplayType"] = dateDisplayType;
  json["DateColorSettings"] = DateColorSettings;
  json["dayColor"] = dayColorValue;
  json["monthColor"] = monthColorValue;
  json["separatorColor"] = separatorColorValue;
  json["temperatureSymbol"] = temperatureSymbol;
  json["temperatureCorrection"] = temperatureCorrection;
  json["tempDisplayType"] = tempDisplayType;
  json["tempColorSettings"] = tempColorSettings;
  json["tempColor"] = tempColorValue;
  json["typeColor"] = typeColorValue;
  json["degreeColor"] = degreeColorValue;
  json["humiDisplayType"] = humiDisplayType;
  json["humiColorSettings"] = humiColorSettings;
  json["humiColor"] = humiColorValue; 
  json["humiDecimalColor"] = humiDecimalColorValue;
  json["humiSymbolColor"] = humiSymbolColorValue;
#if HAS_BUZZER
  json["useAudibleAlarm"] = useAudibleAlarm;
  json["defaultAudibleAlarm"] = defaultAudibleAlarm;
#endif
  json["colorchangeCD"] = colorchangeCD;
  json["countdownColor"] = countdownColorValue;
  json["scoreboardColorLeft"] = scoreboardColorLeftValue;
  json["scoreboardColorRight"] = scoreboardColorRightValue;
  json["randomSpectrumMode"] = randomSpectrumMode;
  json["spectrumColor"] = spectrumColorValue;
  json["spectrumBackgroundColor"] = spectrumBackgroundColorValue;
  json["spectrumBackgroundSettings"] = spectrumBackgroundSettings;
  json["spectrumColorSettings"] = spectrumColorSettings;
  json["scrollFrequency"] = scrollFrequency;
  json["scrollOptions1"] = scrollOptions1;
  json["scrollOptions2"] = scrollOptions2;
  json["scrollOptions3"] = scrollOptions3;
  json["scrollOptions4"] = scrollOptions4;
  json["scrollOptions5"] = scrollOptions5;
  json["scrollOptions6"] = scrollOptions6;
  json["scrollOptions7"] = scrollOptions7;
  json["scrollOptions8"] = scrollOptions8;
  json["scrollOptions9"] = scrollOptions9;
  json["scrollText"] = scrollText;
  json["scrollOverride"] = scrollOverride;
  json["scrollColor"] = scrollColorValue;
  json["scrollColorSettings"] = scrollColorSettings;
#if HAS_ONLINEWEATHER
  json["weatherapi"]["apikey"] = apikey;
#endif
#if HAS_USWEATHER
  json["stationID"] = stationID;
#endif
#if HAS_ONLINEWEATHER || HAS_USWEATHER
  json["weatherapi"]["latitude"] = latitude;
  json["weatherapi"]["longitude"] = longitude;
  json["humidity_outdoor_enable"] = humidity_outdoor_enable;
  json["temperature_outdoor_enable"] = temperature_outdoor_enable;
#endif
  populateCapabilityFlags(json);
#if HAS_BUZZER
  json["listOfSong"] = listOfSong;
#endif
}

void loadWebPageHandlers() {
 
  server.on("/gethome", []() {
    DynamicJsonDocument json(1500);
    String output;
    if (updateSettingsRequired == 1 && saveclockSettings("generic")) { updateSettingsRequired = 0; }  //if something was changed in settings, write those changes when the home page is loaded.
    populateHomeJson(json.to<JsonObject>());
    serializeJson(json, output);
    server.send(200, "application/json", output);
  });

  server.on("/getsettings", []() {
    DynamicJsonDocument json(8192);
    String output;
    populateSettingsJson(json.to<JsonObject>());
    serializeJson(json, output);
    server.send(200, "application/json", output);
  });

  server.on("/updateanything", HTTP_POST, []() {
    DynamicJsonDocument json(1500);
    String body;
    if(readLoggedPostJson(json, body)) {
      bool saveSettingsNow = false;

      applyDisplayScalarSettings(json.as<JsonObjectConst>());
      applyColorAndToggleSettings(json.as<JsonObjectConst>());
      applyScrollAndSpectrumOptionSettings(json.as<JsonObjectConst>());

      applyWeatherStationCommand(json.as<JsonObjectConst>());
      applySavePresetCommands(json.as<JsonObjectConst>());
      applyManualDateTimeCommand(json.as<JsonObjectConst>());

      applyDisplayModeCommands(json.as<JsonObjectConst>(), saveSettingsNow);

      applyLoadPresetCommands(json.as<JsonObjectConst>());
      applyRealtimeModeCommands(json.as<JsonObjectConst>(), saveSettingsNow);

      applyDefaultAudibleAlarmCommand(json.as<JsonObjectConst>());
      applyWeatherSettings(json.as<JsonObjectConst>());

      if (saveSettingsNow && saveclockSettings("generic")) {
        updateSettingsRequired = 0;
      }
      server.send(200, "text/json", "{\"result\":\"ok\"}");
      return;
    } 
    server.send(401);
  });

  server.on("/playsong", HTTP_POST, []() {
    DynamicJsonDocument json(1500);
    String body;
    if(readLoggedPostJson(json, body)) {
  #if HAS_BUZZER
      // play music
      if (!json["song"].isNull()) {
          playRequestedSong(json["song"]);
      }
  #endif
    } 
    server.send(204);
  });

   server.on("/getscheduler", []() {
    DynamicJsonDocument json(16192);
    String output;
    Serial.println("getscheduler running");
    json["HAS_BUZZER"] = HAS_BUZZER;
#if HAS_BUZZER
    json["listOfSong"] = listOfSong;
#endif
    json["jsonScheduleData"] = jsonScheduleData;
    serializeJson(json, output);
    server.send(200, "application/json", output);
  });

  server.on("/schedulerDelete", HTTP_POST, []() {   //catch post from the scheduler page save function
    DynamicJsonDocument json(1500);
    if(server.args() > 0) {
      deserializeJson(json, server.arg(0));
      Serial.println(server.arg(0));
      if (!json["deleteData"].isNull()) {     //if there was data
        String deleteData = json["deleteData"].as<String>();	  
        char deleteData_array[(deleteData.length() + 1)];
        deleteData.toCharArray(deleteData_array, (deleteData.length() + 1));  //make UID filename
        char XprocessedText[255];
        strcpy(XprocessedText, "/scheduler/");       //build full pathname
        strcat(XprocessedText, deleteData_array);  //build full pathname
        strcat(XprocessedText, ".json");
      deleteFile(FileFS, XprocessedText);              //build full pathname
      Serial.println("Deleting Schedule..");
      Serial.println(deleteData);
      }
      createSchedulesArray();   //update the running schedule json array with the new file contents
      server.send(200, "text/json", "{\"result\":\"ok\"}");
      return;
    } 
  server.send(401);
  });

  server.on("/scheduler", HTTP_POST, []() {   //catch post from the scheduler page save function
    DynamicJsonDocument json(1500);
    if(server.args() > 0) {
      String body = server.arg(0); //assign data from post
      Serial.println(server.arg(0));
      deserializeJson(json, server.arg(0));
      if (!json["scheduleType"].isNull()) {     //if there was data
        int schedulerScheduleType = (int)json["scheduleType"];
        int schedulerDom = (int)json["dom"];
        int schedulerHours = (int)json["hours"];
        int schedulerMinutes = (int)json["minutes"];
        String schedulerSong = json["song"].as<String>();
        String schedulerTitle = json["title"].as<String>();
        int schedulerMon = (int)json["mon"];
        int schedulerTue = (int)json["tue"];
        int schedulerWed = (int)json["wed"];
        int schedulerThu = (int)json["thu"];
        int schedulerFri = (int)json["fri"];
        int schedulerSat = (int)json["sat"];
        int schedulerSun = (int)json["sun"];
        int schedulerDailyType = (int)json["dailyType"];
        int schedulerHourlyType = (int)json["hourlyType"];
        int schedulerGongmode = (int)json["gongmode"];
        int schedulerDaymode = (int)json["daymode"];
        int schedulerYear = (int)json["year"];
        int schedulerMonth = (int)json["month"];
        int schedulerDay = (int)json["day"];
        int schedulerRecurring = (int)json["recurring"];
        String schedulerUid = json["uid"].as<String>();	  
        char schedulerUid_array[(schedulerUid.length() + 1)];
        schedulerUid.toCharArray(schedulerUid_array, (schedulerUid.length() + 1));  //make UID filename
        char body_array[(body.length() + 1)];   //make data blerb
        body.toCharArray(body_array, (body.length() + 1));
        char XprocessedText[255];
        strcpy(XprocessedText, "/scheduler/");       //build full pathname
        strcat(XprocessedText, schedulerUid_array);  //build full pathname
        strcat(XprocessedText, ".json");              //build full pathname
        writeFile(FileFS, XprocessedText, body_array);   //write json data from post to a file named for the UID in the post
      }
      createSchedulesArray();   //update the running schedule json array with the new UID file contents
      server.send(200, "text/json", "{\"result\":\"ok\"}");
      return;
    } 
  server.send(401);
  });

  server.on("/getdebug", []() {
    char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    char monthsOfTheYear[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    #if HAS_DHT
      const shelfclock::runtime::EnvironmentReading environment = platformServices().readEnvironment();
      int humidTemp = environment.humidity;
      float sensorTemp = environment.temperatureC;
      float saturationVaporPressure = 6.1078 * pow(10, (7.5 * sensorTemp / (237.3 + sensorTemp)));  // Calculate saturation vapor pressure
      float vaporPressure = saturationVaporPressure * (humidTemp / 100.0);  // Calculate vapor pressure
      float absoluteHumidity = 217 * vaporPressure / (273.15 + sensorTemp);  // Calculate absolute humidity
      float humidityRatio = 0.622 * vaporPressure / (101325 - vaporPressure);
      float heatIndex = -8.784695 + 1.61139411 * sensorTemp + 2.338549 * humidTemp + -0.14611605 * sensorTemp * humidTemp + -0.01230809 * pow(sensorTemp, 2) + -0.01642482 * pow(humidTemp, 2) + 0.00221173 * pow(sensorTemp, 2) * humidTemp + 0.00072546 * sensorTemp * pow(humidTemp, 2) + -0.00000358 * pow(sensorTemp, 2) * pow(humidTemp, 2);
      heatIndex += altitudeLocal * 0.0065;  // Adjust heat index based on altitude
    #endif
    char tempRTC[64]="";
    char tempRTCE[64]="";
    String output;
    DynamicJsonDocument json(11000);

    json["softwareVersion"] = softwareVersion;
    json["HAS_RTC"] = HAS_RTC;
    json["HAS_DHT"] = HAS_DHT;
    json["HAS_SOUNDDETECTOR"] = HAS_SOUNDDETECTOR;
    json["HAS_BUZZER"] = HAS_BUZZER;
    json["HAS_PHOTOSENSOR"] = HAS_PHOTOSENSOR;
    json["HAS_ONLINEWEATHER"] = HAS_ONLINEWEATHER;
    json["HAS_USWEATHER"] = HAS_USWEATHER;
    #if HAS_SOUNDDETECTOR
      json["ANALYZER_SIZE"] = ANALYZER_SIZE;
    #endif	
    #if HAS_BUZZER
      json["BUZZER_PIN"] = BUZZER_PIN;
    #endif
    json["ClockColorSettings"] = ClockColorSettings;
    json["COLOR_ORDER"] = STRINGIFY(COLOR_ORDER);
    json["ColorChangeFrequency"] = ColorChangeFrequency;
    json["CountUpMillis"] = CountUpMillis;
    json["DateColorSettings"] = DateColorSettings;
    #if HAS_DHT
      json["DHT_PIN"] = DHT_PIN;
      json["DHT11 C Temp"] = sensorTemp;
      json["DHT11 F Temp"] = (sensorTemp * 1.8000) + 32;
      json["DHT11 Humidity (Relative)"] = humidTemp;
      json["DHT11 Humidity (Absolute)"] = absoluteHumidity;
      json["DHT11 Humidity (Ratio)"] = humidityRatio;
      json["DHT11 (Heat Index F)"] = (heatIndex * 1.8000) + 32;
      json["DHTTYPE"] = STRINGIFY(DHTTYPE);
    #endif
    #if HAS_RTC
      struct tm rtcTime = {};
      platformServices().readRtc(rtcTime);
      sprintf(tempRTC, "%s, ", daysOfTheWeek[rtcTime.tm_wday]);
      sprintf(tempRTC + strlen(tempRTC), "%s ", monthsOfTheYear[rtcTime.tm_mon]);
      sprintf(tempRTC + strlen(tempRTC), "%02d ", rtcTime.tm_mday);
      sprintf(tempRTC + strlen(tempRTC), "%d ", rtcTime.tm_year + 1900);
      sprintf(tempRTC + strlen(tempRTC), "%02d:", rtcTime.tm_hour);
      sprintf(tempRTC + strlen(tempRTC), "%02d:", rtcTime.tm_min);
      sprintf(tempRTC + strlen(tempRTC), "%02d", rtcTime.tm_sec);
      json["DS-3231"] = tempRTC; //DS3231 RTC 
    #endif
    json["DSTime"] = DSTime;
    sprintf(tempRTCE, "%s, ", daysOfTheWeek[timeinfo.tm_wday]);
    sprintf(tempRTCE + strlen(tempRTCE), "%s ", monthsOfTheYear[timeinfo.tm_mon]);
    sprintf(tempRTCE + strlen(tempRTCE), "%02d ", timeinfo.tm_mday);
    sprintf(tempRTCE + strlen(tempRTCE), "%d ", timeinfo.tm_year+1900);
    sprintf(tempRTCE + strlen(tempRTCE), "%02d:", timeinfo.tm_hour);
    sprintf(tempRTCE + strlen(tempRTCE), "%02d:", timeinfo.tm_min);
    sprintf(tempRTCE + strlen(tempRTCE), "%02d", timeinfo.tm_sec);
    json["ESP32-NTP"] = tempRTCE; //ESP32 RTC
    json["FAKE_NUM_LEDS"] = FAKE_NUM_LEDS;
    json["LEDS_PER_DIGIT"] = LEDS_PER_DIGIT;
    json["LEDS_PER_SEGMENT"] = LEDS_PER_SEGMENT;
    json["LED_PIN"] = LED_PIN;
    json["LED_TYPE"] = STRINGIFY(LED_TYPE);
    json["MILLI_AMPS"] = MILLI_AMPS;
    json["NUMBER_OF_DIGITS"] = NUMBER_OF_DIGITS;
    json["NUM_LEDS"] = NUM_LEDS;
    #if HAS_PHOTOSENSOR
      json["PHOTO_SAMPLES"] = PHOTO_SAMPLES;
      json["PHOTO_SIZE"] = PHOTO_SIZE;
      json["PHOTORESISTER_PIN"] = PHOTORESISTER_PIN;
    #endif
    json["SEGMENTS_LEDS"] = SEGMENTS_LEDS;
    json["SEGMENTS_PER_NUMBER"] = SEGMENTS_PER_NUMBER;
    #if HAS_SOUNDDETECTOR
      json["SOUNDDETECTOR_Amplitude"] = SOUNDDETECTOR_Amplitude;
      json["SOUNDDETECTOR_BANDS_HEIGHT"] = SOUNDDETECTOR_BANDS_HEIGHT;
      json["SOUNDDETECTOR_BANDS_WIDTH"] = SOUNDDETECTOR_BANDS_WIDTH;
      json["SOUNDDETECTOR_BITS_PER_SAMPLE"] = SOUNDDETECTOR_BITS_PER_SAMPLE;
      json["SOUNDDETECTOR_I2S_PORT"] = SOUNDDETECTOR_I2S_PORT;
      json["SOUNDDETECTOR_I2S_SCK"] = SOUNDDETECTOR_I2S_SCK;
      json["SOUNDDETECTOR_I2S_SD"] = SOUNDDETECTOR_I2S_SD;
      json["SOUNDDETECTOR_I2S_WS"] = SOUNDDETECTOR_I2S_WS;
      json["SOUNDDETECTOR_SAMPLES"] = SOUNDDETECTOR_SAMPLES;
      json["SOUNDDETECTOR_SAMPLING_FREQ"] = SOUNDDETECTOR_SAMPLING_FREQ;
      json["SOUNDDETECTOR_averageAudioInput"] = SOUNDDETECTOR_averageAudioInput;
      JsonArray SOUNDDETECTOR_bandValuesArray = json.createNestedArray("SOUNDDETECTOR_bandValues");
      for (int i = 0; i < SOUNDDETECTOR_BANDS_WIDTH; i++) { SOUNDDETECTOR_bandValuesArray.add(SOUNDDETECTOR_bandValues[i]); }
      json["SOUNDDETECTOR_decay"] = SOUNDDETECTOR_decay;
      json["SOUNDDETECTOR_decay_check"] = SOUNDDETECTOR_decay_check;
      json["SOUNDDETECTOR_post_react"] = SOUNDDETECTOR_post_react;
      json["SOUNDDETECTOR_pre_react"] = SOUNDDETECTOR_pre_react;
      JsonArray SOUNDDETECTOR_noiseThresholdsArray = json.createNestedArray("SOUNDDETECTOR_noiseThresholds");
      for (int i = 0; i < SOUNDDETECTOR_BANDS_WIDTH; i++) { SOUNDDETECTOR_noiseThresholdsArray.add(SOUNDDETECTOR_noiseThresholds[i]); }
      json["SOUNDDETECTOR_react"] = SOUNDDETECTOR_react;
    #endif
    json["SPECTRUM_PIXELS"] = SPECTRUM_PIXELS;
    json["SPOT_LEDS"] = SPOT_LEDS;
    json["USE_LITTLEFS"] = USE_LITTLEFS;
    json["USE_SPIFFS"] = USE_SPIFFS;
    char debugIpAddress[16] = {0};
    platformServices().formatIpAddress(debugIpAddress, sizeof(debugIpAddress));
    json["WiFi IP"] = debugIpAddress;
    #if SHELFCLOCK_STANDALONE
      json["WiFi_MAX_RETRIES"] = shelfclock::standalone::kWifiMaxRetries;
      json["WiFi_MAX_RETRY_DURATION"] = shelfclock::standalone::kWifiMaxRetryDuration;
      json["WiFi_elapsedTime"] = standaloneNetworkStatus.elapsedTime;
      json["WiFi_retryCount"] = standaloneNetworkStatus.retryCount;
      json["WiFi_startTime"] = standaloneNetworkStatus.startTime;
      json["WiFi_totalReconnections"] = standaloneNetworkStatus.totalReconnections;
    #else
      json["WiFi_MAX_RETRIES"] = 0;
      json["WiFi_MAX_RETRY_DURATION"] = 0;
      json["WiFi_elapsedTime"] = 0;
      json["WiFi_retryCount"] = 0;
      json["WiFi_startTime"] = 0;
      json["WiFi_totalReconnections"] = 0;
    #endif
    #if HAS_DHT
      json["altitudeLocal"] = altitudeLocal;
    #endif
    #if HAS_PHOTOSENSOR
      json["analogRead(PHOTORESISTER_PIN)"] = analogRead(PHOTORESISTER_PIN);
    #endif
    json["breakOutSet"] = breakOutSet;
    json["brightness"] = brightness;
    #if HAS_SOUNDDETECTOR
      json["buttonPushCounter"] = buttonPushCounter;
    #endif	
    json["clearOldLeds"] = clearOldLeds;
    json["clockDisplayType"] = clockDisplayType;
    json["clockMode"] = clockMode;
    json["colonType"] = colonType;
    json["colorWheelPosition"] = colorWheelPosition;
    json["colorWheelPositionTwo"] = colorWheelPositionTwo;
    json["colorWheelSpeed"] = colorWheelSpeed;
    json["colorchangeCD"] = colorchangeCD;
    #if HAS_SOUNDDETECTOR
      json["colorTimer"] = colorTimer;
    #endif	
    json["countdownMilliSeconds"] = countdownMilliSeconds;
    json["countupMilliSeconds"] = countupMilliSeconds;
    json["currentMode"] = currentMode;
    json["currentReal"] = currentReal;
    json["cylonPosition"] = cylonPosition;
    #if HAS_USWEATHER
      json["dailyPollSuccess"] = dailyPollSuccess;
      json["dailyPollFailed"] = dailyPollFailed;
      #endif
    json["dateDisplayType"] = dateDisplayType;
    json["daylightOffset_sec"] = daylightOffset_sec;
    json["daysUptime"] = daysUptime;
    #if HAS_BUZZER
      json["defaultAudibleAlarm"] = listOfSong[defaultAudibleAlarm];
    #endif
    json["dotsOn"] = dotsOn;
    json["endCountDownMillis"] = endCountDownMillis;
    json["fakeclockrunning"] = fakeclockrunning;
    json["fakeTime"] = fakeTime;
    #if HAS_BUZZER
      JsonArray filesArrayJson = json.createNestedArray("filesArray");
      for (int i = 0; i < 64; i++) { filesArrayJson.add(filesArray[i]); }
    #endif	
    json["foodSpot"] = foodSpot;
    json["getSlower"] = getSlower;
    json["gmtOffset_sec"] = gmtOffset_sec;
    JsonArray greenMatrixArray = json.createNestedArray("greenMatrix");
    for (int i = 0; i < SPECTRUM_PIXELS; i++) { greenMatrixArray.add(greenMatrix[i]); }
    json["host"] = host;
    json["hoursUptime"] = hoursUptime;
    #if HAS_ONLINEWEATHER || HAS_DHT || HAS_USWEATHER
      json["humiColorSettings"] = humiColorSettings;
      json["humiDisplayType"] = humiDisplayType;
    #endif
    json["isAsleep"] = isAsleep;
    #if HAS_PHOTOSENSOR
      json["lightSensorValue"] = lightSensorValue;
    #endif
    json["lightshowMode"] = lightshowMode;
    json["lightshowSpeed"] = lightshowSpeed;
    json["millis"] = millis();
  	json["minutesUptime"] = minutesUptime;
    json["ntpServer"] = ntpServer;
    #if HAS_SOUNDDETECTOR
      JsonArray oldBarHeightsArray = json.createNestedArray("oldBarHeights");
      for (unsigned int i = 0; i < sizeof(oldBarHeights) / sizeof(oldBarHeights[0]); i++) { oldBarHeightsArray.add(oldBarHeights[i]); }
    #endif	
    #if HAS_USWEATHER
      JsonArray tideHeightArray = json.createNestedArray("tideHeight");
      for (int i = 0; i < 48; i++) { tideHeightArray.add(String(tideHeight[i], 1)); }
      JsonArray tideColorArray = json.createNestedArray("tideColor");
      for (int i = 0; i < 14; i++) {
        char hexColor[7];  // 6 digits + null terminator
        sprintf(hexColor, "%06X", tideColor[i]);
        tideColorArray.add(hexColor);
      }
      json["noaaTemp"] = noaaTemp;
      json["noaaHumidity"] = noaaHumidity;
      JsonArray rainForecastArray = json.createNestedArray("rainForecast");
      int forecastBrightness[14];
      portENTER_CRITICAL(&rainForecastMux);
      memcpy(forecastBrightness, rainForecast, sizeof(forecastBrightness));
      portEXIT_CRITICAL(&rainForecastMux);
      for (int i = 0; i < 14; i++) { rainForecastArray.add(forecastBrightness[i]); }
      #endif
    #if HAS_ONLINEWEATHER || HAS_USWEATHER
      json["outdoorTemp"] = outdoorTemp;
      json["outdoorHumidity"] = outdoorHumidity;
      json["humidity_outdoor_enable"] = humidity_outdoor_enable;
      #endif
    json["pastelColors"] = pastelColors;
    #if HAS_PHOTOSENSOR
      JsonArray photoresisterReadingsArray = json.createNestedArray("photoresisterReadings");
      for (int i = 0; i < PHOTO_SAMPLES; i++) { photoresisterReadingsArray.add(String(photoresisterReadings[i])); }   
    #endif
    json["prevTime"] = prevTime;
    json["prevTime2"] = prevTime2;
    json["previousTimeDay"] = previousTimeDay;
    json["previousTimeHour"] = previousTimeHour;
    json["previousTimeMin"] = previousTimeMin;
    json["previousTimeMonth"] = previousTimeMonth;
    json["previousTimeWeek"] = previousTimeWeek;
    JsonArray rainArray = json.createNestedArray("rain");
    for (int i = 0; i < SPECTRUM_PIXELS; i++) { rainArray.add(rain[i]); }
    json["randomDayPassed"] = randomDayPassed;
    json["randomHourPassed"] = randomHourPassed;
    json["randomMinPassed"] = randomMinPassed;
    json["randomMonthPassed"] = randomMonthPassed;
    #if HAS_SOUNDDETECTOR
      json["randomSpectrumMode"] = randomSpectrumMode;
    #endif
    json["randomWeekPassed"] = randomWeekPassed;
    #if HAS_PHOTOSENSOR
      json["readIndex"] = readIndex;
    #endif
    json["realtimeMode"] = realtimeMode;
    #if HAS_SOUNDDETECTOR
      json["sampling_period_us"] = sampling_period_us; 
    #endif	
    json["scoreboardLeft"] = scoreboardLeft;
    json["scoreboardRight"] = scoreboardRight;
    json["scrollColorSettings"] = scrollColorSettings;
    json["scrollFrequency"] = scrollFrequency;
    json["scrollOptions1"] = scrollOptions1;
    json["scrollOptions2"] = scrollOptions2;
    json["scrollOptions3"] = scrollOptions3;
    json["scrollOptions4"] = scrollOptions4;
    json["scrollOptions5"] = scrollOptions5;
    json["scrollOptions6"] = scrollOptions6;
    json["scrollOptions7"] = scrollOptions7;
    json["scrollOptions8"] = scrollOptions8;
    json["scrollOptions9"] = scrollOptions9;
    json["scrollOverride"] = scrollOverride;
    json["scrollText"] = scrollText.c_str();
    json["firstRun"] = firstRun;
    json["sleepTimerCurrent"] = sleepTimerCurrent;
    json["snakeLastDirection"] = snakeLastDirection;
    json["snakePosition"] = snakePosition;
    json["snakeWaiting"] = snakeWaiting;
    #if HAS_BUZZER
      json["songTaskbuffer"] = songTaskbuffer;
      json["songTaskbufferSize"] = songTaskbufferSize;
      JsonArray songsArrayJson = json.createNestedArray("songsArray");
      for (int i = 0; i < 64; i++) { songsArrayJson.add(songsArray[i]); }
    #endif	
    #if HAS_BUZZER
      json["specialAudibleAlarm"] = listOfSong[specialAudibleAlarm];
    #endif
    #if HAS_SOUNDDETECTOR
      json["spectrumBackgroundSettings"] = spectrumBackgroundSettings;
      json["spectrumColorSettings"] = spectrumColorSettings;
      json["spectrumMode"] = spectrumMode;
    #endif
    json["spotlightsColorSettings"] = spotlightsColorSettings;
    #if HAS_USWEATHER
      json["stationID"] = stationID; 
      #endif
    json["suspendFrequency"] = suspendFrequency;
    json["suspendType"] = suspendType;
    #if HAS_ONLINEWEATHER || HAS_DHT
      json["tempColorSettings"] = tempColorSettings;
      json["tempDisplayType"] = tempDisplayType;
      json["temperatureCorrection"] = temperatureCorrection;
      json["temperatureSymbol"] = temperatureSymbol;
    #endif
    #if HAS_USWEATHER
      json["textTides"] = textTides;
      #endif
    #if HAS_BUZZER
      json["totalSongs"] = totalSongs;
    #endif	
    json["updateSettingsRequired"] = updateSettingsRequired;
    #if HAS_BUZZER
      json["useAudibleAlarm"] = useAudibleAlarm;
    #endif
    json["useSpotlights"] = useSpotlights;
    #if HAS_BUZZER
      JsonArray validDurations = json.createNestedArray("valid_durations");
      for (unsigned int i = 0; i < sizeof(valid_durations) / sizeof(valid_durations[0]); i++) { validDurations.add(valid_durations[i]); }
      JsonArray validOctaves = json.createNestedArray("valid_octaves");
      for (unsigned int i = 0; i < sizeof(valid_octaves) / sizeof(valid_octaves[0]); i++) { validOctaves.add(valid_octaves[i]); }
      JsonArray validBeats = json.createNestedArray("valid_beats");
      for (unsigned int i = 0; i < sizeof(valid_beats) / sizeof(valid_beats[0]); i++) { validBeats.add(valid_beats[i]); }
    #endif	
    #if HAS_ONLINEWEATHER
      json["apikey"] = apikey;
    #endif	
    #if HAS_ONLINEWEATHER || HAS_USWEATHER
      json["latitude"] = latitude;
      json["longitude"] = longitude;
      json["temperature_outdoor_enable"] = temperature_outdoor_enable;
    #endif	
      json["countdownColorValue"] = countdownColorValue;
      json["spectrumColorValue"] = spectrumColorValue;
      json["spectrumBackgroundColorValue"] = spectrumBackgroundColorValue;
      json["hourColorValue"] = hourColorValue;
      json["colonColorValue"] = colonColorValue;
      json["minColorValue"] = minColorValue;
      json["spotlightsColorValue"] = spotlightsColorValue;
      json["scrollColorValue"] = scrollColorValue;
      json["humiColorValue"] = humiColorValue;
      json["humiSymbolColorValue"] = humiSymbolColorValue;
      json["humiDecimalColorValue"] = humiDecimalColorValue;
      json["scoreboardColorLeftValue"] = scoreboardColorLeftValue;
      json["scoreboardColorRightValue"] = scoreboardColorRightValue;
      json["tempColorValue"] = tempColorValue;
      json["typeColorValue"] = typeColorValue;
      json["degreeColorValue"] = degreeColorValue;
      json["dayColorValue"] = dayColorValue;
      json["monthColorValue"] = monthColorValue;  
      json["separatorColorValue"] = separatorColorValue;

    serializeJson(json, output);
    server.send(200, "application/json", output);
  });

  server.on("/network-reset", HTTP_POST, []() {
    shelfclock::runtime::requestNetworkRecovery();
    server.send(202, "application/json", "{\"result\":\"restarting\"}");
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(firmwareUpdateSucceeded ? 200 : 400, "text/plain",
                firmwareUpdateSucceeded ? "Update installed. Restarting..." : "Update failed");
    if (firmwareUpdateSucceeded) {
      firmwareRestartRequested = true;
      firmwareRestartAt = millis() + 1000;
    }
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      const String filename = upload.filename;
      firmwareUpdateSucceeded = false;
      firmwareUploadInProgress = true;
      firmwareUploadAccepted = filename == "firmware.bin" || filename.endsWith(".ota.bin") ||
                               filename.endsWith("-OTA-Update.bin") ||
                               filename.endsWith("-ESPHome-OTA-Update.bin") ||
                               filename.endsWith("-OTA-ESPHome.bin");
      if (!firmwareUploadAccepted) {
        firmwareUploadInProgress = false;
        Serial.printf("Rejected non-OTA image: %s\n", filename.c_str());
        return;
      }
      Serial.printf("Firmware update: %s\n", filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        firmwareUploadAccepted = false;
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (firmwareUploadAccepted &&
          Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          firmwareUploadAccepted = false;
          Update.printError(Serial);
      }
      platformServices().cooperateDuringSetup();
    } else if (upload.status == UPLOAD_FILE_END) {
      if (firmwareUploadAccepted) {
        firmwareUpdateSucceeded = Update.end(true);
      } else {
        Update.abort();
        firmwareUpdateSucceeded = false;
      }
      firmwareUploadInProgress = false;
      if (firmwareUpdateSucceeded) {
        Serial.printf("Update Success: %u\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
      firmwareUploadAccepted = false;
      firmwareUploadInProgress = false;
      Serial.println("Firmware update aborted");
    }
  });



#if HAS_BUZZER
  /*handling uploading song file */
server.on("/uploadSong", HTTP_POST, []() {
    // Send response after upload finished, based on status set in upload handler
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (lastSongUploadOk) {
      DynamicJsonDocument doc(128);
      doc["error"] = 0;
      doc["message"] = lastSongUploadMessage.length() ? lastSongUploadMessage : "Upload Complete";
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    } else {
      DynamicJsonDocument doc(128);
      doc["error"] = 1;
      doc["message"] = lastSongUploadMessage.length() ? lastSongUploadMessage : "Upload failed";
      String out;
      serializeJson(doc, out);
      server.send(500, "application/json", out);
    }
}, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        lastSongUploadOk = false;
        lastSongUploadMessage = "";
        lastSongUploadPath[0] = '\0';
        fsUploadFile = File();
        const char* filename = upload.filename.c_str();
        char fullFilename[256];
        int pathLength = 0;
        if (filename[0] != '/') {
            pathLength = snprintf(fullFilename, sizeof(fullFilename), "/songs/%s", filename);
        } else {
            pathLength = strlen(filename);
            if (pathLength < (int)sizeof(fullFilename)) {
                strncpy(fullFilename, filename, sizeof(fullFilename));
                fullFilename[sizeof(fullFilename) - 1] = '\0';
            }
        }
        if (pathLength < 0 || pathLength >= (int)sizeof(fullFilename)) {
            Serial.println("Upload filename too long");
            lastSongUploadOk = false;
            lastSongUploadMessage = "Upload filename too long";
            return;
        }
        strncpy(lastSongUploadPath, fullFilename, sizeof(lastSongUploadPath) - 1);
        lastSongUploadPath[sizeof(lastSongUploadPath) - 1] = '\0';
        Serial.print("Upload Name: ");
        Serial.println(fullFilename);
        fsUploadFile = FileFS.open(fullFilename, "w");
        if (!fsUploadFile) {
            Serial.println("Failed to open file for writing");
            lastSongUploadOk = false;
            lastSongUploadMessage = "Failed to open file";
        } else {
            Serial.println("Upload started");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Serial.print(".");
        if (fsUploadFile) {
            fsUploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.println("");
        if (fsUploadFile) {
            fsUploadFile.close();
            Serial.print("Upload Size: ");
            Serial.println(upload.totalSize);
            getListOfSongs();
            if (FileFS.exists(lastSongUploadPath) && songPathLoaded(lastSongUploadPath)) {
                lastSongUploadOk = true;
                lastSongUploadMessage = "Upload Complete";
            } else {
                lastSongUploadOk = false;
                lastSongUploadMessage = "Uploaded RTTTL was rejected";
            }
        } else {
            lastSongUploadOk = false;
            if (!lastSongUploadMessage.length()) {
                lastSongUploadMessage = "Upload failed";
            }
        }
    }
});


  server.on("/deleteSong", HTTP_POST, []() {
    DynamicJsonDocument json(1500);
      Serial.print("Upload Size: "); Serial.println(server.args());
    String body;
    if(readLoggedPostJson(json, body)) {
      if (!json["song"].isNull()) {
        deleteRequestedSong(json["song"]);
      }
      server.send(200, "text/json", "{\"result\":\"ok\"}");
      return;
    } 
    server.send(401);
  });
#endif

}
