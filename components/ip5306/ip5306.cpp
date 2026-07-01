#include "ip5306.h"
#include "esphome/core/log.h"

// Driver for the IP5306 battery management IC (I2C variant, address 0x75).
// Reads register 0x78 to determine charging status and battery level.
// Battery level is reported in 25% increments (4 LED bars); 100% is
// indicated by the CHARGE_FULL flag rather than the LED bits.
namespace esphome {
namespace ip5306 {

void IP5306::setup() {
    uint8_t data;
    auto err = this->read_register(IP5306_REG_READ0, &data, 1);
    if (err != i2c::ERROR_OK) {
        ESP_LOGE(TAG, "IP5306 not found at address 0x%02X (I2C error %d)", this->address_, err);
        this->mark_failed();
        return;
    }
    ESP_LOGI(TAG, "IP5306 found");
}

void IP5306::update() {
    uint8_t data;
    auto err = this->read_register(IP5306_REG_READ0, &data, 1);
    if (err != i2c::ERROR_OK) {
        ESP_LOGW(TAG, "I2C read failed: %d", err);
        this->status_set_warning();
        return;
    }
    this->status_clear_warning();

    bool charge_full = (data >> 4) & 0x01;
    bool charging    = (data >> 3) & 0x01;

    // LED bits [2:1] encode four capacity steps (0, 25, 50, 75 %).
    // When CHARGE_FULL is set, treat as 100 % regardless of LED bits.
    float level_pct;
    if (charge_full) {
        level_pct = 100.0f;
    } else {
        uint8_t leds = (data >> 1) & 0x03;
        level_pct = leds * 25.0f;
    }

    ESP_LOGD(TAG, "REG_READ0=0x%02X → level=%.0f%% charging=%s full=%s",
             data, level_pct, charging ? "yes" : "no", charge_full ? "yes" : "no");

    if (this->battery_level_ != nullptr)
        this->battery_level_->publish_state(level_pct);

    if (this->charging_ != nullptr)
        this->charging_->publish_state(charging || charge_full);
}

void IP5306::dump_config() {
    ESP_LOGCONFIG(TAG, "IP5306 Battery:");
    LOG_I2C_DEVICE(this);
    LOG_UPDATE_INTERVAL(this);
    LOG_SENSOR("  ", "Battery Level", this->battery_level_);
    LOG_BINARY_SENSOR("  ", "Charging", this->charging_);
}

}  // namespace ip5306
}  // namespace esphome
