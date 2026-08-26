#include "papermono_display.h"

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace {

constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;
constexpr gpio_num_t kDisplayMosi = GPIO_NUM_14;
constexpr gpio_num_t kDisplayClock = GPIO_NUM_15;
constexpr gpio_num_t kDisplayChipSelect = GPIO_NUM_16;
constexpr gpio_num_t kDisplayDataCommand = GPIO_NUM_17;
constexpr gpio_num_t kDisplayBusy = GPIO_NUM_18;
constexpr int kDisplaySpiHz = 20 * 1000 * 1000;

bool configurePins() {
    gpio_config_t outputConfig = {};
    outputConfig.pin_bit_mask = (1ULL << kDisplayChipSelect) | (1ULL << kDisplayDataCommand);
    outputConfig.mode = GPIO_MODE_OUTPUT;
    outputConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    outputConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&outputConfig) != ESP_OK) return false;

    gpio_config_t busyConfig = {};
    busyConfig.pin_bit_mask = 1ULL << kDisplayBusy;
    busyConfig.mode = GPIO_MODE_INPUT;
    busyConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    busyConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    busyConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&busyConfig) != ESP_OK) return false;

    return gpio_set_level(kDisplayChipSelect, 1) == ESP_OK &&
           gpio_set_level(kDisplayDataCommand, 0) == ESP_OK;
}

} // namespace

bool PaperMonoDisplay::beginTransport() {
    if (busOwned_) return device_ != nullptr;
    if (!configurePins()) return false;

    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = kDisplayMosi;
    busConfig.miso_io_num = -1;
    busConfig.sclk_io_num = kDisplayClock;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = 4;
    if (spi_bus_initialize(kDisplaySpiHost, &busConfig, SPI_DMA_DISABLED) != ESP_OK) return false;
    busOwned_ = true;

    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = kDisplaySpiHz;
    deviceConfig.mode = 0;
    deviceConfig.spics_io_num = -1;
    deviceConfig.queue_size = 1;

    spi_device_handle_t device = nullptr;
    if (spi_bus_add_device(kDisplaySpiHost, &deviceConfig, &device) != ESP_OK) {
        spi_bus_free(kDisplaySpiHost);
        busOwned_ = false;
        return false;
    }
    device_ = device;
    return true;
}

bool PaperMonoDisplay::waitBusyIdle(uint32_t timeoutMs) {
    const uint32_t startMs = millis();
    while (gpio_get_level(kDisplayBusy) != 0) {
        if (millis() - startMs >= timeoutMs) return false;
        delay(1);
    }
    return true;
}

bool PaperMonoDisplay::softwareReset() {
    if (!waitBusyIdle(15000)) return false;
    if (!sendCommand(0x12)) return false;
    delay(10);
    return waitBusyIdle(15000);
}

bool PaperMonoDisplay::configureNoRefresh() {
    // These OTP-demo controller setup commands only establish controller state;
    // they do not write image RAM or request a panel update.
    constexpr uint8_t kTemperatureSelection[] = {0x80};
    constexpr uint8_t kBoosterSoftStart[] = {0x17, 0x17, 0x17, 0x17, 0x17};
    constexpr uint8_t kDriverOutput[] = {0x1F, 0x00, 0x00};
    constexpr uint8_t kBorder[] = {0x05};

    return waitBusyIdle(15000) &&
           sendCommandData(0x18, kTemperatureSelection, sizeof(kTemperatureSelection)) &&
           waitBusyIdle(15000) && sendCommandData(0x0C, kBoosterSoftStart, sizeof(kBoosterSoftStart)) &&
           waitBusyIdle(15000) && sendCommandData(0x01, kDriverOutput, sizeof(kDriverOutput)) &&
           waitBusyIdle(15000) && sendCommandData(0x3C, kBorder, sizeof(kBorder)) && waitBusyIdle(15000);
}

bool PaperMonoDisplay::releaseTransport() {
    bool ok = true;
    if (device_ != nullptr) {
        ok = spi_bus_remove_device(static_cast<spi_device_handle_t>(device_)) == ESP_OK;
        device_ = nullptr;
    }
    if (busOwned_) {
        ok &= spi_bus_free(kDisplaySpiHost) == ESP_OK;
        busOwned_ = false;
    }
    ok &= gpio_set_level(kDisplayChipSelect, 1) == ESP_OK;
    ok &= gpio_set_level(kDisplayDataCommand, 0) == ESP_OK;
    ok &= gpio_set_direction(kDisplayMosi, GPIO_MODE_INPUT) == ESP_OK;
    ok &= gpio_set_direction(kDisplayClock, GPIO_MODE_INPUT) == ESP_OK;
    return ok;
}

bool PaperMonoDisplay::sendCommand(uint8_t command) { return transmitByte(command, false); }

bool PaperMonoDisplay::sendCommandData(uint8_t command, const uint8_t *data, uint8_t length) {
    if (!sendCommand(command)) return false;
    for (uint8_t index = 0; index < length; ++index) {
        if (!transmitByte(data[index], true)) return false;
    }
    return true;
}

bool PaperMonoDisplay::transmitByte(uint8_t value, bool dataMode) {
    if (device_ == nullptr || gpio_set_level(kDisplayDataCommand, dataMode ? 1 : 0) != ESP_OK ||
        gpio_set_level(kDisplayChipSelect, 0) != ESP_OK) {
        return false;
    }

    spi_transaction_t transaction = {};
    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.length = 8;
    transaction.tx_data[0] = value;
    const bool sent = spi_device_transmit(static_cast<spi_device_handle_t>(device_), &transaction) == ESP_OK;
    return gpio_set_level(kDisplayChipSelect, 1) == ESP_OK && sent;
}
#endif
