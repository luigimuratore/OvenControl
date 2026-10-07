#pragma once

// Collegamenti ripresi dagli altri firmware ESP32-S3 N16R8.
constexpr int PIN_CS1 = 14, PIN_CS2 = 10;
constexpr int PIN_SCK = 13, PIN_MISO = 12, PIN_MOSI = 11;
constexpr int PIN_RELAY = 4, PIN_RED = 6, PIN_GREEN = 7;
// PT100 a 3 fili; verificare anche ponticelli e RREF sulle due Click.
constexpr float RREF_1 = 470.0f, RREF_2 = 470.0f, RNOMINAL = 100.0f;
constexpr bool RELAY_ACTIVE_HIGH = true;
constexpr bool LED_ACTIVE_HIGH = true;
