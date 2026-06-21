#include <WiFi.h>
#include <WiFiManager.h>          // 🔹 ADDED
#include <Firebase_ESP_Client.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

/********* FIREBASE *********/
#define API_KEY         "AIzaSyDaZqZPV6XjNK5cqLpArysfYikZMOnfdgQ"
#define DATABASE_URL    "https://iot-water-quality-a3f25-default-rtdb.firebaseio.com/"

#define USER_EMAIL     "smaity2358@gmail.com"
#define USER_PASSWORD  "12345678"

/********* PINS *********/
#define TDS_PIN        39
#define TURBIDITY_PIN  36
#define ONE_WIRE_BUS   32
#define BUZZER_PIN     4
#define PH_PIN 34

/* ========= OBJECTS ========= */
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, 16, 2);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiManager wm;   // 🔹 WiFiManager object

/* ========= CALIBRATION ========= */
float tdsFactor = 1.0;
float turbOffset = 0.0;
float tempOffset = 0.0;
float phOffset = 0.0;

/* ========= TIMING ========= */
unsigned long lastCalibRead = 0;
unsigned long lastSend = 0;

/* ========= SETUP ========= */
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Water Quality");

  /* ===== WiFiManager ===== */
  lcd.setCursor(0,1);
  lcd.print("WiFi Config...");

  wm.setConfigPortalTimeout(180); // 3 minutes

  bool res = wm.autoConnect("WaterQuality_AP", "12345678");

  if (!res) {
    lcd.clear();
    lcd.print("WiFi Failed");
    delay(3000);
    ESP.restart();
  }

  lcd.clear();
  lcd.print("WiFi Connected");

  Serial.println(WiFi.localIP());

  /* ===== Firebase config ===== */
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  sensors.begin();
}

/* ========= READ CALIBRATION ========= */
void readCalibration() {
  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/tdsFactor"))
    tdsFactor = fbdo.floatData();

  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/turbOffset"))
    turbOffset = fbdo.floatData();

  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/tempOffset"))
    tempOffset = fbdo.floatData();

  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/phOffset"))
  phOffset = fbdo.floatData();
}

/* ========= LOOP ========= */
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Lost!");
}

  /* Read calibration every 20s */
  if (millis() - lastCalibRead > 20000) {
    readCalibration();
    lastCalibRead = millis();
  }

  /* Read & send data every 5s */
  if (millis() - lastSend > 5000 && Firebase.ready()) {

    /* --- RAW SENSOR READINGS --- */
    int tdsADC = analogRead(TDS_PIN);
    int turbADC = analogRead(TURBIDITY_PIN);

    sensors.requestTemperatures();
    float tempRaw = sensors.getTempCByIndex(0);

    float voltage = tdsADC * 3.3 / 4095.0;
    float tdsRaw = (133.42 * voltage * voltage * voltage
               - 255.86 * voltage * voltage
               + 857.39 * voltage) * 0.5;
    
    
    float turbVoltage = turbADC * 3.3 / 4095.0;
    float turbRaw = map(turbVoltage * 100, 0, 330, 3000, 0) / 1000.0;

    int phSum = 0;

for(int i=0; i<10; i++){
  phSum += analogRead(PH_PIN);
  delay(10);
}

float phADC = phSum / 10.0;

float phVoltage = phADC * 3.3 / 4095.0;

float phValue = 7 + ((2.5 - phVoltage) / 0.18);

phValue += phOffset;

    /* --- CALIBRATED VALUES --- */
    float tdsCal = tdsRaw * tdsFactor;
    float turbCal = turbRaw + turbOffset;
    float tempCal = tempRaw + tempOffset;

    /* --- LCD DISPLAY --- */
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("pH:");
    lcd.print(phValue,1);

    lcd.setCursor(9,0);
    lcd.print("TDS:");
    lcd.print((int)tdsCal);
    //lcd.print("ppm");

    lcd.setCursor(0,1);
    lcd.print("T:");
    lcd.print(tempCal,1);
    lcd.print((char)223);
    lcd.print("C");

    /* --- BUZZER ALERT --- */
    if (tdsCal > 500 || turbCal > 5) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    if (phValue < 6.5 || phValue > 8.5) {
  digitalWrite(BUZZER_PIN, HIGH);
}

    /* --- FIREBASE UPLOAD --- */
FirebaseJson json;

json.set("raw/tds", tdsRaw);
json.set("raw/turbidity", turbRaw);
json.set("raw/temperature", tempRaw);

json.set("calibrated/tds", tdsCal);
json.set("calibrated/turbidity", turbCal);
json.set("calibrated/temperature", tempCal);

json.set("raw/ph", phVoltage);
json.set("calibrated/ph", phValue);

json.set("timestamp", millis());

if (Firebase.RTDB.setJSON(&fbdo, "/waterQuality/live", &json)) {
  Serial.println("Data Uploaded");
} else {
  Serial.println("Upload Failed");
  Serial.println(fbdo.errorReason());
}
}
}
