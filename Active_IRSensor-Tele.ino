#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Wi-Fi and Telegram Credentials
const char* ssid = "SSID";
const char* password = "Password";


#define BOTtoken "TOKEN"
#define CHAT_ID "Chat_ID"


#ifdef ESP8266
  X509List cert(TELEGRAM_CERTIFICATE_ROOT);
#endif

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

#define LED 2
#define Buzzer 5
#define Sensor 4

int vallue;



void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

    #ifdef ESP8266
  configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
  client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
  #endif

   #ifdef ESP32
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  #endif

  pinMode(LED, OUTPUT);
  pinMode(Buzzer, OUTPUT);

  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);


  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);


  int a = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    a++;
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(500);

  bot.sendMessage(CHAT_ID, "Bot started up", "");
  //delay(1000);

}


void loop() {
  vallue = digitalRead(Sensor);

  if (vallue==LOW){
  tone(Buzzer,3000);
  bot.sendMessage(CHAT_ID, "Gerakan Terdeteksi", "");
  digitalWrite(LED, HIGH);
  Serial.println("Gerakan Terdeteksi");
  }
  // put your main code here, to run repeatedly:
if (vallue==LOW){
  digitalWrite(LED, LOW);
  noTone(Buzzer); // Turn off buzzer as well if needed
 // Serial.println("Tidak Ada Gerakan Terdeteksi");
 // bot.sendMessage(CHAT_ID, "Tidak Ada Gerakan Terdeteksi", "");
}
delay(500);
}
