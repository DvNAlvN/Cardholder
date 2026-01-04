#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL6hGXuo6yR"
#define BLYNK_TEMPLATE_NAME "Smart Card Holder IoT"
#define BLYNK_AUTH_TOKEN "YFvr61CB0_39cGn9hjLouZLmj_6p077P"

char auth[] = "YFvr61CB0_39cGn9hjLouZLmj_6p077P";
char ssid[] = "Open";     
char pass[] = "datamining";

#define BOT_TOKEN "8599863562:AAE8HyQzbGchxFBh0gsvzYKeBy8EQ5trf5Q" 
#define CHAT_ID "5101667452"

String targetName = "HP_DAVIN3"; 

float w_card     = 0.092792;
float w_rssi     = -0.245453;
float w_delta    = 0.989172;
float w_duration = 1.173964;
float bias       = -43.915385;

#define BUZZER_PIN 9   
#define SWITCH_PIN 10   
#define A9G_RX_PIN 20  
#define A9G_TX_PIN 21  

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <BlynkSimpleEsp32.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <math.h> 
#include <HardwareSerial.h> 

NimBLEScan* pBLEScan;
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

HardwareSerial A9GSerial(1);

int currentRSSI = -100;      
int lastRSSI = -100;         
bool findMyMode = false;     
float threatProb = 0.0;      
float rssiDelta = 0.0; 

double realLat = 0.0;
double realLon = 0.0;
String latStr = ""; 
String lonStr = "";

unsigned long lastLogicTime = 0;
unsigned long anomalyStartTime = 0; 
unsigned long lastGpsCheck = 0; 
bool isAnomalyActive = false;       

bool telegramSent = false;          
bool warningNotifSent = false;      
unsigned long lastCriticalNotif = 0; 

BLYNK_WRITE(V11) { 
  findMyMode = param.asInt();
  Serial.print("MODE FIND MY: ");
  Serial.println(findMyMode);
}

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
      String foundName = advertisedDevice->getName().c_str();
      Serial.print("Found Name : ");
      Serial.println(foundName);
      if (foundName == targetName) {
        lastRSSI = currentRSSI; 
        currentRSSI = advertisedDevice->getRSSI();
      }
    }
};

float hitungProbabilitasAI(int hardwareSwitch, int rssi, int last_rssi, float durationSec) {
  float x_card = (hardwareSwitch == HIGH) ? 1.0 : 0.0; 
  float x_rssi = (float)rssi;
  rssiDelta = abs(x_rssi - (float)last_rssi); 
  float x_duration = durationSec;

  float z = (x_card * w_card) + (x_rssi * w_rssi) + (rssiDelta * w_delta) + (x_duration * w_duration) + bias;     
  return 1.0 / (1.0 + exp(-z));
}

void updateLokasiA9G() {
  while(A9GSerial.available()) A9GSerial.read(); 
  A9GSerial.println("AT+LOCATION=2"); 
  
  long start = millis();
  String respon = "";
  while(millis() - start < 2000) { 
    while(A9GSerial.available()) {
      char c = A9GSerial.read();
      respon += c;
    }
  }
  
  if (respon.length() > 5 && respon.indexOf("GPS NOT") == -1) {
    respon.trim(); 
    int koma = respon.indexOf(',');
    if (koma > 0) {
      String rawLat = respon.substring(0, koma);
      String rawLon = respon.substring(koma + 1);
      
      int firstDigit = -1;
      for(int i=0; i<rawLat.length(); i++) {
        if(isdigit(rawLat[i]) || rawLat[i] == '-') { firstDigit = i; break; }
      }
      
      if(firstDigit != -1) {
          latStr = rawLat.substring(firstDigit);
          lonStr = rawLon;
          realLat = latStr.toDouble();
          realLon = lonStr.toDouble();
      }
    }
  }
}

void kirimTelegramBahaya() {
  String pesan = "PERINGATAN! MALING TERDETEKSI!\n\n";
  pesan += "Kartu dicabut & Sinyal HP menjauh.\n";
  
  if (realLat != 0.0 && realLon != 0.0) {
    String googleMapsLink = "https://www.google.com/maps/place/" + String(realLat, 6) + "," + String(realLon, 6);
    pesan += "Lokasi Terkini (A9G):\n" + googleMapsLink;
  } else {
    pesan += "Lokasi: Menunggu Sinyal Satelit... (Bawa ke luar ruangan)";
  }
  
  if (bot.sendMessage(CHAT_ID, pesan, "")) {
    Serial.println(">> Telegram Terkirim!");
  } else {
    Serial.println(">> Gagal kirim Telegram (Cek Koneksi)");
  }
}

void setup() {
  Serial.begin(115200);

  A9GSerial.begin(115200, SERIAL_8N1, A9G_RX_PIN, A9G_TX_PIN);
  delay(1000);
  
  Serial.println("Mengaktifkan GPS A9G...");
  A9GSerial.println("AT+GPS=1");    
  delay(1000);
  A9GSerial.println("AT+GPSRD=0");  
  delay(1000);

  Serial.println("Menghubungkan ke Blynk & WiFi...");
  Blynk.begin(auth, ssid, pass);
  Serial.println("ONLINE!");

  client.setInsecure(); 

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);

  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan(); 
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); 
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  Serial.println("SYSTEM READY: WIFI + GPS AT MODE!");
}

