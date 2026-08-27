#include "papermono_display.h"

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||                    \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
#include <Arduino.h>
#include <cstring>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||               \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
#include <esp_heap_caps.h>
#endif

namespace {

constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;
constexpr gpio_num_t kDisplayMosi = GPIO_NUM_14;
constexpr gpio_num_t kDisplayClock = GPIO_NUM_15;
constexpr gpio_num_t kDisplayChipSelect = GPIO_NUM_16;
constexpr gpio_num_t kDisplayDataCommand = GPIO_NUM_17;
constexpr gpio_num_t kDisplayBusy = GPIO_NUM_18;
constexpr int kDisplaySpiHz = 20 * 1000 * 1000;

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
constexpr size_t kOtpBytesPerRow = 100;
constexpr size_t kOtpFrameRows = 480;
constexpr size_t kOtpFrameBytes = kOtpBytesPerRow * kOtpFrameRows;
constexpr size_t kOtpPayloadTransferBytes = 4092;
#endif

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
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
    busConfig.max_transfer_sz = kOtpPayloadTransferBytes;
    if (spi_bus_initialize(kDisplaySpiHost, &busConfig, SPI_DMA_CH_AUTO) != ESP_OK) return false;
#else
    busConfig.max_transfer_sz = 4;
    if (spi_bus_initialize(kDisplaySpiHost, &busConfig, SPI_DMA_DISABLED) != ESP_OK) return false;
#endif
    busOwned_ = true;

    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.clock_speed_hz = kDisplaySpiHz;
    deviceConfig.mode = 0;
    deviceConfig.spics_io_num = -1;
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
    deviceConfig.flags = SPI_DEVICE_HALFDUPLEX;
#endif
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

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||             \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoDisplay::configureOtpMono() {
    // Direct port of the official OTP Demo's init_mono_mode() command order.
    constexpr uint8_t kTemperatureSelection[] = {0x80};
    constexpr uint8_t kBoosterSoftStart[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    constexpr uint8_t kDriverOutput[] = {0xDF, 0x01, 0x02};
    constexpr uint8_t kBorder[] = {0x01};
    constexpr uint8_t kNormalRamMode[] = {0x00};

    return softwareReset() && waitBusyIdle(15000) &&
           sendCommandData(0x18, kTemperatureSelection, sizeof(kTemperatureSelection)) &&
           waitBusyIdle(15000) && sendCommandData(0x0C, kBoosterSoftStart, sizeof(kBoosterSoftStart)) &&
           waitBusyIdle(15000) && sendCommandData(0x01, kDriverOutput, sizeof(kDriverOutput)) &&
           waitBusyIdle(15000) && sendCommandData(0x3C, kBorder, sizeof(kBorder)) && waitBusyIdle(15000) &&
           sendCommandData(0x21, kNormalRamMode, sizeof(kNormalRamMode)) && setOtpFullWindow();
}

bool PaperMonoDisplay::writeOtpWhiteBaseline() {
    uint8_t whiteRow[kOtpBytesPerRow];
    std::memset(whiteRow, 0xFF, sizeof(whiteRow));

    if (!waitBusyIdle(15000) || !setOtpFullWindow() || !sendCommand(0x26)) return false;
    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        if (!sendDataBlock(whiteRow, sizeof(whiteRow))) return false;
    }

    if (!setOtpFullWindow() || !sendCommand(0x24)) return false;
    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        if (!sendDataBlock(whiteRow, sizeof(whiteRow))) return false;
    }
    return true;
}

bool PaperMonoDisplay::writeOtpInitialBlockFrame() {
    uint8_t rowData[kOtpBytesPerRow];
    if (!waitBusyIdle(15000) || !setOtpFullWindow() || !sendCommand(0x24)) return false;

    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        // Direct port of demo_begin(): the upper-left official block is black.
        std::memset(rowData, 0xFF, sizeof(rowData));
        if (row < kOtpFrameRows / 2) std::memset(rowData, 0x00, kOtpBytesPerRow / 2);
        if (!sendDataBlock(rowData, sizeof(rowData))) return false;
    }
    return true;
}

