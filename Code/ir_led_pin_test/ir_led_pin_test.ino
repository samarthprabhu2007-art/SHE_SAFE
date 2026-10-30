// Standalone pin test for Seeed Studio XIAO ESP32-C5.
// Select "XIAO_ESP32C5" in Arduino IDE before compiling.
// Connect one LED through a 220-330 ohm resistor from the tested pin to GND.

const uint8_t testPins[] = { D1, D4, D5 };
const char* testNames[] = { "D1", "D4", "D5" };
const size_t testPinCount = sizeof(testPins) / sizeof(testPins[0]);

void testPin(const char* label, uint8_t pin) {
  Serial.printf("\nTesting %s / GPIO%d for 5 seconds\n", label, (int)pin);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  const unsigned long startedAt = millis();
  bool outputHigh = false;
  while (millis() - startedAt < 5000) {
    outputHigh = !outputHigh;
    digitalWrite(pin, outputHigh ? HIGH : LOW);
    const int readBack = digitalRead(pin);
    Serial.printf("%s/GPIO%d set=%s read=%s\n",
                  label,
                  (int)pin,
                  outputHigh ? "HIGH" : "LOW",
                  readBack == HIGH ? "HIGH" : "LOW");
    delay(500);
  }

  digitalWrite(pin, LOW);
  pinMode(pin, INPUT);
  Serial.printf("Finished %s / GPIO%d\n", label, (int)pin);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("XIAO pin map: D1=%d D4=%d D5=%d\n", D1, D4, D5);
  Serial.println("Run the LED from the selected pin through a 220-330 ohm resistor to GND.");
}

void loop() {
  for (size_t i = 0; i < testPinCount; ++i) {
    testPin(testNames[i], testPins[i]);
  }
}
