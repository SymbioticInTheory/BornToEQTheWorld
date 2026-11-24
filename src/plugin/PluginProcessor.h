#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/GraphicEq.h"
#include "dsp/HeadphoneLibrary.h"

#include <atomic>
#include <memory>

// The headphone library, decompressed and parsed once per process and shared
// by every instance of the plugin.
struct LibraryHolder {
    LibraryHolder();
    bteq::HeadphoneLibrary library;
    bool loaded = false;
};

class BteqProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    BteqProcessor();
    ~BteqProcessor() override;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.4; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Used by the editor (message thread only).
    const bteq::HeadphoneLibrary& library() const { return holder->library; }
    bool libraryLoaded() const { return holder->loaded; }
    void selectHeadphone(const juce::String& name);
    void selectProfile(int indexWithinHeadphone);
    juce::String selectedHeadphone() const { return headphoneName; }
    int selectedProfileIndex() const { return profileIndex; }
    const bteq::HeadphoneProfile* selectedProfile() const;
    bool graphicAvailable() const;
    bool graphicModeActive() const;
    double amount() const; // 0..1, 0 when bypassed
    double currentPreampDb() const;

    int accentIndex = 0; // UI colour choice, saved with the session
    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void rebuild(bool force);
    void buildParametric(const bteq::HeadphoneProfile* p, double k, double sr);
    void buildGraphic(const bteq::HeadphoneProfile* p, double k, double sr);

    juce::SharedResourcePointer<LibraryHolder> holder;
    juce::CriticalSection buildLock; // selection + engine building

    juce::String headphoneName;
    int profileIndex = 0;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* trimParam = nullptr;

    // Parametric engine: built on the message thread, swapped in on the audio
    // thread without blocking.
    juce::SpinLock swapLock;
    std::unique_ptr<bteq::ParametricEq> activeEq, pendingEq;
    std::atomic<bool> pendingReady { false };

    // Graphic engine (thread-safe IR loading is handled by juce::dsp::Convolution).
    juce::dsp::Convolution convolution;
    std::atomic<bool> graphicReady { false };
    bool lastBlockGraphic = false;

    juce::SmoothedValue<float> trimGain;
    double sampleRate = 48000;
    int maxBlock = 512;

    // What the engines were last built for.
    const bteq::HeadphoneProfile* builtProfile = nullptr;
    double builtAmount = -1, builtRate = 0;
    bool builtGraphic = false;
    double preampDb = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BteqProcessor)
};
