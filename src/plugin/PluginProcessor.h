#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/CurveLibrary.h"
#include "dsp/GraphicEq.h"
#include "dsp/HeadphoneLibrary.h"

#include <atomic>
#include <map>
#include <memory>

// The headphone library, decompressed and parsed once per process and shared
// by every instance of the plugin.
struct LibraryHolder {
    LibraryHolder();
    bteq::HeadphoneLibrary library;
    bool loaded = false;
};

// Measurements and targets for the advanced graph. Loaded the first time the
// advanced view is opened, then shared by every instance.
struct CurvesHolder {
    CurvesHolder();
    bteq::CurveLibrary curves;
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
    double currentPreampDb() const; // what is applied now (scaled by the correction amount)

    // Your own changes to a profile, kept per profile and saved with the session.
    struct ProfileEdit {
        bool filtersEdited = false, preampEdited = false;
        std::vector<bteq::FilterSpec> filters;
        double preampDb = 0;
    };
    bteq::ParametricPreset effectiveParametric() const; // published filters with your edits applied
    double publishedPreampDb() const;                   // preamp before any edits
    double preampSettingDb() const;                     // preamp at 100% correction, with your edit
    std::vector<double> graphicShapeDb() const;         // graphic curve with the published preamp taken out
    bool selectionEdited() const;
    void setFilter(int index, const bteq::FilterSpec& spec);
    void setPreamp(double db);
    void resetEdits();
    void resetPreamp(); // back to the published preamp, keeping filter edits

    // Make the selected headphone sound like another one (graphic EQ built
    // from the two measurements). Empty name turns it off.
    struct Match {
        bool active = false, sameRig = true;
        std::vector<double> shape, ownMeasured, otherMeasured;
        double preampDb = 0;
        juce::String source; // where the other headphone's measurement comes from
    };
    void setMatch(const juce::String& otherHeadphone);
    juce::String matchedHeadphone() const { return matchName; }
    const Match& currentMatch() const { return match; }
    const bteq::CurveLibrary* curveLibrary(); // loaded on first use

    // Snapshot of the current choice (headphone, profile, mode, correction and
    // your edits), used by the saved profiles menu.
    juce::ValueTree makeSnapshot() const;
    void loadSnapshot(const juce::ValueTree& snapshot);

    int accentIndex = 0; // UI colour choice, saved with the session
    bool advancedView = false; // UI: target and result curves, saved with the session
    int hiddenCurves = 0;      // UI: bit mask of curves switched off in the advanced legend
    float uiScale = 0.0f;      // UI: window scale, 0 = use the last scale picked in any instance
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
    std::map<juce::String, ProfileEdit> edits; // keyed by profile key
    int editVersion = 0, builtEditVersion = -1;
    const ProfileEdit* currentEdit() const;
    int indexForKey(const juce::String& headphone, const juce::String& key) const;
    juce::String editKey() const;
    void computeMatch();
    juce::String matchName;
    Match match;
    std::unique_ptr<juce::SharedResourcePointer<CurvesHolder>> curvesHolder;

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
