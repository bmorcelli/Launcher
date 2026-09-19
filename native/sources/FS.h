// Native shim: minimal stand-ins for Arduino's FS.h types, backed by a small
// in-memory fake filesystem so real navigation code (sd_functions.cpp's
// readFs()/loopSD()) can walk it for real instead of being stubbed out.
// Extend kFakeFs below to try different folder layouts.
#pragma once

#include "Stream.h"

#include <cstdint>
#include <string>
#include <vector>

struct FakeFsEntry {
    const char *parent; // exact path this entry lives directly under ("/", "/Documents", ...)
    const char *name;   // just the name, no slashes
    bool isDir;
};

// clang-format off
static const FakeFsEntry kFakeFs[] = {
    {"/",          "Documents",       true },
    {"/",          "Firmware",        true },
    {"/",          "Backup",          true },
    {"/",          "readme.txt",      false},
    {"/",          "config.conf",     false},
    {"/",          "firmware.bin",    false},
    {"/Documents", "notes.txt",       false},
    {"/Documents", "todo.txt",        false},
    {"/Firmware",  "marauder.bin",    false},
    {"/Firmware",  "bruce.bin",       false},
    {"/Backup",    "config.conf.bak", false},
};
// clang-format on

inline std::string fakeFsJoin(const std::string &dir, const std::string &name) {
    if (dir == "/") return "/" + name;
    return dir + "/" + name;
}

class File : public Stream {
public:
    File() = default;
    // path: the entry this File refers to. asDir: true for the root and any
    // directory found in kFakeFs; a file open still gets a File (Stream-like,
    // though nothing here reads/writes its bytes).
    File(std::string path, bool asDir) : _path(std::move(path)), _isDir(asDir), _valid(true) {}

    operator bool() const { return _valid; }
    bool isDirectory() const { return _isDir; }
    String name() const {
        auto slash = _path.find_last_of('/');
        return String((slash == std::string::npos ? _path : _path.substr(slash + 1)).c_str());
    }
    size_t size() const { return 0; }
    void close() {}
    void flush() {}
    void rewindDirectory() { _iter = 0; }
    bool seek(uint32_t) { return true; }
    int read() { return -1; }
    size_t read(uint8_t *, size_t) { return 0; }
    size_t readBytes(char *, size_t) { return 0; }
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *, size_t len) override { return len; }

    // Matches fs::File::getNextFileName(bool*) from the ESP32 Arduino core:
    // returns the next child's full path, "" once exhausted.
    String getNextFileName(bool *isDir = nullptr) {
        if (!_isDir) return String();
        while (_iter < sizeof(kFakeFs) / sizeof(kFakeFs[0])) {
            const FakeFsEntry &e = kFakeFs[_iter++];
            if (_path == e.parent) {
                if (isDir) *isDir = e.isDir;
                return String(fakeFsJoin(_path, e.name).c_str());
            }
        }
        return String();
    }

private:
    std::string _path;
    bool _isDir = false;
    bool _valid = false;
    size_t _iter = 0;
};

class FS {
public:
    File open(const char *path, const char * = "r", bool = false) {
        std::string p = path ? path : "";
        if (p.size() > 1 && p.back() == '/') p.pop_back();
        if (p.empty()) p = "/";
        if (p == "/") return File("/", true);
        for (const auto &e : kFakeFs) {
            if (fakeFsJoin(e.parent, e.name) == p) return File(p, e.isDir);
        }
        return File();
    }
    File open(const String &path, const char *mode = "r", bool create = false) {
        return open(path.c_str(), mode, create);
    }
    bool exists(const char *path) {
        File f = open(path);
        return (bool)f;
    }
    bool exists(const String &path) { return exists(path.c_str()); }
    bool remove(const char *) { return false; }
    bool remove(const String &) { return false; }
    bool mkdir(const char *) { return false; }
    bool mkdir(const String &) { return false; }
    bool rmdir(const char *) { return false; }
    bool rmdir(const String &) { return false; }
};
