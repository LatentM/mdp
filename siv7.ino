// ===========================================
// SIWSA PREMIUM - SMART AGRICULTURE 4.0
// Final Core: Hysteresis Logic & Custom Crops
// ===========================================

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include <RTClib.h>
#include <Adafruit_MLX90614.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define DEVICE_NAME "SIWSA-Premium"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Pins
#define SOIL_MOISTURE_PIN 34
#define UV_SENSOR_PIN 35
#define RAIN_SENSOR_PIN 36
#define DHT22_PIN 25
#define DS18B20_PIN 26
#define US1_TRIG 5
#define US1_ECHO 27
#define LIMIT_OPEN_PIN 13    
#define LIMIT_CLOSE_PIN 14   
#define TFT_CS 15
#define TFT_RST 4
#define TFT_DC 32
#define TFT_MOSI 23
#define TFT_SCK 18
#define TFT_LED_PIN 16 
#define I2C_SDA 21
#define I2C_SCL 22

// Actuators
#define PUMP_PIN 19        
#define FAN_PIN 33         
#define SHADE_IN1 2        
#define SHADE_IN2 12       

// Dual-Voltage PWM
#define SHADE_PWM_SPEED 200  
#define PUMP_PWM_SPEED 60   

// Objects
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
LiquidCrystal_I2C lcd(0x27, 16, 2);  
DHT dht(DHT22_PIN, DHT22);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
BH1750 lightMeter;
Adafruit_BMP280 bmp;
RTC_DS3231 rtc;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false, oldDeviceConnected = false;

// --- EXPANDED CROP THRESHOLDS (MIN & MAX) ---
String currentCrop = "Tomato";
float th_light = 50000.0, th_light_min = 15000.0;
float th_airTemp = 29.0, th_airTemp_min = 18.0;
float th_hum = 80.0, th_hum_min = 50.0;
float th_soilMoist = 80.0, th_soilMoist_min = 40.0;

struct SensorData {
  float temperatureDHT = 0, humidity = 0, temperatureDS18B20 = 0, soilMoisture = 0;
  float lightLux = 0, uvIntensity = 0, pressure = 0, altitude = 0;
  float temperatureMLXAmbient = 0, temperatureMLXObject = 0, rainLevel = 0; 
  bool rainDetected = false;
  float vpd = 0, dewPoint = 0; 
  int hour = 0, minute = 0, second = 0, day = 0, month = 0, year = 0;
};
SensorData sensorData;

bool screensActive = true;
unsigned long lastPresenceTime = 0;
const unsigned long SCREEN_TIMEOUT = 10000;
bool firstDraw = true;

bool isSystemAuto = true, isPumpOn = false, isFanOn = false;
enum ShadeState { SHADE_OPEN, SHADE_CLOSED, SHADE_MOVING };
ShadeState currentShadeState = SHADE_OPEN; 
int targetShadeState = SHADE_OPEN;
unsigned long shadeMoveStartTime = 0;
unsigned long lastUpdate = 0, lastLCDChange = 0;
int lcdScreen = 0;
bool bmpFound = false, bh1750Found = false, rtcFound = false, mlxFound = false;

