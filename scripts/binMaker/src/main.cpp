#include <Arduino.h>

void setup() {
    Serial.begin(152000);
}

void loop() {
    Serial.println("Hello World!");
    delay(1000);
}