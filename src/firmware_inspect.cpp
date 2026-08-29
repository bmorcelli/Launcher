#include "firmware_inspect.h"

#include "display.h"
#include "launcher_capabilities.h"
#include "launcher_storage.h"

#include <esp_app_desc.h>
#include <esp_app_format.h>

#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kPartitionTableOffset = 0x8000;
constexpr size_t kPartitionTableBytes = 0x1000;
constexpr size_t kPartitionEntryBytes = 32;
constexpr uint16_t kPartitionMagic = 0x50AA;
constexpr uint8_t kAppPartitionType = 0x00;
constexpr uint8_t kMaxImageSegments = 16;
constexpr uint8_t kImageHashBytes = 32;

enum class FirmwareKind : uint8_t {
    AppImage,
    FullFlashImage,
    Unsupported,
};

struct ImageInfo {
    bool valid = false;
    bool esp32S3 = false;
    uint32_t offset = 0;
    uint32_t imageBytes = 0;
    String project;
    String version;
};

struct InspectionResult {
    FirmwareKind kind = FirmwareKind::Unsupported;
    uint32_t fileBytes = 0;
    ImageInfo app;
    bool readFailure = false;
};

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

uint16_t readLe16(const uint8_t *bytes) {
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

bool spanFits(uint32_t fileBytes, uint64_t offset, uint64_t length) {
    return offset <= fileBytes && length <= static_cast<uint64_t>(fileBytes) - offset;
}

bool readExact(const String &path, uint32_t offset, void *buffer, size_t length) {
    return launcherStorageReadAt(path, offset, static_cast<uint8_t *>(buffer), length) ==
           LauncherStorageFileResult::Ready;
}

String descriptorText(const char *field, size_t fieldBytes) {
    size_t length = 0;
    while (length < fieldBytes && field[length] != '\0') ++length;
    String text;
    text.reserve(length);
    for (size_t index = 0; index < length; ++index) text += field[index];
    return text;
}

bool inspectAppImage(const String &path, uint32_t fileBytes, uint32_t offset, ImageInfo &image) {
    if (!spanFits(fileBytes, offset, sizeof(esp_image_header_t))) return false;

    esp_image_header_t header = {};
    if (!readExact(path, offset, &header, sizeof(header))) return false;
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > kMaxImageSegments) {
        return false;
    }

    uint64_t cursor = static_cast<uint64_t>(offset) + sizeof(header);
    esp_app_desc_t descriptor = {};
    bool descriptorFound = false;
    for (uint8_t segment = 0; segment < header.segment_count; ++segment) {
        if (!spanFits(fileBytes, cursor, sizeof(esp_image_segment_header_t))) return false;
        uint8_t segmentHeader[sizeof(esp_image_segment_header_t)] = {};
        if (!readExact(path, static_cast<uint32_t>(cursor), segmentHeader, sizeof(segmentHeader)))
            return false;

        const uint32_t segmentBytes = readLe32(segmentHeader + 4);
        cursor += sizeof(segmentHeader);
        if (!spanFits(fileBytes, cursor, segmentBytes)) return false;

        if (segment == 0 && segmentBytes >= sizeof(descriptor)) {
            if (!readExact(path, static_cast<uint32_t>(cursor), &descriptor, sizeof(descriptor)))
                return false;
            descriptorFound = descriptor.magic_word == ESP_APP_DESC_MAGIC_WORD;
        }
        cursor += segmentBytes;
    }

    uint64_t end = cursor + 1; // image checksum follows the final segment immediately
    if (header.hash_appended) end += kImageHashBytes;
    end = (end + 15U) & ~uint64_t(15U);
    if (!spanFits(fileBytes, offset, end - offset) || end <= offset) return false;

    image.valid = true;
    image.esp32S3 = header.chip_id == ESP_CHIP_ID_ESP32S3;
    image.offset = offset;
    image.imageBytes = static_cast<uint32_t>(end - offset);
    if (descriptorFound) {
        image.project = descriptorText(descriptor.project_name, sizeof(descriptor.project_name));
        image.version = descriptorText(descriptor.version, sizeof(descriptor.version));
    }
    return true;
}

