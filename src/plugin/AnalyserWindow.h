#pragma once

// The spectrum analyser's views: drawing it (behind the main curve, or in its own
// window) and the pop-out window with every setting. Each view registers with the
// processor's tap while it exists, so nothing runs while none is showing.

#include "PluginEditor.h"

// Draws the analyser into `area`, whose left and right edges are settings.fLo and fHi.
void paintSpectrum(juce::Graphics&, juce::Rectangle<float> area, const bteq::spectrum::Analyser&, juce::Colour accent,
                   float scale = 1.0f);

// Hooks a view's analyser up to the processor: in the standalone, "before
// correction" takes BEQ's EQ back out of what Windows plays.
void connectAnalyser(bteq::spectrum::Analyser&, const BteqProcessor&);
// Reads new audio into the analyser (starting the average again if the source changed).
void feedAnalyser(bteq::spectrum::Analyser&, const BteqProcessor&, int& seenGeneration);

class AnalyserPanel : public juce::Component, private juce::Timer {
public:
    AnalyserPanel(BteqProcessor& p, const Theme& t, juce::LookAndFeel& lnf);
    ~AnalyserPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::Colour accent;

private:
    void timerCallback() override;
    void followSettings();
    void change(const std::function<void(bteq::spectrum::Settings&)>& edit);
    void showChoices(juce::TextButton& b, const juce::StringArray& names, int current, std::function<void(int)> pick);
    void refreshControls();
    void showSmoothMenu();
    juce::Rectangle<int> knobValueArea(int knob) const;
    juce::Rectangle<float> plotArea() const;
    // My EQ over the spectrum, on its own +-12 dB scale: drag its points to tune it.
    static constexpr double kEqRange = 12.0;
    float xOf(double f) const;
    double freqAt(float x) const;
    float eqY(double db) const;
    juce::Point<float> nodePos(const bteq::FilterSpec& f) const;
    int nodeAt(juce::Point<float> p) const;
    void paintMyEq(juce::Graphics&);
    // The dB range bar: the whole span the range knobs allow, with the part shown as its thumb.
    static constexpr double kRangeBottom = -150.0, kRangeTop = 20.0;
    juce::Rectangle<float> rangeBar() const;
    float rangeY(double db) const;
    void setRange(double lo, double hi);
    void paintRangeBar(juce::Graphics&);
    int rangeDrag = 0; // 1: top end, 2: bottom end, 3: the whole thumb
    double rangeStartLo = 0, rangeStartHi = 0;
    bool overRangeBar = false;
    void editBand(int i, const std::function<void(bteq::FilterSpec&)>& edit);
    int dragIndex = -1;

    BteqProcessor& proc;
    const Theme& theme;
    bteq::spectrum::Analyser analyser;
    int seenVersion = -1, seenGeneration = -1;
    juce::uint32 lastChange = 0;
    bool unsaved = false;
    float mouseX = -1;

    juce::TextButton whereButton, viewButton, windowButton, blockButton, resetButton { "Reset" }, defaultsButton { "Defaults" };
    juce::TextButton myEqButton { "My EQ" }, foldButton;
    struct Knob {
        ValueKnob knob;
        juce::String name;
        std::function<juce::String(double)> text;
    };
    std::array<Knob, 8> knobs;
    enum { kAvg, kOverlap, kSlope, kSmooth, kLo, kHi, kFLo, kFHi };
    juce::Rectangle<int> controlsArea;
};

class AnalyserWindow : public juce::DocumentWindow {
public:
    AnalyserWindow(BteqProcessor& p, const Theme& t, juce::LookAndFeel& lnf, juce::Colour accent, std::function<void()> onClose);
    void closeButtonPressed() override;
    void setAccent(juce::Colour c);

private:
    std::function<void()> closed;
    AnalyserPanel* panel = nullptr;
};