void loop() {
  Blynk.run();

  if(!pBLEScan->isScanning()){
    pBLEScan->start(1, false);
    pBLEScan->clearResults();
  }

  if (millis() - lastLogicTime > 1000) {
    
    if (threatProb > 0.90 || findMyMode || (millis() - lastGpsCheck > 10000)) {
       updateLokasiA9G();
       lastGpsCheck = millis();
    }
    
    if (findMyMode) {
       Serial.println("[STATUS] MODE: FIND MY DEVICE (Buzzer ON)");
       digitalWrite(BUZZER_PIN, HIGH);
       if(Blynk.connected()) Blynk.virtualWrite(V4, "MENCARI PERANGKAT..."); 
       
       if (!telegramSent) {
          kirimTelegramBahaya();
          telegramSent = true;
       }

    } else {
       int switchState = digitalRead(SWITCH_PIN); 
       
       if (switchState == HIGH && currentRSSI > -75) {
          isAnomalyActive = false;
          anomalyStartTime = 0;
          telegramSent = false; 
          warningNotifSent = false; 
       } else {
          if (!isAnomalyActive) {
            isAnomalyActive = true;
            anomalyStartTime = millis(); 
          }
       }

       float durationSec = 0.0;
       if (isAnomalyActive) {
         durationSec = (float)(millis() - anomalyStartTime) / 1000.0;
       }

       threatProb = hitungProbabilitasAI(switchState, currentRSSI, lastRSSI, durationSec);
       int deltaPrint = abs(currentRSSI - lastRSSI); 

       Serial.println("\n=============================================");
       Serial.print("[SYSTEM] Uptime: "); Serial.print(millis()/1000); Serial.println("s");
       
       Serial.println("|-- [INPUTS] --------------------------------");
       Serial.print("|   Switch    : "); 
       if(switchState == HIGH) Serial.println("[ON] Terpasang (Aman)");
       else Serial.println("[OFF] DICABUT!");
       
       Serial.print("|   BLE RSSI  : "); Serial.print(currentRSSI); 
       Serial.print(" dBm (Last: "); Serial.print(lastRSSI);
       Serial.print(" | Delta: "); Serial.print(deltaPrint); Serial.println(")");

       Serial.println("|-- [AI LOGIC] ------------------------------");
       Serial.print("|   Duration  : "); Serial.print(durationSec, 1); Serial.println(" sec");
       Serial.print("|   Risk Prob : "); Serial.print(threatProb * 100, 1); Serial.println("%");

       Serial.println("|-- [LOCATION] ------------------------------");
       Serial.print("|   GPS A9G   : ");
       if (realLat != 0.0) {
          Serial.print(realLat, 6); Serial.print(", "); Serial.println(realLon, 6);
       } else {
          Serial.println("Searching Satellites...");
       }

       Serial.println("|-- [STATUS] --------------------------------");
       Serial.print("|   Network   : "); 
       if(WiFi.status() == WL_CONNECTED) Serial.print("WiFi [OK] "); else Serial.print("WiFi [OFF] ");
       if(Blynk.connected()) Serial.println("Blynk [OK]"); else Serial.println("Blynk [OFF]");
       
       Serial.print("|   Sent      : ");
       Serial.print("Tele["); Serial.print(telegramSent ? "YES" : "NO"); Serial.print("] ");
       Serial.print("Warn["); Serial.print(warningNotifSent ? "YES" : "NO"); Serial.println("]");

       Serial.print("|   RESULT    : ");
       if (threatProb > 0.90) Serial.println("CRITICAL (MALING!)");
       else if (threatProb > 0.50) Serial.println("WARNING (Mencurigakan)");
       else Serial.println("SAFE (Aman)");
       Serial.println("=============================================");

       if(Blynk.connected()) {
         Blynk.virtualWrite(V2, currentRSSI);         
         Blynk.virtualWrite(V3, threatProb * 100); 
         if (realLat != 0.0) {
            String lokasiStr = "Lat:" + String(realLat, 6) + ", Lon:" + String(realLon, 6);
            Blynk.virtualWrite(V1, lokasiStr); 
         } else {
            Blynk.virtualWrite(V1, "Mencari Satelit..."); 
         }

         if (threatProb > 0.85) {
             if (millis() - lastCriticalNotif > 15000) { 
                 Blynk.logEvent("critical_alert", "BAHAYA! Maling Terdeteksi! Cek Lokasi!");
                 Serial.println(">> Notifikasi Critical Dikirim!");
                 lastCriticalNotif = millis();
             }
         } 
         else if (threatProb > 0.50) {
             if (!warningNotifSent) {
                 Blynk.logEvent("warning_alert", "Peringatan: Ada aktivitas mencurigakan.");
                 Serial.println(">> Notifikasi Warning Dikirim!");
                 warningNotifSent = true; 
             }
         }
       }

       if (threatProb > 0.85) { 
         digitalWrite(BUZZER_PIN, HIGH); 
         if(Blynk.connected()) Blynk.virtualWrite(V4, "CRITICAL: THIEF!"); 

         if (!telegramSent) {
            kirimTelegramBahaya(); 
            telegramSent = true; 
         }

       } else if (threatProb > 0.50) {
         digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);
         if(Blynk.connected()) Blynk.virtualWrite(V4, "WARNING"); 

       } else {
         digitalWrite(BUZZER_PIN, LOW);
         if(Blynk.connected()) Blynk.virtualWrite(V4, "SAFE"); 
       }
    }

    lastLogicTime = millis();
  }
}