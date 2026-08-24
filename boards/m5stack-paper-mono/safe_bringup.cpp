#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef PAPERMONO_SAFE_BRINGUP
#error "safe_bringup.cpp must only be built with PAPERMONO_SAFE_BRINGUP enabled"
#endif

#define PAPERMONO_STAGE_PM1_READ_ONLY 1
#define PAPERMONO_STAGE_IOE1_READ_ONLY 2

#ifndef PAPERMONO_CLASS_A_STAGE
#define PAPERMONO_CLASS_A_STAGE PAPERMONO_STAGE_PM1_READ_ONLY
#endif

#if PAPERMONO_CLASS_A_STAGE != PAPERMONO_STAGE_PM1_READ_ONLY && \
    PAPERMONO_CLASS_A_STAGE != PAPERMONO_STAGE_IOE1_READ_ONLY
#error "Unsupported PAPERMONO_CLASS_A_STAGE"
#endif

namespace {
constexpr gpio_num_t kSdaPin = GPIO_NUM_47;
constexpr gpio_num_t kSclPin = GPIO_NUM_48;
constexpr std::uint32_t kI2cFrequencyHz = 100000;
constexpr std::uint32_t kSclWaitUs = 5000;
constexpr int kTransactionTimeoutMs = 20;
constexpr int kStartupSettlingDelayMs = 500;

constexpr std::uint8_t kPm1Address = 0x6E;
constexpr std::uint8_t kPm1DeviceIdRegister = 0x00;
constexpr std::uint16_t kPm1ExpectedDeviceId = 0x2050;

constexpr std::uint8_t kIoe1Address = 0x4F;
constexpr std::uint8_t kIoe1UidRegister = 0x00;
constexpr std::uint8_t kIoe1GpioModeRegister = 0x03;
constexpr std::uint8_t kIoe1GpioOutRegister = 0x05;
constexpr std::uint8_t kIoe1GpioInRegister = 0x07;
constexpr std::size_t kIoe1UidLength = 2;
constexpr std::size_t kIoe1GpioSnapshotLength = 6;

// M5IOE1 GPIO numbering is one-based at the package boundary and zero-based
// in the register banks. Low-bank bits cover PYG1..PYG8; high-bank bits cover
// PYG9..PYG14. PaperMono's relevant control outputs are therefore:
// PYG3=low bit2, PYG11=high bit2, PYG13=high bit4, PYG14=high bit5.
constexpr std::uint8_t kIoe1Pyg3Mask = 1U << 2;
constexpr std::uint8_t kIoe1Pyg11Mask = 1U << 2;
constexpr std::uint8_t kIoe1Pyg13Mask = 1U << 4;
constexpr std::uint8_t kIoe1Pyg14Mask = 1U << 5;

constexpr std::uint32_t kResultMagic = 0x504D4130;       // "PMA0"
constexpr std::uint32_t kCompletionMarker = 0x434F4D50;  // "COMP"
constexpr std::int32_t kErrorNotSet = INT32_MIN;

enum class ProbeState : std::uint32_t {
    NOT_STARTED = 0,
    BUS_NOT_IDLE = 1,
    GPIO_INIT_FAILED = 2,
    I2C_INIT_FAILED = 3,
    TRANSACTION_FAILED = 4,
    ID_MISMATCH = 5,
    PASS = 6,
};

struct ReadOnlyStagePlan {
    std::uint32_t stage;
    std::uint8_t device_address;
    std::uint8_t register_address;
    bool validate_expected_value;
    std::uint16_t expected_value;
};

constexpr ReadOnlyStagePlan kPm1ReadOnlyPlan = {
    PAPERMONO_STAGE_PM1_READ_ONLY,
    kPm1Address,
    kPm1DeviceIdRegister,
    true,
    kPm1ExpectedDeviceId,
};

// M5Unified 0.2.20 M5IOE1_Class::begin() identifies IOE1 by reading two
// bytes from register 0x00. It then writes register 0x23; this harness never
// performs that write. The official source does not define a fixed UID value,
// so this prepared stage records the raw UID and validates transaction success.
constexpr ReadOnlyStagePlan kIoe1ReadOnlyPlan = {
    PAPERMONO_STAGE_IOE1_READ_ONLY,
    kIoe1Address,
    kIoe1UidRegister,
    false,
    0,
};

#if PAPERMONO_CLASS_A_STAGE == PAPERMONO_STAGE_PM1_READ_ONLY
constexpr ReadOnlyStagePlan kActiveStagePlan = kPm1ReadOnlyPlan;
#else
constexpr ReadOnlyStagePlan kActiveStagePlan = kIoe1ReadOnlyPlan;
#endif
}  // namespace

