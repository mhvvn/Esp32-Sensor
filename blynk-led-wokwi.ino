
#define BLYNK_TEMPLATE_ID "your blynk template id"
#define BLYNK_TEMPLATE_NAME "your blynk template name"
#define BLYNK_AUTH_TOKEN "your token"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "Wokwi-GUEST";
char pass[] = "";

void setup()
{
  Serial.begin(115200);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}
void loop()
{
  Blynk.run();
  delay(100); 
}
