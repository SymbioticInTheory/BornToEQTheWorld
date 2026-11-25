#pragma once

#include <juce_core/juce_core.h>

// Writes BEQ's EQ into Equalizer APO's config folder, so the standalone can
// set the EQ for the whole system the way Peace does. Message thread only.
class ApoBridge {
public:
    // APO's config folder: from the registry, else the default install path.
    // BTEQ_APO_CONFIG_DIR overrides it (used for testing). Empty if not found.
    static juce::File findConfigDir();

    // Includes BEQ's file in config.txt and comments out the other active
    // lines (a copy of the old config.txt is kept as config.txt.beq-backup).
    bool start(juce::String& error);
    // Puts config.txt back as it was and empties BEQ's file.
    void stop();
    bool write(const juce::String& text, juce::String& error);
    bool running() const { return on; }
    juce::File folder() const { return dir; }

private:
    static bool replaceFile(const juce::File& target, const juce::String& text, juce::String& error);
    juce::File dir;
    bool on = false;
};