bool PaperMonoDisplay::stageOtpUpdateControl() {
    constexpr uint8_t kNormalRamMode[] = {0x00};
    constexpr uint8_t kOtpSingleActivationUpdateControl[] = {0xFF};
    return waitBusyIdle(15000) && sendCommandData(0x21, kNormalRamMode, sizeof(kNormalRamMode)) &&
           waitBusyIdle(15000) &&
           sendCommandData(
               0x22, kOtpSingleActivationUpdateControl, sizeof(kOtpSingleActivationUpdateControl)
           );
}

bool PaperMonoDisplay::activateOtpOnce(uint8_t &activationCount) {
    activationCount = 0;
    if (!waitBusyIdle(15000) || !sendCommand(0x20)) return false;
    activationCount = 1;
    return waitBusyIdle(15000);
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||               \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoDisplay::configureOtpFullMono() {
    // Direct port of the official OTP Demo's init_mono_mode() command order.
    constexpr uint8_t kTemperatureSelection[] = {0x80};
    constexpr uint8_t kBoosterSoftStart[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    constexpr uint8_t kDriverOutput[] = {0xDF, 0x01, 0x02};
    constexpr uint8_t kBorder[] = {0x01};
    constexpr uint8_t kNormalRamMode[] = {0x00};

    return softwareReset() && waitBusyIdle(15000) &&
           sendCommandData(0x18, kTemperatureSelection, sizeof(kTemperatureSelection)) &&
           waitBusyIdle(15000) && sendCommandData(0x0C, kBoosterSoftStart, sizeof(kBoosterSoftStart)) &&
           waitBusyIdle(15000) && sendCommandData(0x01, kDriverOutput, sizeof(kDriverOutput)) &&
           waitBusyIdle(15000) && sendCommandData(0x3C, kBorder, sizeof(kBorder)) && waitBusyIdle(15000) &&
           sendCommandData(0x21, kNormalRamMode, sizeof(kNormalRamMode)) && setOtpFullWindow();
}

bool PaperMonoDisplay::stageOtpFullFirstControl() {
    constexpr uint8_t kOfficialFirstControl[] = {0xF8};
    return waitBusyIdle(15000) && sendCommandData(0x22, kOfficialFirstControl, sizeof(kOfficialFirstControl));
}

bool PaperMonoDisplay::writeOtpFullStageOneFrame() {
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    if (!otpPendingFrameValid_ || otpPendingFrame_ == nullptr || !waitBusyIdle(15000) ||
        !setOtpFullWindow() || !sendCommand(0x24)) {
        return false;
    }

    uint8_t rowData[kOtpBytesPerRow];
    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        const uint8_t *pendingRow = otpPendingFrame_ + row * kOtpBytesPerRow;
        for (size_t byte = 0; byte < kOtpBytesPerRow; ++byte)
            rowData[byte] = static_cast<uint8_t>(~pendingRow[byte]);
        if (!sendDataBlock(rowData, sizeof(rowData))) return false;
    }
    return true;
#else
    uint8_t rowData[kOtpBytesPerRow];
    if (!waitBusyIdle(15000) || !setOtpFullWindow() || !sendCommand(0x24)) return false;

    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        // Direct port of make_bw_quadrants(): black upper-left and lower-right.
        std::memset(rowData, 0xFF, sizeof(rowData));
        if (row < kOtpFrameRows / 2) std::memset(rowData, 0x00, kOtpBytesPerRow / 2);
        else std::memset(rowData + kOtpBytesPerRow / 2, 0x00, kOtpBytesPerRow / 2);
        for (size_t byte = 0; byte < sizeof(rowData); ++byte)
            rowData[byte] = static_cast<uint8_t>(~rowData[byte]);
        if (!sendDataBlock(rowData, sizeof(rowData))) return false;
    }
    return true;
#endif
}