bool inspectFullFlashImage(const String &path, uint32_t fileBytes, ImageInfo &image) {
    if (!spanFits(fileBytes, kPartitionTableOffset, kPartitionTableBytes)) return false;

    uint8_t table[kPartitionTableBytes] = {};
    if (!readExact(path, kPartitionTableOffset, table, sizeof(table))) return false;

    for (size_t entryOffset = 0; entryOffset + kPartitionEntryBytes <= sizeof(table);
         entryOffset += kPartitionEntryBytes) {
        const uint8_t *entry = table + entryOffset;
        if ((entry[0] == 0xFF && entry[1] == 0xFF) || (entry[0] == 0xEB && entry[1] == 0xEB)) break;
        if (readLe16(entry) != kPartitionMagic || entry[2] != kAppPartitionType) continue;

        const uint32_t appOffset = readLe32(entry + 4);
        const uint32_t declaredBytes = readLe32(entry + 8);
        if (appOffset == 0 || declaredBytes == 0 || !spanFits(fileBytes, appOffset, 1)) continue;

        ImageInfo candidate;
        if (!inspectAppImage(path, fileBytes, appOffset, candidate)) continue;
        if (candidate.imageBytes > declaredBytes) continue;
        image = candidate;
        return true;
    }
    return false;
}

InspectionResult inspectFirmware(const String &path) {
    InspectionResult result;
    if (launcherStorageFileSize(path, result.fileBytes) != LauncherStorageFileResult::Ready ||
        result.fileBytes == 0) {
        result.readFailure = true;
        return result;
    }

    ImageInfo merged;
    if (inspectFullFlashImage(path, result.fileBytes, merged)) {
        result.kind = merged.esp32S3 ? FirmwareKind::FullFlashImage : FirmwareKind::Unsupported;
        result.app = merged;
        return result;
    }

    ImageInfo app;
    if (inspectAppImage(path, result.fileBytes, 0, app)) {
        result.kind = app.esp32S3 ? FirmwareKind::AppImage : FirmwareKind::Unsupported;
        result.app = app;
        return result;
    }
    return result;
}

String shortFileName(const String &path) {
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

String bytesText(uint32_t bytes) { return String(bytes / 1024U) + " KiB (" + String(bytes) + " B)"; }

void addReportLine(std::vector<Option> &options, const String &text) {
    options.push_back({text, []() {}});
}

void showInspection(const String &path, const InspectionResult &result) {
    std::vector<Option> options;
    addReportLine(options, "File: " + shortFileName(path));
    if (result.readFailure) {
        addReportLine(options, "Read failed");
    } else {
        addReportLine(options, "Size: " + bytesText(result.fileBytes));
        if (result.kind == FirmwareKind::AppImage) {
            addReportLine(options, "ESP32-S3 APP IMAGE");
        } else if (result.kind == FirmwareKind::FullFlashImage) {
            addReportLine(options, "MERGED / FULL-FLASH");
            addReportLine(options, "App offset: 0x" + String(result.app.offset, HEX));
        } else {
            addReportLine(options, "INVALID / UNSUPPORTED");
        }
        if (result.app.valid) {
            addReportLine(options, "Chip: " + String(result.app.esp32S3 ? "ESP32-S3" : "Other"));
            if (!result.app.project.isEmpty()) addReportLine(options, "Project: " + result.app.project);
            if (!result.app.version.isEmpty()) addReportLine(options, "Version: " + result.app.version);
        }
    }
    addReportLine(options, "Inspection only");
    options.push_back({"Back", []() {}});
    loopOptions(options);
}

} // namespace

void launcherInspectFirmwareFile(const String &path) {
    if (!launcherFirmwareInspectAllowed()) return;
    showInspection(path, inspectFirmware(path));
}

bool launcherValidateFirmwareFile(const String &path, LauncherFirmwareValidation &validation) {
    validation = LauncherFirmwareValidation();
    const InspectionResult result = inspectFirmware(path);
    validation.fileBytes = result.fileBytes;
    if (result.kind == FirmwareKind::FullFlashImage) {
        validation.kind = LauncherFirmwareKind::FullFlash;
        validation.esp32S3 = result.app.esp32S3;
        validation.imageBytes = result.app.imageBytes;
        return false;
    }
    if (result.kind != FirmwareKind::AppImage || !result.app.valid || !result.app.esp32S3) return false;
    validation.kind = LauncherFirmwareKind::StandaloneApp;
    validation.esp32S3 = true;
    validation.imageBytes = result.app.imageBytes;
    return validation.fileBytes != 0 && validation.imageBytes != 0;
}