// Declarations
void initBLE(); void initGPIO(); void initI2C();
void initDisplays(); void initSensors();
void readAllSensors(); void handleAutomation(); void updateDisplays(); String createJSONData();
void manageScreens(); float getDistance();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("✅ BLE Connected"); }
    void onDisconnect(BLEServer* pServer) { deviceConnected = false; delay(500); pServer->startAdvertising(); }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      String cmd = String(pChar->getValue().c_str());
      cmd.trim();
      
      if (cmd == "getdata") { 
        String jsonData = createJSONData();
        // FIXED: Explicit length to prevent JSON truncation
        pChar->setValue((uint8_t*)jsonData.c_str(), jsonData.length()); 
        pChar->notify(); 
      }
      else if (cmd == "auto") isSystemAuto = true;
      else if (cmd == "manual") isSystemAuto = false;
      else if (cmd.startsWith("CROP|")) {
        // Parse the new 10-part format: CROP|Name|MaxL|MinL|MaxT|MinT|MaxH|MinH|MaxS|MinS
        int p1 = cmd.indexOf('|');
        int p2 = cmd.indexOf('|', p1 + 1);
        int p3 = cmd.indexOf('|', p2 + 1);
        int p4 = cmd.indexOf('|', p3 + 1);
        int p5 = cmd.indexOf('|', p4 + 1);
        int p6 = cmd.indexOf('|', p5 + 1);
        int p7 = cmd.indexOf('|', p6 + 1);
        int p8 = cmd.indexOf('|', p7 + 1);
        int p9 = cmd.indexOf('|', p8 + 1);
        if (p9 != -1) {
          currentCrop = cmd.substring(p1 + 1, p2);
          th_light = cmd.substring(p2 + 1, p3).toFloat();
          th_light_min = cmd.substring(p3 + 1, p4).toFloat();
          th_airTemp = cmd.substring(p4 + 1, p5).toFloat();
          th_airTemp_min = cmd.substring(p5 + 1, p6).toFloat();
          th_hum = cmd.substring(p6 + 1, p7).toFloat();
          th_hum_min = cmd.substring(p7 + 1, p8).toFloat();
          th_soilMoist = cmd.substring(p8 + 1, p9).toFloat();
          th_soilMoist_min = cmd.substring(p9 + 1).toFloat();
        }
      }
      else if (!isSystemAuto) {
        if (cmd == "pump_on") { isPumpOn = true; analogWrite(PUMP_PIN, PUMP_PWM_SPEED); } 
        else if (cmd == "pump_off") { isPumpOn = false; analogWrite(PUMP_PIN, 0); }
        else if (cmd == "fan_on") { isFanOn = true; digitalWrite(FAN_PIN, LOW); } 
        else if (cmd == "fan_off") { isFanOn = false; digitalWrite(FAN_PIN, HIGH); }
        else if (cmd == "shade_close" && currentShadeState != SHADE_CLOSED) {
          analogWrite(SHADE_IN1, SHADE_PWM_SPEED);
          analogWrite(SHADE_IN2, 0); 
          currentShadeState = SHADE_MOVING; targetShadeState = SHADE_CLOSED; shadeMoveStartTime = millis();
        }
        else if (cmd == "shade_open" && currentShadeState != SHADE_OPEN) {
          analogWrite(SHADE_IN1, 0);
          analogWrite(SHADE_IN2, SHADE_PWM_SPEED); 
          currentShadeState = SHADE_MOVING; targetShadeState = SHADE_OPEN; shadeMoveStartTime = millis();
        }
      }
    }
};

void setup() {
  Serial.begin(115200); delay(1000);
  initI2C(); initGPIO(); initDisplays();
  initSensors(); initBLE();
  readAllSensors(); 
  lastPresenceTime = millis(); 
  updateDisplays();
}

void initBLE() {
  BLEDevice::init(DEVICE_NAME); pServer = BLEDevice::createServer(); pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks()); pCharacteristic->addDescriptor(new BLE2902());
  pService->start(); BLEDevice::startAdvertising();
}

void initGPIO() {
  pinMode(US1_TRIG, OUTPUT); pinMode(US1_ECHO, INPUT);
  pinMode(LIMIT_OPEN_PIN, INPUT_PULLUP); pinMode(LIMIT_CLOSE_PIN, INPUT_PULLUP);
  pinMode(PUMP_PIN, OUTPUT); pinMode(FAN_PIN, OUTPUT); pinMode(SHADE_IN1, OUTPUT); pinMode(SHADE_IN2, OUTPUT);
  pinMode(TFT_LED_PIN, OUTPUT); digitalWrite(TFT_LED_PIN, HIGH); 
  analogWrite(PUMP_PIN, 0); digitalWrite(FAN_PIN, HIGH); analogWrite(SHADE_IN1, 0); analogWrite(SHADE_IN2, 0);
}

void initI2C() { Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000); }
void initDisplays() { tft.initR(INITR_BLACKTAB); tft.setRotation(3); lcd.init(); lcd.backlight(); }

void initSensors() {
  dht.begin(); ds18b20.begin(); ds18b20.setWaitForConversion(false); ds18b20.requestTemperatures();
  if (rtc.begin()) { 
    rtcFound = true; DateTime compiledTime = DateTime(F(__DATE__), F(__TIME__));
    if (rtc.lostPower() || rtc.now().unixtime() < compiledTime.unixtime()) { rtc.adjust(compiledTime); Serial.println("⌚ RTC Force-Synced!");} 
  }
  if (bmp.begin(0x76) || bmp.begin(0x77)) bmpFound = true;
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) bh1750Found = true;
  if (mlx.begin()) mlxFound = true;
}

