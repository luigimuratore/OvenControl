#pragma once
#include <Adafruit_MAX31865.h>
#include "rtd_conversion.h"

// Retain Adafruit's initialization and calibrated temperature calculation.
// Only the short register transfers below replace its blocking read waits.
class AsyncRtd : public Adafruit_MAX31865 {
  int cs;
  uint8_t config = 0x11; // 3 wire, 50 Hz; bias and fault bits set per conversion.
  void write(uint8_t address, uint8_t value) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
    digitalWrite(cs, LOW); SPI.transfer(address | 0x80); SPI.transfer(value);
    digitalWrite(cs, HIGH); SPI.endTransaction();
  }
  uint16_t read(uint8_t address, bool pair = false) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
    digitalWrite(cs, LOW); SPI.transfer(address & 0x7f); uint16_t value = SPI.transfer(0xff);
    if (pair) value = (value << 8) | SPI.transfer(0xff);
    digitalWrite(cs, HIGH); SPI.endTransaction(); return value;
  }
 public:
  probes::Conversion conversion;
  explicit AsyncRtd(int chipSelect) : Adafruit_MAX31865(chipSelect), cs(chipSelect) {}
  void bias() { write(MAX31865_CONFIG_REG, config | MAX31865_CONFIG_FAULTSTAT | MAX31865_CONFIG_BIAS); }
  void oneShot() { write(MAX31865_CONFIG_REG, config | MAX31865_CONFIG_BIAS | MAX31865_CONFIG_1SHOT); }
  uint16_t readRaw() { return read(MAX31865_RTDMSB_REG, true) >> 1; }
  void startFault() { write(MAX31865_CONFIG_REG, config | 0x84); }
  uint8_t faultBits() { return uint8_t(read(MAX31865_FAULTSTAT_REG)); }
  void stop() { write(MAX31865_CONFIG_REG, config); }
  void start(uint32_t now) { conversion.start(*this, now); }
  bool tick(uint32_t now) { return conversion.tick(*this, now); }
};
