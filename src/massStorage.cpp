
#include "massStorage.h"
#include "display.h"
#include "idf/launcher_platform.h"
#include "sd_functions.h"
#ifdef SOC_USB_OTG_SUPPORTED
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include <algorithm>
#include <cstring>
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_private/periph_ctrl.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_wrap_ll.h"
#include "soc/lp_system_struct.h"
#include "soc/usb_wrap_struct.h"
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#include "hal/clk_gate_ll.h"
#include "soc/periph_defs.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_wrap_struct.h"
#endif

bool MassStorage::shouldStop = false;

namespace {
usb_phy_handle_t s_usbPhy = nullptr;
TaskHandle_t s_usbDeviceTask = nullptr;
bool s_usbStarted = false;
bool s_usbMounted = false;

constexpr uint8_t kUsbItfMsc = 0;
constexpr uint8_t kUsbEpOut = 0x01;
constexpr uint8_t kUsbEpIn = 0x81;
constexpr uint16_t kUsbEpSize = 64;

constexpr tusb_desc_device_t kDeviceDescriptor = {
    sizeof(tusb_desc_device_t),
    TUSB_DESC_DEVICE,
    0x0200,
    0x00, // bDeviceClass    - defined at interface level (fixes macOS MSC enumeration)
    0x00, // bDeviceSubClass - defined at interface level
    0x00, // bDeviceProtocol - defined at interface level
    CFG_TUD_ENDOINT0_SIZE,
    0x303A,
    0x1001,
    0x0100,
    0x01,
    0x02,
    0x03,
    0x01,
};

constexpr uint8_t kConfigDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500
    ),
    TUD_MSC_DESCRIPTOR(kUsbItfMsc, 4, kUsbEpOut, kUsbEpIn, kUsbEpSize),
};

const char *kUsbStrings[] = {
    "",
    "M5Stack",
    "Launcher SD",
    "TAB5-SD",
    "MSC",
};

void resetUsbDevicePeripheral() {
#if CONFIG_IDF_TARGET_ESP32P4
    PERIPH_RCC_ATOMIC() { _usb_wrap_ll_reset_register(); }
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_IO_MUX_RESET_DISABLE);
    REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_RESET_DISABLE);
    periph_ll_reset(PERIPH_USB_MODULE);
    periph_ll_enable_clk_clear_rst(PERIPH_USB_MODULE);
#endif
}

void usbDeviceTask(void *param) {
    (void)param;
    while (true) {
        tud_task();
        bool mounted = tud_mounted();
        if (mounted != s_usbMounted) {
            s_usbMounted = mounted;
            drawUSBStickIcon(mounted);
        }
        vTaskDelay(1);
    }
}

void restoreUsbSerial() {
#if CONFIG_IDF_TARGET_ESP32P4
    PERIPH_RCC_ATOMIC() { _usb_wrap_ll_enable_bus_clock(false); }
    usb_wrap_ll_phy_enable_pad(&USB_WRAP, false);
    PERIPH_RCC_ATOMIC() {
        _usb_serial_jtag_ll_enable_bus_clock(true);
        (usb_serial_jtag_ll_reset_register)();
    }
    usb_serial_jtag_ll_phy_set_defaults();
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;
    LP_SYS.usb_ctrl.sw_usb_phy_sel = 0; // USB_SERIAL_JTAG -> FSLS PHY0 (Tab5 USB-C)
    usb_serial_jtag_ll_phy_enable_pad(true);
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.begin(115200);
#endif
}

bool beginUsbRaw() {
    if (s_usbStarted) return true;

#if CONFIG_IDF_TARGET_ESP32P4
    PERIPH_RCC_ATOMIC() {
        _usb_serial_jtag_ll_enable_bus_clock(false);
        _usb_wrap_ll_enable_bus_clock(true);
    }
#endif
    resetUsbDevicePeripheral();
    vTaskDelay(pdMS_TO_TICKS(20));

    static const usb_phy_otg_io_conf_t otgIoConf = USB_PHY_SELF_POWERED_DEVICE(-1);
    usb_phy_config_t phyConfig = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_FULL,
        .ext_io_conf = nullptr,
        .otg_io_conf = &otgIoConf,
    };
    if (usb_new_phy(&phyConfig, &s_usbPhy) != ESP_OK) return false;

    tusb_rhport_init_t tinit = {};
    tinit.role = TUSB_ROLE_DEVICE;
    tinit.speed = TUSB_SPEED_FULL;
    if (!tusb_init(0, &tinit)) return false;
    tud_connect();

    xTaskCreate(usbDeviceTask, "usb_msc", 4096, nullptr, configMAX_PRIORITIES - 1, &s_usbDeviceTask);
    s_usbStarted = true;
    return true;
}

void endUsbRaw() {
    s_usbMounted = false;
    if (s_usbStarted) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(30));
        tusb_deinit(0);
    }
    if (s_usbDeviceTask) {
        vTaskDelete(s_usbDeviceTask);
        s_usbDeviceTask = nullptr;
    }
    if (s_usbPhy) {
        usb_del_phy(s_usbPhy);
        s_usbPhy = nullptr;
    }
    resetUsbDevicePeripheral();
    s_usbStarted = false;
    restoreUsbSerial();
}
} // namespace

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
    return reinterpret_cast<uint8_t const *>(&kDeviceDescriptor);
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return kConfigDescriptor;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[32];
    uint8_t chrCount = 0;

    if (index == 0) {
        desc[1] = 0x0409;
        chrCount = 1;
    } else {
        if (index >= (sizeof(kUsbStrings) / sizeof(kUsbStrings[0]))) return nullptr;
        const char *str = kUsbStrings[index];
        chrCount = static_cast<uint8_t>(std::min<size_t>(std::strlen(str), 31));
        for (uint8_t i = 0; i < chrCount; ++i) desc[1 + i] = str[i];
    }

    desc[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * chrCount + 2));
    return desc;
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void) { return 0; }

