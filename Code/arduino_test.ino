/*
 * SheSafe - ESP32 Dev Module
 * IR LED Blink Test
 * GPIO 14
 * 500ms ON / 500ms OFF
 */

const int IR_LED_PIN = 14;

const unsigned long BLINK_INTERVAL = 500;

unsigned long lastBlinkTime = 0;
bool ledState = false;
unsigned long blinkCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("SheSafe - IR LED Blink Test");
  Serial.println("Board: ESP32 Dev Module");
  Serial.println("GPIO: 14");

  pinMode(IR_LED_PIN, OUTPUT);
  digitalWrite(IR_LED_PIN, LOW);
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = currentMillis;

    ledState = !ledState;
    digitalWrite(IR_LED_PIN, ledState ? HIGH : LOW);

    if (ledState) {
      blinkCount++;
      Serial.printf("IR LED ON  | Cycle: %lu\n", blinkCount);
    } else {
      Serial.println("IR LED OFF");
    }
  }
}
}