float getDistance() {
  digitalWrite(US1_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(US1_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(US1_TRIG, LOW);
  long duration = pulseIn(US1_ECHO, HIGH, 30000); 
  if (duration == 0) return 999; return (duration * 0.0343) / 2.0;
}

void readAllSensors() {
  sensorData.temperatureDHT = dht.readTemperature(); sensorData.humidity = dht.readHumidity();
  if (isnan(sensorData.temperatureDHT)) sensorData.temperatureDHT = 0; if (isnan(sensorData.humidity)) sensorData.humidity = 0;
  sensorData.temperatureDS18B20 = ds18b20.getTempCByIndex(0); ds18b20.requestTemperatures(); 
  if (mlxFound) { sensorData.temperatureMLXAmbient = mlx.readAmbientTempC(); sensorData.temperatureMLXObject = mlx.readObjectTempC(); }
  sensorData.soilMoisture = constrain(map(analogRead(SOIL_MOISTURE_PIN), 4095, 1000, 0, 100), 0, 100);
  sensorData.uvIntensity = analogRead(UV_SENSOR_PIN) * (3.3 / 4095.0) * 10.0;
  float es = 0.6108 * exp((17.27 * sensorData.temperatureDHT) / (sensorData.temperatureDHT + 237.3));
  sensorData.vpd = es - (es * (sensorData.humidity / 100.0));
  float a = 17.271; float b = 237.7;
  float gamma = (a * sensorData.temperatureDHT / (b + sensorData.temperatureDHT)) + log(sensorData.humidity / 100.0);
  sensorData.dewPoint = (b * gamma) / (a - gamma);
  
  sensorData.rainLevel = constrain(map(analogRead(RAIN_SENSOR_PIN), 4095, 1000, 0, 100), 0, 100);
  sensorData.rainDetected = (sensorData.rainLevel > 10);
  if (bh1750Found) sensorData.lightLux = max(0.0f, lightMeter.readLightLevel());
  if (bmpFound) { sensorData.pressure = bmp.readPressure() / 1000.0;
  sensorData.altitude = bmp.readAltitude(1013.25); }
  if (rtcFound) { DateTime now = rtc.now(); sensorData.hour = now.hour(); sensorData.minute = now.minute();
  sensorData.second = now.second(); sensorData.day = now.day(); sensorData.month = now.month(); sensorData.year = now.year(); }
}

void handleAutomation() {
  if (!isSystemAuto) return;
  
  // 1. IRRIGATION PUMP HYSTERESIS
  // Turn on if completely dry.
  // Do not turn off until max moisture is reached.
  if (sensorData.soilMoisture < th_soilMoist_min) {
    isPumpOn = true;
  } else if (sensorData.soilMoisture > th_soilMoist) {
    isPumpOn = false;
  }
  analogWrite(PUMP_PIN, isPumpOn ? PUMP_PWM_SPEED : 0);

  // 2. VENTILATION FAN HYSTERESIS
  // Turn on if extreme heat or humidity.
  // Only turn off when BOTH have cooled down to minimum safe levels.
  if (sensorData.temperatureDHT > th_airTemp || sensorData.humidity > th_hum) {
    isFanOn = true;
  } else if (sensorData.temperatureDHT < th_airTemp_min && sensorData.humidity < th_hum_min) {
    isFanOn = false;
  }
  digitalWrite(FAN_PIN, isFanOn ? LOW : HIGH);

  // 3. SHADE NET LOGIC
  int wantShade = currentShadeState;
  // Default to staying where it is
  
  // Danger! Too bright or too hot.
  // Close the net to protect crops.
  if (sensorData.lightLux > th_light || sensorData.temperatureDHT > th_airTemp) {
    wantShade = SHADE_CLOSED;
  } 
  // Safe zone or Nighttime. Open the net to allow fresh air/light/dew.
  else if (sensorData.lightLux < th_light_min || sensorData.temperatureDHT < th_airTemp_min || sensorData.lightLux < 20) {
    wantShade = SHADE_OPEN;
  }

  // Execute Shade Net Movement
  if (currentShadeState != SHADE_MOVING) {
    if (wantShade == SHADE_CLOSED && currentShadeState == SHADE_OPEN) {
      analogWrite(SHADE_IN1, SHADE_PWM_SPEED);
      analogWrite(SHADE_IN2, 0); 
      currentShadeState = SHADE_MOVING; targetShadeState = SHADE_CLOSED; shadeMoveStartTime = millis();
    } else if (wantShade == SHADE_OPEN && currentShadeState == SHADE_CLOSED) {
      analogWrite(SHADE_IN1, 0); analogWrite(SHADE_IN2, SHADE_PWM_SPEED);
      currentShadeState = SHADE_MOVING; targetShadeState = SHADE_OPEN; shadeMoveStartTime = millis();
    }
  }
}

void manageScreens() {
  float distance = getDistance();
  if (distance > 0 && distance < 80.0) lastPresenceTime = millis();
  bool shouldBeOn = (millis() - lastPresenceTime < SCREEN_TIMEOUT);
  if (shouldBeOn && !screensActive) {
    digitalWrite(TFT_LED_PIN, HIGH); lcd.display(); lcd.backlight();   
    screensActive = true; firstDraw = true;
  } else if (!shouldBeOn && screensActive) {
    digitalWrite(TFT_LED_PIN, LOW); lcd.noBacklight(); lcd.noDisplay();   
    screensActive = false;
  }
}

String createJSONData() {
  String j = "{";
  j += "\"crop\":\"" + currentCrop + "\",\"time\":\"" + String(sensorData.hour) + ":" + String(sensorData.minute) + ":" + String(sensorData.second) + "\",";
  j += "\"date\":\"" + String(sensorData.day) + "/" + String(sensorData.month) + "/" + String(sensorData.year) + "\",";
  j += "\"temperature\":" + String(sensorData.temperatureDHT, 1) + ",\"humidity\":" + String(sensorData.humidity, 0) + ",";
  j += "\"water_temp\":" + String(sensorData.temperatureDS18B20, 1) + ",\"ir_temp\":" + String(sensorData.temperatureMLXObject, 1) + ",";
  j += "\"soil_moisture\":" + String(sensorData.soilMoisture, 0) + ",\"light\":" + String(sensorData.lightLux, 0) + ",";
  j += "\"pressure\":" + String(sensorData.pressure, 2) + ",\"rain\":" + String(sensorData.rainLevel, 0) + ",";
  j += "\"uv\":" + String(sensorData.uvIntensity, 2) + ",\"vpd\":" + String(sensorData.vpd, 2) + ",\"dew_point\":" + String(sensorData.dewPoint, 1) + ",";
  j += "\"mode\":\"" + String(isSystemAuto ? "AUTO" : "MANUAL") + "\",\"pump_status\":\"" + String(isPumpOn ? "ON" : "OFF") + "\",";
  j += "\"fan_status\":\"" + String(isFanOn ? "ON" : "OFF") + "\",\"shade_status\":\"" + String(currentShadeState == SHADE_OPEN ? "OPEN" : (currentShadeState == SHADE_CLOSED ? "CLOSED" : "MOVING")) + "\"}";
  return j;
}

void updateDisplays() {
  if (!screensActive) return; 

  if (firstDraw) {
    tft.fillScreen(0x0000); 
    tft.drawFastHLine(0, 16, 160, 0x5AEB);
    tft.drawFastHLine(0, 34, 160, 0x5AEB); tft.drawFastHLine(0, 106, 160, 0x5AEB); 
    firstDraw = false;
  }

  tft.setTextSize(1);
  tft.setTextColor(0x07FF, 0x0000); tft.setCursor(2, 4);
  tft.print("SIWSA OS v4.0");
  tft.setTextColor(deviceConnected ? 0x07E0 : 0xF800, 0x0000); tft.setCursor(105, 4); tft.print(deviceConnected ? "BLE: ON " : "BLE: OFF");
  tft.setTextColor(0xFFFF, 0x0000); tft.setCursor(2, 22);
  char timeStr[30]; sprintf(timeStr, "%02d/%02d %02d:%02d | %-7s", sensorData.day, sensorData.month, sensorData.hour, sensorData.minute, currentCrop.c_str());
  tft.print(timeStr);

  tft.setTextColor(0xFFE0, 0x0000);
  tft.setCursor(2, 40); tft.printf("Air: %4.1fC", sensorData.temperatureDHT);
  tft.setCursor(85, 40); tft.printf("Hum: %3.0f%%", sensorData.humidity);

  tft.setTextColor(0x07E0, 0x0000); tft.setCursor(2, 56); tft.printf("Wat: %4.1fC", sensorData.temperatureDS18B20);
  tft.setCursor(85, 56);
  tft.printf("SM:  %3.0f%%", sensorData.soilMoisture);

  tft.setTextColor(0xF81F, 0x0000); tft.setCursor(2, 72); tft.printf("Lux:%6.0f", sensorData.lightLux);
  tft.setTextColor(0x001F, 0x0000); tft.setCursor(85, 72); tft.printf("Rain:%3.0f%%", sensorData.rainLevel);
  
  tft.setTextColor(0xFCA0, 0x0000); tft.setCursor(2, 88);
  tft.printf("UV: %4.1f ", sensorData.uvIntensity);
  tft.setTextColor(0x07FF, 0x0000); tft.setCursor(85, 88); tft.printf("VPD:%4.1f", sensorData.vpd);

  tft.setTextColor(0xFFFF, 0x0000); tft.setCursor(2, 114); tft.print("Pmp:");
  tft.setTextColor(isPumpOn ? 0x07E0 : 0xF800, 0x0000); tft.print(isPumpOn ? "ON " : "OFF");

  tft.setTextColor(0xFFFF, 0x0000); tft.setCursor(55, 114); tft.print("Fan:");
  tft.setTextColor(isFanOn ? 0x07E0 : 0xF800, 0x0000); tft.print(isFanOn ? "ON " : "OFF");

  tft.setTextColor(0xFFFF, 0x0000); tft.setCursor(105, 114); tft.print("Net:");
  uint16_t shadeColor = (currentShadeState == SHADE_MOVING) ? 0xFFE0 : ((currentShadeState == SHADE_OPEN) ? 0x07E0 : 0xF800);
  String shadeTxt = (currentShadeState == SHADE_MOVING) ? "MOV" : ((currentShadeState == SHADE_OPEN) ? "OPN" : "CLS");
  tft.setTextColor(shadeColor, 0x0000); tft.print(shadeTxt);
  
  if (millis() - lastLCDChange > 5000) { lastLCDChange = millis(); lcdScreen = !lcdScreen; }
  lcd.clear();
  if (lcdScreen == 0) { 
    lcd.setCursor(0, 0); lcd.printf("T:%.1fC H:%.0f%%", sensorData.temperatureDHT, sensorData.humidity); 
    lcd.setCursor(0, 1); lcd.printf("Soil:%.0f%%", sensorData.soilMoisture);
  } else { 
    lcd.setCursor(0, 0); lcd.printf("VPD:%.1f kPa", sensorData.vpd); 
    lcd.setCursor(0, 1); lcd.printf("Mode:%s", isSystemAuto ? "AUTO" : "MANUAL");
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentShadeState == SHADE_MOVING) {
    bool stopMotor = false;
    if (currentMillis - shadeMoveStartTime > 500) {
      if (targetShadeState == SHADE_OPEN && digitalRead(LIMIT_OPEN_PIN) == LOW) {
        delay(30);
        if (digitalRead(LIMIT_OPEN_PIN) == LOW) stopMotor = true; 
      }
      else if (targetShadeState == SHADE_CLOSED && digitalRead(LIMIT_CLOSE_PIN) == LOW) {
        delay(30);
        if (digitalRead(LIMIT_CLOSE_PIN) == LOW) stopMotor = true; 
      }
    }

    if (currentMillis - shadeMoveStartTime >= 8000) stopMotor = true;
    if (stopMotor) {
      analogWrite(SHADE_IN1, 0); analogWrite(SHADE_IN2, 0);
      currentShadeState = (ShadeState)targetShadeState;
    }
  }
  
  if (currentMillis - lastUpdate >= 2000) {
    lastUpdate = currentMillis; 
    readAllSensors();
    manageScreens(); 
    handleAutomation(); 
    updateDisplays();
    if (deviceConnected) { 
      String jsonData = createJSONData(); 
      // FIXED: Explicit length to prevent JSON truncation
      pCharacteristic->setValue((uint8_t*)jsonData.c_str(), jsonData.length()); 
      pCharacteristic->notify();
    }
  }
  
  if (!deviceConnected && oldDeviceConnected) { delay(500); oldDeviceConnected = deviceConnected; }
  if (deviceConnected && !oldDeviceConnected) { oldDeviceConnected = deviceConnected; }
}