extern "C" void
tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    std::memset(vendor_id, ' ', 8);
    std::memset(product_id, ' ', 16);
    std::memset(product_rev, ' ', 4);
    std::memcpy(vendor_id, "M5Stack", 7);
    std::memcpy(product_id, "Launcher SD", 11);
    std::memcpy(product_rev, "1.0", 3);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = SDM.numSectors();
    *block_size = static_cast<uint16_t>(SDM.sectorSize());
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    return usbStartStopCallback(power_condition, start, load_eject);
}

extern "C" int32_t
tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    return usbReadCallback(lba, offset, buffer, bufsize);
}

extern "C" int32_t
tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    return usbWriteCallback(lba, offset, buffer, bufsize);
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)lun;
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL: return 0;
        case 0x35: return 0; // SYNCHRONIZE CACHE (10)
        default: tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); return -1;
    }
}

MassStorage::MassStorage() { setup(); }

MassStorage::~MassStorage() { endUsbRaw(); }

void MassStorage::setup() {
    displayMessage("Mounting...");

    setShouldStop(false);
    SDM.end(); // Forces SDCard mounting again.
    if (!setupSdCard()) {
        displayRedStripe("SD card not found.");
        launcherDelayMs(1000);
        return;
    }

    beginUsb();

    vTaskDelay(pdTICKS_TO_MS(500));
    return loop();
}

void MassStorage::loop() {
    while (!check(EscPress) && !shouldStop) yield();
}

void MassStorage::beginUsb() {
    drawUSBStickIcon(false);
    Serial.flush();
    Serial.end();
#if CONFIG_IDF_TARGET_ESP32P4
    usb_serial_jtag_ll_phy_enable_pad(false);
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;
    LP_SYS.usb_ctrl.sw_usb_phy_sel = 1; // USB_WRAP -> FSLS PHY0 (GPIO24/25 on Tab5 USB-C)
    usb_wrap_ll_phy_set_defaults(&USB_WRAP);
    usb_wrap_ll_phy_enable_pad(&USB_WRAP, true);
    usb_wrap_ll_phy_set_pullup_strength(&USB_WRAP, true);
#endif
    s_usbMounted = false;
    beginUsbRaw();
}

void MassStorage::displayMessage(String message) {
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    setTftDisplay(7, 7, ALCOLOR, FP, BGCOLOR);
    tft->drawCentreString("-= USB MSC =-", tftWidth / 2, 0, 8);
    tft->setCursor(10, 20);
    tftprint(message, 10, 5);
}

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    const uint32_t secSize = SDM.sectorSize();
    if (secSize == 0 || offset != 0 || (bufsize % secSize) != 0) return -1;

    const uint32_t blocks = bufsize / secSize;
    for (uint32_t x = 0; x < blocks; ++x) {
        if (!SDM.writeRAW(buffer + (secSize * x), lba + x)) {
            return -1; // write error
        }
    }
    return bufsize;
}

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    const uint32_t secSize = SDM.sectorSize();
    if (secSize == 0 || offset != 0 || (bufsize % secSize) != 0) return -1;

    const uint32_t blocks = bufsize / secSize;
    for (uint32_t x = 0; x < blocks; ++x) {
        if (!SDM.readRAW(reinterpret_cast<uint8_t *>(buffer) + (x * secSize), lba + x)) {
            return -1; // read error
        }
    }
    return bufsize;
}

bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (!start && load_eject) {
        MassStorage::setShouldStop(true);
        return true;
    }

    return true;
}

void drawUSBStickIcon(bool plugged) {
    MassStorage::displayMessage("");

    float scale;
    if (rotation & 0b01) scale = float((float)tftHeight / (float)135);
    else scale = float((float)tftWidth / (float)240);

    int iconW = scale * 120;
    int iconH = scale * 40;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    int radius = 5;

    int bodyW = 2 * iconW / 3;
    int bodyH = iconH;
    int bodyX = tftWidth / 2 - iconW / 2;
    int bodyY = tftHeight / 2;

    int portW = iconW - bodyW;
    int portH = 0.8 * bodyH;
    int portX = bodyX + bodyW;
    int portY = bodyY + (bodyH - portH) / 2;

    int portDetailW = portW / 2;
    int portDetailH = portH / 4;
    int portDetailX = portX + (portW - portDetailW) / 2;
    int portDetailY1 = portY + 0.8 * portDetailH;
    int portDetailY2 = portY + portH - 1.8 * portDetailH;

    int ledW = 0.1 * bodyH;
    int ledH = 0.6 * bodyH;
    int ledX = bodyX + 2 * ledW;
    int ledY = bodyY + (iconH - ledH) / 2;

    // Body
    tft->fillRoundRect(bodyX, bodyY, bodyW, bodyH, radius, DARKCYAN);
    // Port USB
    tft->fillRoundRect(portX, portY, portW, portH, radius, LIGHTGREY);
    // Small square on port
    tft->fillRoundRect(portDetailX, portDetailY1, portDetailW, portDetailH, radius, DARKGREY);
    tft->fillRoundRect(portDetailX, portDetailY2, portDetailW, portDetailH, radius, DARKGREY);
    // Led
    tft->fillRoundRect(ledX, ledY, ledW, ledH, radius, plugged ? GREEN : RED);

    tft->display(false);
}

#endif // SOC_USB_OTG_SUPPORTED
