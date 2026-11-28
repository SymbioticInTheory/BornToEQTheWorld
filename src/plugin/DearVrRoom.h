#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// An optional stereo room stage backed by the user's installed dearVR MIX.
class DearVrRoom {
public:
    explicit DearVrRoom(juce::AudioProcessor& parent) : owner(parent) {}

    // Windows VST3 installation. Call on the message thread.
    juce::String select(const juce::String& scene);
    juce::StringArray scenes() const { return sceneNames; }
    bool loaded() const { return instance != nullptr; }
    float tailSeconds() const { return tail.load(); }

    void prepare(double sampleRate, int maximumBlockSize, bool stereo);
    void release();
    void process(juce::AudioBuffer<float>&);
    void updateTiming();

    // Opens a separate dearVR MIX (the room in use is untouched) and lists every
    // setting it exposes, with all the choices of each stepped one. For working
    // out which setting picks the headphone model. Call on the message thread.
    static juce::String describeParameters(juce::String& error);

private:
    static juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance&, const juce::String& name);
    static std::unique_ptr<juce::AudioPluginInstance> openInstance(double sampleRate, int block, juce::String& error);
    void prepareInstance();

    juce::AudioProcessor& owner;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioProcessorParameter* sceneParameter = nullptr;
    juce::StringArray sceneNames;
    juce::MidiBuffer midi;
    double rate = 0.0;
    int blockSize = 0;
    bool useStereo = false;
    bool prepared = false;
    std::atomic<float> tail { 0.0f };
};
