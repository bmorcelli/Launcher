// Native shim: minimal stand-ins for Arduino's FS.h types. Nothing here talks
// to a real filesystem — the UI code paths this harness exercises don't call
// through these, they only need the types to exist.
#pragma once

#include "Stream.h"

class File : public Stream {
public:
    operator bool() const { return false; }
    bool isDirectory() const { return false; }
    String name() const { return String(); }
    size_t size() const { return 0; }
    void close() {}
};

class FS {
public:
    File open(const char *, const char * = "r") { return File(); }
    bool exists(const char *) { return false; }
    bool remove(const char *) { return false; }
    bool mkdir(const char *) { return false; }
    bool rmdir(const char *) { return false; }
};
