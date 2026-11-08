// Diagnostic only: no OLED or tone calls. Passive piezo D6 -> GND.
#include <DHT.h>
DHT dht(2, DHT11);
void setup() {
  digitalWrite(6, LOW);
  pinMode(6, OUTPUT);
  Serial.begin(9600);
  dht.begin();
  Serial.println(F("HardwareCheck: DHT11 on D2; D6 held LOW. No sound generated."));
  delay(2500); // Sensor power-up settling time.
}
void loop() {
  const int idle = digitalRead(2);
  const float h = dht.readHumidity();
  const float t = dht.readTemperature();
  Serial.print(F("DATA idle=")); Serial.print(idle);
  if (isnan(h) || isnan(t)) {
    Serial.println(F(" | DHT11 read FAILED. Check pinout, power, DATA and pull-up."));
  } else {
    Serial.print(F(" | Temperature=")); Serial.print(t,1);
    Serial.print(F(" C | Humidity=")); Serial.print(h,1); Serial.println(F(" %"));
  }
  delay(2500);
}
