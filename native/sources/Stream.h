// Native shim: just enough of Arduino's Stream to satisfy signatures that
// take one by reference. Nothing calls through it in the UI code paths this
// harness exercises.
#pragma once

class Stream {
public:
    virtual ~Stream() = default;
};