struct PaperMonoClassAResult {
    std::uint32_t magic;
    std::uint32_t active_stage;
    std::uint32_t state;
    std::uint32_t completion_marker;
    std::uint32_t planned_transaction_count;
    std::uint32_t transaction_call_count;
    std::uint32_t failed_transaction_index;
    std::uint32_t idf_internal_recovery_allowed;
    std::int32_t gpio_error;
    std::int32_t bus_init_error;
    std::int32_t device_add_error;
    std::int32_t transaction_error;
    std::uint8_t sda_before;
    std::uint8_t scl_before;
    std::uint8_t device_address;
    std::uint8_t register_address;
    std::uint8_t read_length;
    std::uint8_t transaction1_register;
    std::uint8_t transaction1_read_length;
    std::uint8_t transaction2_register;
    std::uint8_t transaction2_read_length;
    std::uint8_t raw_byte0;
    std::uint8_t raw_byte1;
    std::uint8_t expected_value_checked;
    std::uint16_t decoded_value;
    std::uint16_t expected_value;
    std::uint8_t ioe1_uid[2];
    std::uint16_t ioe1_uid_value;
    std::uint8_t ioe1_snapshot_start_register;
    std::uint8_t ioe1_snapshot_length;
    std::uint8_t ioe1_snapshot[6];
    std::uint8_t ioe1_address_probe_only;
    std::uint8_t ioe1_address_ack;
    std::uint8_t epd_en;
    std::uint8_t ip2315_gate;
    std::uint8_t tp_en;
    std::uint8_t tf_en;
    std::uint8_t epd_en_is_output;
    std::uint8_t ip2315_gate_is_output;
    std::uint8_t tp_en_is_output;
    std::uint8_t tf_en_is_output;
    std::uint8_t epd_en_input_level;
    std::uint8_t ip2315_gate_input_level;
    std::uint8_t tp_en_input_level;
    std::uint8_t tf_en_input_level;
};

extern "C" {
volatile PaperMonoClassAResult papermono_class_a_result = {};
}

