#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace ip5306 {

// REG_READ0 (0x78) bit layout:
//   [4] CHARGE_FULL  – 1 when battery is fully charged
//   [3] CHARGING     – 1 when charging is in progress
//   [2:1] LEVEL[1:0] – LED indicator bits: 00=0%, 01=25%, 10=50%, 11=75%
//                      100% is inferred from CHARGE_FULL.
static constexpr uint8_t IP5306_REG_READ0 = 0x78;

constexpr static const char *const TAG = "ip5306";

class IP5306 : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_battery_level_sensor(sensor::Sensor *s) { battery_level_ = s; }
  void set_charging_sensor(binary_sensor::BinarySensor *bs) { charging_ = bs; }

 protected:
  sensor::Sensor *battery_level_{nullptr};
  binary_sensor::BinarySensor *charging_{nullptr};
};

}  // namespace ip5306
}  // namespace esphome
