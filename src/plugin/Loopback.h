#pragma once

// Windows only: listens to what the default output device is playing (WASAPI
// loopback), for the standalone's analyser. The standalone's sound goes through
// Equalizer APO rather than through BEQ, so this is how its analyser hears it.
// No virtual cable needed. Runs only between start() and stop(); elsewhere it
// does nothing and start() returns false.

#include "Spectrum.h"

#include <memory>

class SystemLoopback {
public:
    SystemLoopback();
    ~SystemLoopback();
    // Starts a capture thread that writes into `tap` (and marks it as fed from
    // outside, so the audio callback leaves it alone).
    bool start(bteq::spectrum::Tap& tap);
    void stop();
    bool running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