bool PaperMonoDisplay::activateOtpFullFirst(uint8_t &activationCount) {
    if (activationCount != 0 || !waitBusyIdle(15000) || !sendCommand(0x20)) return false;
    activationCount = 1;
    return waitBusyIdle(15000);
}

bool PaperMonoDisplay::stageOtpFullSecondControl() {
    constexpr uint8_t kOfficialSecondControl[] = {0x14};
    return waitBusyIdle(15000) &&
           sendCommandData(0x22, kOfficialSecondControl, sizeof(kOfficialSecondControl));
}

bool PaperMonoDisplay::writeOtpFullStageTwoFrames() {
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    if (!otpPendingFrameValid_ || otpPendingFrame_ == nullptr) return false;
    return writeOtpFrameToRam(0x26, otpPendingFrame_) && writeOtpFrameToRam(0x24, otpPendingFrame_);
#else
    uint8_t rowData[kOtpBytesPerRow];
    if (!waitBusyIdle(15000) || !setOtpFullWindow() || !sendCommand(0x26)) return false;

    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        std::memset(rowData, 0xFF, sizeof(rowData));
        if (row < kOtpFrameRows / 2) std::memset(rowData, 0x00, kOtpBytesPerRow / 2);
        else std::memset(rowData + kOtpBytesPerRow / 2, 0x00, kOtpBytesPerRow / 2);
        if (!sendDataBlock(rowData, sizeof(rowData))) return false;
    }

    if (!setOtpFullWindow() || !sendCommand(0x24)) return false;
    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        std::memset(rowData, 0xFF, sizeof(rowData));
        if (row < kOtpFrameRows / 2) std::memset(rowData, 0x00, kOtpBytesPerRow / 2);
        else std::memset(rowData + kOtpBytesPerRow / 2, 0x00, kOtpBytesPerRow / 2);
        if (!sendDataBlock(rowData, sizeof(rowData))) return false;
    }
    return true;
#endif
}

