#pragma once

class PaperMonoBootstrap {
public:
    bool begin();
    bool ready() const;

private:
    bool beginAttempted_ = false;
    bool ready_ = false;
};