namespace {
void initializeResult(const ReadOnlyStagePlan& plan) {
    papermono_class_a_result.magic = kResultMagic;
    papermono_class_a_result.active_stage = plan.stage;
    papermono_class_a_result.state = static_cast<std::uint32_t>(ProbeState::NOT_STARTED);
    papermono_class_a_result.completion_marker = 0;
    papermono_class_a_result.planned_transaction_count =
        1;
    papermono_class_a_result.transaction_call_count = 0;
    papermono_class_a_result.failed_transaction_index = 0;
    papermono_class_a_result.idf_internal_recovery_allowed = 1;
    papermono_class_a_result.gpio_error = kErrorNotSet;
    papermono_class_a_result.bus_init_error = kErrorNotSet;
    papermono_class_a_result.device_add_error = kErrorNotSet;
    papermono_class_a_result.transaction_error = kErrorNotSet;
    papermono_class_a_result.sda_before = 0xFF;
    papermono_class_a_result.scl_before = 0xFF;
    papermono_class_a_result.device_address = plan.device_address;
    papermono_class_a_result.register_address =
        plan.stage == PAPERMONO_STAGE_IOE1_READ_ONLY ? 0xFF : plan.register_address;
    papermono_class_a_result.read_length =
        plan.stage == PAPERMONO_STAGE_IOE1_READ_ONLY ? 0 : 2;
    papermono_class_a_result.transaction1_register = papermono_class_a_result.register_address;
    papermono_class_a_result.transaction1_read_length = papermono_class_a_result.read_length;
    papermono_class_a_result.transaction2_register = 0;
    papermono_class_a_result.transaction2_read_length = 0;
    papermono_class_a_result.raw_byte0 = 0;
    papermono_class_a_result.raw_byte1 = 0;
    papermono_class_a_result.expected_value_checked = plan.validate_expected_value ? 1 : 0;
    papermono_class_a_result.decoded_value = 0;
    papermono_class_a_result.expected_value = plan.expected_value;
    papermono_class_a_result.ioe1_uid[0] = 0;
    papermono_class_a_result.ioe1_uid[1] = 0;
    papermono_class_a_result.ioe1_uid_value = 0;
    papermono_class_a_result.ioe1_snapshot_start_register = kIoe1GpioModeRegister;
    papermono_class_a_result.ioe1_snapshot_length = 0;
    for (std::size_t i = 0; i < kIoe1GpioSnapshotLength; ++i) {
        papermono_class_a_result.ioe1_snapshot[i] = 0;
    }
    papermono_class_a_result.ioe1_address_probe_only =
        plan.stage == PAPERMONO_STAGE_IOE1_READ_ONLY ? 1 : 0;
    papermono_class_a_result.ioe1_address_ack = 0;
    papermono_class_a_result.epd_en = 0;
    papermono_class_a_result.ip2315_gate = 0;
    papermono_class_a_result.tp_en = 0;
    papermono_class_a_result.tf_en = 0;
    papermono_class_a_result.epd_en_is_output = 0;
    papermono_class_a_result.ip2315_gate_is_output = 0;
    papermono_class_a_result.tp_en_is_output = 0;
    papermono_class_a_result.tf_en_is_output = 0;
    papermono_class_a_result.epd_en_input_level = 0;
    papermono_class_a_result.ip2315_gate_input_level = 0;
    papermono_class_a_result.tp_en_input_level = 0;
    papermono_class_a_result.tf_en_input_level = 0;
}

void setState(ProbeState state) {
    papermono_class_a_result.state = static_cast<std::uint32_t>(state);
}

esp_err_t readRegisterOnce(
    i2c_master_dev_handle_t device_handle,
    std::uint8_t register_address,
    std::uint8_t* read_data,
    std::size_t read_length
) {
    ++papermono_class_a_result.transaction_call_count;
    const esp_err_t transaction_error = i2c_master_transmit_receive(
        device_handle,
        &register_address,
        1,
        read_data,
        read_length,
        kTransactionTimeoutMs
    );
    papermono_class_a_result.transaction_error = transaction_error;
    if (transaction_error != ESP_OK) {
        papermono_class_a_result.failed_transaction_index =
            papermono_class_a_result.transaction_call_count;
    }
    return transaction_error;
}

__attribute__((noinline)) void runClassAReadOnlyProbe(const ReadOnlyStagePlan& plan) {
    initializeResult(plan);

    vTaskDelay(pdMS_TO_TICKS(kStartupSettlingDelayMs));

    gpio_config_t idle_check_config = {};
    idle_check_config.pin_bit_mask = (1ULL << kSdaPin) | (1ULL << kSclPin);
    idle_check_config.mode = GPIO_MODE_INPUT;
    idle_check_config.pull_up_en = GPIO_PULLUP_DISABLE;
    idle_check_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    idle_check_config.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t gpio_error = gpio_config(&idle_check_config);
    papermono_class_a_result.gpio_error = gpio_error;
    if (gpio_error != ESP_OK) {
        setState(ProbeState::GPIO_INIT_FAILED);
        return;
    }

    papermono_class_a_result.sda_before = static_cast<std::uint8_t>(gpio_get_level(kSdaPin));
    papermono_class_a_result.scl_before = static_cast<std::uint8_t>(gpio_get_level(kSclPin));
    if (papermono_class_a_result.sda_before == 0 || papermono_class_a_result.scl_before == 0) {
        setState(ProbeState::BUS_NOT_IDLE);
        return;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_1;
    bus_config.sda_io_num = kSdaPin;
    bus_config.scl_io_num = kSclPin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.intr_priority = 0;
    bus_config.trans_queue_depth = 0;
    bus_config.flags.enable_internal_pullup = 1;
    bus_config.flags.allow_pd = 0;

    i2c_master_bus_handle_t bus_handle = nullptr;
    const esp_err_t bus_init_error = i2c_new_master_bus(&bus_config, &bus_handle);
    papermono_class_a_result.bus_init_error = bus_init_error;
    if (bus_init_error != ESP_OK) {
        setState(ProbeState::I2C_INIT_FAILED);
        return;
    }

    // Stage 2D deliberately performs an address-only transaction. Unlike the
    // synchronous register-read path, IDF 5.5.4's probe API preserves address
    // NACK as ESP_ERR_NOT_FOUND and timeout as ESP_ERR_TIMEOUT. No register
    // pointer or payload byte is sent, and there is no application retry.
    if (plan.stage == PAPERMONO_STAGE_IOE1_READ_ONLY) {
        papermono_class_a_result.transaction_call_count = 1;
        const esp_err_t probe_error =
            i2c_master_probe(bus_handle, plan.device_address, kTransactionTimeoutMs);
        papermono_class_a_result.transaction_error = probe_error;
        papermono_class_a_result.ioe1_address_ack = probe_error == ESP_OK ? 1 : 0;
        if (probe_error != ESP_OK) {
            papermono_class_a_result.failed_transaction_index = 1;
            setState(ProbeState::TRANSACTION_FAILED);
            return;
        }
        setState(ProbeState::PASS);
        return;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = plan.device_address;
    device_config.scl_speed_hz = kI2cFrequencyHz;
    device_config.scl_wait_us = kSclWaitUs;
    device_config.flags.disable_ack_check = 0;

    i2c_master_dev_handle_t device_handle = nullptr;
    const esp_err_t device_add_error = i2c_master_bus_add_device(bus_handle, &device_config, &device_handle);
    papermono_class_a_result.device_add_error = device_add_error;
    if (device_add_error != ESP_OK) {
        setState(ProbeState::I2C_INIT_FAILED);
        return;
    }

    if (plan.stage == PAPERMONO_STAGE_PM1_READ_ONLY) {
        std::uint8_t read_data[2] = {};
        if (readRegisterOnce(device_handle, plan.register_address, read_data, sizeof(read_data)) != ESP_OK) {
            setState(ProbeState::TRANSACTION_FAILED);
            return;
        }

        papermono_class_a_result.raw_byte0 = read_data[0];
        papermono_class_a_result.raw_byte1 = read_data[1];
        papermono_class_a_result.decoded_value =
            static_cast<std::uint16_t>(read_data[0]) |
            (static_cast<std::uint16_t>(read_data[1]) << 8);

        if (plan.validate_expected_value && papermono_class_a_result.decoded_value != plan.expected_value) {
            setState(ProbeState::ID_MISMATCH);
            return;
        }
    } else {
        std::uint8_t uid[kIoe1UidLength] = {};
        if (readRegisterOnce(device_handle, kIoe1UidRegister, uid, sizeof(uid)) != ESP_OK) {
            setState(ProbeState::TRANSACTION_FAILED);
            return;
        }
        papermono_class_a_result.ioe1_uid[0] = uid[0];
        papermono_class_a_result.ioe1_uid[1] = uid[1];
        papermono_class_a_result.ioe1_uid_value =
            static_cast<std::uint16_t>(uid[0]) |
            (static_cast<std::uint16_t>(uid[1]) << 8);
        papermono_class_a_result.raw_byte0 = uid[0];
        papermono_class_a_result.raw_byte1 = uid[1];
        papermono_class_a_result.decoded_value = papermono_class_a_result.ioe1_uid_value;

        std::uint8_t snapshot[kIoe1GpioSnapshotLength] = {};
        if (readRegisterOnce(device_handle, kIoe1GpioModeRegister, snapshot, sizeof(snapshot)) != ESP_OK) {
            setState(ProbeState::TRANSACTION_FAILED);
            return;
        }
        for (std::size_t i = 0; i < kIoe1GpioSnapshotLength; ++i) {
            papermono_class_a_result.ioe1_snapshot[i] = snapshot[i];
        }

        const std::uint8_t mode_low = snapshot[kIoe1GpioModeRegister - kIoe1GpioModeRegister];
        const std::uint8_t mode_high = snapshot[kIoe1GpioModeRegister + 1 - kIoe1GpioModeRegister];
        const std::uint8_t out_low = snapshot[kIoe1GpioOutRegister - kIoe1GpioModeRegister];
        const std::uint8_t out_high = snapshot[kIoe1GpioOutRegister + 1 - kIoe1GpioModeRegister];
        const std::uint8_t in_low = snapshot[kIoe1GpioInRegister - kIoe1GpioModeRegister];
        const std::uint8_t in_high = snapshot[kIoe1GpioInRegister + 1 - kIoe1GpioModeRegister];

        papermono_class_a_result.epd_en = (out_low & kIoe1Pyg3Mask) != 0;
        papermono_class_a_result.ip2315_gate = (out_high & kIoe1Pyg11Mask) != 0;
        papermono_class_a_result.tp_en = (out_high & kIoe1Pyg13Mask) != 0;
        papermono_class_a_result.tf_en = (out_high & kIoe1Pyg14Mask) != 0;
        papermono_class_a_result.epd_en_is_output = (mode_low & kIoe1Pyg3Mask) != 0;
        papermono_class_a_result.ip2315_gate_is_output = (mode_high & kIoe1Pyg11Mask) != 0;
        papermono_class_a_result.tp_en_is_output = (mode_high & kIoe1Pyg13Mask) != 0;
        papermono_class_a_result.tf_en_is_output = (mode_high & kIoe1Pyg14Mask) != 0;
        papermono_class_a_result.epd_en_input_level = (in_low & kIoe1Pyg3Mask) != 0;
        papermono_class_a_result.ip2315_gate_input_level = (in_high & kIoe1Pyg11Mask) != 0;
        papermono_class_a_result.tp_en_input_level = (in_high & kIoe1Pyg13Mask) != 0;
        papermono_class_a_result.tf_en_input_level = (in_high & kIoe1Pyg14Mask) != 0;
    }

    setState(ProbeState::PASS);
}

[[noreturn]] __attribute__((noinline)) void enterPermanentIdle() {
    vTaskSuspend(nullptr);
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
}  // namespace

void setup() {
    runClassAReadOnlyProbe(kActiveStagePlan);
    papermono_class_a_result.completion_marker = kCompletionMarker;
    enterPermanentIdle();
}

void loop() {
    enterPermanentIdle();
}