bool PaperMonoDisplay::activateOtpFullSecond(uint8_t &activationCount) {
    if (activationCount != 1 || !waitBusyIdle(15000) || !sendCommand(0x20)) return false;
    activationCount = 2;
    return waitBusyIdle(15000);
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||               \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoDisplay::ensureOtpShadowFrames() {
    if (otpPreviousFrame_ != nullptr && otpPendingFrame_ != nullptr) return true;

    if (otpPreviousFrame_ == nullptr) {
        otpPreviousFrame_ =
            static_cast<uint8_t *>(heap_caps_malloc(kOtpFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (otpPendingFrame_ == nullptr) {
        otpPendingFrame_ =
            static_cast<uint8_t *>(heap_caps_malloc(kOtpFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (otpPreviousFrame_ == nullptr || otpPendingFrame_ == nullptr) {
        otpPreviousFrameValid_ = false;
        return false;
    }
    return true;
}

bool PaperMonoDisplay::submitMonochromeFrame(const uint8_t *packedFrame, size_t bytes) {
    if (packedFrame == nullptr || bytes != kOtpFrameBytes || !ensureOtpShadowFrames()) return false;
    std::memcpy(otpPendingFrame_, packedFrame, kOtpFrameBytes);
    otpPendingFrameValid_ = true;
    return true;
}

bool PaperMonoDisplay::pendingFrameValid() const { return otpPendingFrameValid_; }

void PaperMonoDisplay::fillOtpQuadrantFrame(uint8_t *frame, bool inverse) const {
    if (frame == nullptr) return;

    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        uint8_t *rowData = frame + row * kOtpBytesPerRow;
        std::memset(rowData, 0xFF, kOtpBytesPerRow);
        const bool upper = row < kOtpFrameRows / 2;
        const bool blackLeft = inverse ? !upper : upper;
        if (blackLeft) std::memset(rowData, 0x00, kOtpBytesPerRow / 2);
        else std::memset(rowData + kOtpBytesPerRow / 2, 0x00, kOtpBytesPerRow / 2);
    }
}

#if defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoDisplay::prepareOtpQuadrantTarget(bool inverse) {
    if (!ensureOtpShadowFrames()) return false;
    fillOtpQuadrantFrame(otpPendingFrame_, inverse);
    otpPendingFrameValid_ = true;
    return true;
}
#endif

bool PaperMonoDisplay::seedOtpPreviousFromPending() {
    if (!ensureOtpShadowFrames()) return false;
    std::memcpy(otpPreviousFrame_, otpPendingFrame_, kOtpFrameBytes);
    otpPreviousFrameValid_ = true;
    return true;
}

bool PaperMonoDisplay::otpPreviousFrameValid() const { return otpPreviousFrameValid_; }

bool PaperMonoDisplay::writeOtpFrameToRam(uint8_t command, const uint8_t *frame) {
    if (frame == nullptr || !waitBusyIdle(15000) || !setOtpFullWindow() || !sendCommand(command))
        return false;

    for (size_t row = 0; row < kOtpFrameRows; ++row) {
        if (!sendDataBlock(frame + row * kOtpBytesPerRow, kOtpBytesPerRow)) return false;
    }
    return true;
}

bool PaperMonoDisplay::writeOtpRepeatedPartialPlanes() {
    if (!otpPreviousFrameValid_ || otpPendingFrame_ == nullptr || otpPreviousFrame_ == nullptr) return false;

    // FreeInk PaperMono's binary OTP path derives 0x24 from the next target and
    // 0x26 from the last successfully displayed B/W frame.  B/W packing is
    // unchanged: white is bit 1 and black is bit 0 in both full-frame buffers.
    return writeOtpFrameToRam(0x24, otpPendingFrame_) && writeOtpFrameToRam(0x26, otpPreviousFrame_);
}

bool PaperMonoDisplay::commitOtpPendingFrame() {
    if (!otpPreviousFrameValid_ || otpPreviousFrame_ == nullptr || otpPendingFrame_ == nullptr) return false;
    std::memcpy(otpPreviousFrame_, otpPendingFrame_, kOtpFrameBytes);
    return true;
}
#endif

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

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoDisplay::sendDataBlock(const uint8_t *data, size_t length) {
    if (device_ == nullptr || length == 0 || gpio_set_level(kDisplayDataCommand, 1) != ESP_OK ||
        gpio_set_level(kDisplayChipSelect, 0) != ESP_OK) {
        return false;
    }

    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    const bool sent =
        spi_device_polling_transmit(static_cast<spi_device_handle_t>(device_), &transaction) == ESP_OK;
    return gpio_set_level(kDisplayChipSelect, 1) == ESP_OK && sent;
}

bool PaperMonoDisplay::setOtpFullWindow() {
    constexpr uint8_t kDataEntry[] = {0x03};
    constexpr uint8_t kRamXRange[] = {0x00, 0x00, 0x1F, 0x03};
    constexpr uint8_t kRamYRange[] = {0x00, 0x00, 0xDF, 0x01};
    constexpr uint8_t kRamXCounter[] = {0x00, 0x00};
    constexpr uint8_t kRamYCounter[] = {0x00, 0x00};

    return waitBusyIdle(15000) && sendCommandData(0x11, kDataEntry, sizeof(kDataEntry)) &&
           waitBusyIdle(15000) && sendCommandData(0x44, kRamXRange, sizeof(kRamXRange)) &&
           waitBusyIdle(15000) && sendCommandData(0x45, kRamYRange, sizeof(kRamYRange)) &&
           waitBusyIdle(15000) && sendCommandData(0x4E, kRamXCounter, sizeof(kRamXCounter)) &&
           waitBusyIdle(15000) && sendCommandData(0x4F, kRamYCounter, sizeof(kRamYCounter)) &&
           waitBusyIdle(15000);
}
#endif

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
