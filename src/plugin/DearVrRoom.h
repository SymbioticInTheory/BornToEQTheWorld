#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

// An optional stereo room, recorded from the user's installed dearVR MIX.
//
// A dearVR scene with fixed settings is a linear, time-invariant stereo
// process, so four impulse responses describe it completely: left in to left
// out, left to right, right to left and right to right. BEQ records those
// once per scene and sample rate (dearVR runs offline for a few seconds),
// keeps them in its settings folder, and from then on applies the room by
// convolution without loading dearVR. Equalizer APO can run the same
// responses, so the room also works system-wide.
class DearVrRoom {
public:
    explicit DearVrRoom(juce::AudioProcessor& parent) : owner(parent) {}

    // Turns the room on with a scene, or off with an empty name. Records the
    // scene first if it hasn't been recorded at this sample rate, which needs
    // dearVR MIX installed. Call on the message thread.
    juce::String select(const juce::String& scene);
    // The scenes dearVR offers: remembered from the last time dearVR was opened.
    juce::StringArray scenes() const;
    bool loaded() const { return engine != nullptr; }
    float tailSeconds() const { return tail.load(); }
    // The sample rate the room was recorded at, when it differs from the one now
    // playing (JUCE resamples it meanwhile). The processor records it again.
    bool needsRecapture() const;

    void prepare(double sampleRate, int maximumBlockSize, bool stereo);
    void release();
    void process(juce::AudioBuffer<float>&);
    void updateTiming();

    // The four responses (LL, LR, RL, RR) of a scene at a sample rate, from
    // the saved copy or recorded now. Message thread.
    static bool responses(const juce::String& scene, double sampleRate, juce::AudioBuffer<float>& ir, juce::String& error);
    // Records them from dearVR. Message thread.
    static bool capture(const juce::String& scene, double sampleRate, juce::AudioBuffer<float>& ir, juce::String& error);
    static juce::File folder();
    static juce::AudioFormatWriterOptions floatWav(double sampleRate, int channels);

    // Opens a separate dearVR MIX and lists every setting it exposes, with all
    // the choices of each stepped one. For working out which setting picks the
    // headphone model. Call on the message thread.
    static juce::String describeParameters(juce::String& error);

private:
    struct Engine {
        // One convolution per input side, each with a stereo response: what the
        // left input sends to the left and right outputs, and the same for right.
        juce::dsp::Convolution fromLeft, fromRight;
        juce::AudioBuffer<float> left, right;
        double irRate = 0.0;
        int blockSize = 0;
        void prepare(double sampleRate, int block);
        void process(juce::AudioBuffer<float>&);
    };

    static juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance&, const juce::String& name);
    static std::unique_ptr<juce::AudioPluginInstance> openInstance(double sampleRate, int block, juce::String& error);
    static juce::File responseFile(const juce::String& scene, double sampleRate);
    static void rememberScenes(const juce::StringArray&);

    juce::AudioProcessor& owner;
    std::unique_ptr<Engine> engine;
    double rate = 0.0;
    int blockSize = 0;
    bool useStereo = false;
    std::atomic<float> tail { 0.0f };
};
