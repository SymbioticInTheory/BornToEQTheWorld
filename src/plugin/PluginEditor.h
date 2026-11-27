#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

// Fonts and colours shared by the editor's components.
struct Theme {
    Theme();
    juce::Typeface::Ptr blackletter, condSemi, condBold, body, bodySemi;

    juce::Font titleFont(float h) const { return juce::Font(juce::FontOptions(blackletter).withHeight(h)); }
    juce::Font labelFont(float h) const {
        return juce::Font(juce::FontOptions(condBold).withHeight(h)).withExtraKerningFactor(0.14f);
    }
    juce::Font condFont(float h) const { return juce::Font(juce::FontOptions(condSemi).withHeight(h)); }
    juce::Font bodyFont(float h, bool semi = false) const {
        return juce::Font(juce::FontOptions(semi ? bodySemi : body).withHeight(h));
    }

    static juce::Colour accent(int index);
    static juce::String accentName(int index);
    // Advanced view colours that stay clear of the chosen accent (used for the corrected curve).
    static juce::Colour measuredFor(int accentIndex);
    static juce::Colour eqFor(int accentIndex);
    static juce::Colour myEqFor(int accentIndex); // My EQ curve: lilac against light accents
    // Four built-in colours, and a fifth picked from your own background image.
    static constexpr int numAccents = 5, numBuiltIn = 4, imageAccent = 4;
    // Set from the background image (see ImageTheme.h); shared by every open window.
    static inline bool hasImage = false, imageVivid = true;
    static inline bool lowMemory = false; // no background picture (settings.xml lowMemory)
    static inline juce::Colour imageColour { 0xffD9D5CD }, imageMeasured { 0xff4F86D6 }, imageEq { 0xffD2607E },
        fog { 0xff0A0B0C };

    static inline const juce::Colour text { 0xffE8E4DC }, textBright { 0xffF2EEE6 }, muted { 0xffA6A49F },
        soft { 0xffBEBBB5 }, panel { 0xbd0C0D0F }, panelBorder { 0x17ffffff }, field { 0xb3060708 },
        selected { 0x1fE8E4DC }, grid { 0x14ffffff };
};

class BteqLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit BteqLookAndFeel(const Theme& t);
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    juce::Colour accentColour;

private:
    const Theme& theme;
};

// The standalone window's title bar: black, with the Standard Model
// Lagrangian drawn in the accent colour.
class TitleBarLookAndFeel : public BteqLookAndFeel {
public:
    explicit TitleBarLookAndFeel(const Theme& t);
    void drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW,
                                    const juce::Image* icon, bool drawTitleTextOnLeft) override;
    // The window's Options button: a small outlined pill that fits the bar.
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;

private:
    juce::Image formula, scaled; // white on transparent, tinted when drawn
    const Theme& font;
};

// The response curve. In parametric mode the numbered points can be dragged
// (frequency and gain), and scrolling over a point changes its Q.
class ResponseGraph : public juce::Component, private juce::Timer {
public:
    ResponseGraph(BteqProcessor& p, const Theme& t);
    ~ResponseGraph() override;
    void setAnalyser(bool on); // the spectrum behind the curve
    std::function<void()> onPopOut; // open the analyser window
    void paint(juce::Graphics&) override;
    void resized() override;
    void setAdvanced(bool on);
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override; // on a point: switch that filter on or off
    void refreshMyEqButton();
    juce::Colour accent;
    std::function<void()> onMyEqToggled;

private:
    juce::Rectangle<float> plotArea() const;
    float xOf(double f) const;
    float yOf(double db) const;
    juce::Point<float> nodePosition(const bteq::FilterSpec& f, double k) const;
    int nodeAt(juce::Point<float> pos) const;
    double rangeDb() const { return proc.advancedView ? 18.0 : 12.0; }
    void paintAdvanced(juce::Graphics&, const bteq::HeadphoneProfile&, double k, juce::Colour curveColour);
    void paintEq(juce::Graphics&, double k, juce::Colour colour, float width, bool nodes);
    void paintMyEq(juce::Graphics&, juce::Colour colour, float width, bool nodes, bool dashed);
    void paintNodes(juce::Graphics&, const std::vector<bteq::FilterSpec>& filters, double k, juce::Colour colour);
    std::vector<bteq::FilterSpec> nodeFilters(double& k) const; // the points that can be dragged now
    void setNodeFilter(int index, const bteq::FilterSpec& f);
    int dragIndex = -1;
    BteqProcessor& proc;
    const Theme& theme;
    juce::TextButton advancedButton { "Advanced" }, myEqButton { "My EQ" }, scopeButton;
    juce::TextButton analyserButton { "Analyser" }, popOutButton { juce::String::fromUTF8("\xe2\x86\x97") };
    void timerCallback() override;
    std::unique_ptr<bteq::spectrum::Analyser> overlay; // only while the analyser is on
    int overlayVersion = -1, overlayGeneration = -1;
    std::unique_ptr<juce::SharedResourcePointer<CurvesHolder>> curves; // loaded on first use
    enum Curve { Target, Measured, Corrected, Eq, numCurves };
    std::array<juce::Rectangle<float>, numCurves> legendHit;
};

// Rotary knob that reports right-clicks (used to type an exact value).
class ValueKnob : public juce::Slider {
public:
    std::function<void()> onRightClick, onDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (onDoubleClick) onDoubleClick();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            if (onRightClick) onRightClick();
            return;
        }
        juce::Slider::mouseDown(e);
    }
};

// A value on a filter card: drag up or down to change it, or click to type.
class DragLabel : public juce::Label {
public:
    std::function<void()> onDragStart;
    std::function<void(int pixelsUp, bool fine)> onDrag; // movement since the last call; Shift for fine steps
    void mouseDown(const juce::MouseEvent& e) override {
        dragging = false;
        lastY = e.getPosition().y;
        juce::Label::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!isEditableOnSingleClick()) return;
        if (!dragging && std::abs(e.getDistanceFromDragStartY()) < 3) return;
        if (!dragging) {
            dragging = true;
            if (onDragStart) onDragStart();
        }
        const int y = e.getPosition().y;
        if (onDrag && y != lastY) onDrag(lastY - y, e.mods.isShiftDown());
        lastY = y;
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragging) {
            dragging = false;
            return; // a drag, not a click: don't open the text editor
        }
        juce::Label::mouseUp(e);
    }

private:
    bool dragging = false;
    int lastY = 0;
};

// Filter cards. Each value can be dragged up or down, or clicked to type into.
class FiltersPanel : public juce::Component {
public:
    static constexpr int maxCards = 10;
    FiltersPanel(BteqProcessor& p, const Theme& t);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // update the values from the processor (message thread)
    void mouseDown(const juce::MouseEvent&) override; // click a My EQ band's type to change it
    juce::Colour accent;

private:
    enum Field { Freq, Gain, Q, numFields };
    std::vector<bteq::FilterSpec> shownFilters() const;
    juce::TextButton myEqOnButton { "On" }, autoGainButton { "Auto" };
    ValueKnob gainKnob; // My EQ's own gain
    juce::Label gainValue;
    juce::Rectangle<int> cardBounds(int i) const;
    void commit(int card, Field field, const juce::String& text);
    BteqProcessor& proc;
    const Theme& theme;
    std::array<std::array<DragLabel, numFields>, maxCards> values;
    void dragValue(int card, Field field, int pixelsUp, bool fine);
    double dragPosition = 0; // the dragged value before rounding (log2 for frequency and Q)
};

class BteqEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BteqEditor(BteqProcessor&);
    ~BteqEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Everything is laid out at 1080 x 680 inside this component, which is
    // scaled to the size picked from the right-click menu.
    struct Content : juce::Component {
        explicit Content(BteqEditor& e) : ed(e) {}
        void paint(juce::Graphics& g) override { ed.paintContent(g); }
        BteqEditor& ed;
    };
    void paintContent(juce::Graphics&);
    void layoutContent();
    void mouseDown(const juce::MouseEvent&) override; // right-clicks anywhere in the window
    void showScaleMenu(bool withSizes = true);
    void showBackgroundMenu();
    void setUiScale(float scale);
    // Background: the forest, or your own image (stored next to settings.xml) with colours picked from it.
    static juce::File backgroundFile();
    void loadBackground();
    void chooseBackground();
    void useBuiltInBackground(const juce::String& name); // Sunset, Ocean, ... (made by tools/make_backgrounds.py)
    void storeBackground(const juce::Image& img, const juce::String& name);
    void useForest();
    void backgroundChanged();
    void setLowMemory(bool on);
    void timerCallback() override;
    void refreshHeadphones();
    void refreshTargets();
    void showMatchChooser(juce::Rectangle<int> rowInList);
    static constexpr int kMatchRow = -2;     // target list row for "sound like another headphone"
    static constexpr int kNewPresetRow = -3; // target list row for "+ New preset"
    void showNewPresetMenu();
    void showCustomPresetMenu(int profileIndex);
    void newPreset(const bteq::ParametricPreset& preset, const juce::String& suggestedName);
    std::unique_ptr<juce::FileChooser> fileChooser;
    void setAccent(int index);
    void setMode(bool graphic);
    void refreshEditing();
    void showSavedMenu();
    void askForName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> done);
    static juce::File savedProfilesFile();
    static juce::ValueTree loadSavedProfiles();
    static void storeSavedProfiles(const juce::ValueTree&);

    // Headphone search results.
    struct HeadphoneModel : juce::ListBoxModel {
        BteqEditor& ed;
        explicit HeadphoneModel(BteqEditor& e) : ed(e) {}
        int getNumRows() override { return (int)ed.results.size(); }
        void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    };
    // Profiles (targets) for the selected headphone, with group headings.
    struct TargetRow { bool header; juce::String text; int profile; };
    struct TargetModel : juce::ListBoxModel {
        BteqEditor& ed;
        explicit TargetModel(BteqEditor& e) : ed(e) {}
        int getNumRows() override { return (int)ed.targetRows.size(); }
        void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    };

    // Panel rectangles, worked out by layoutContent() from what is collapsed.
    juce::Rectangle<int> rHeadphone, rTarget, rTabs, rGraph, rFilters, rCorrection, rSummary, rColour;
    void toggleCollapse(int what); // 0 whole left side, 1 headphone box, 2 target box
    juce::String headphoneSummary() const, targetSummary() const;

    BteqProcessor& proc;
    juce::SharedResourcePointer<Theme> theme;
    BteqLookAndFeel lnf;
    TitleBarLookAndFeel titleLnf { *theme };
    juce::Component::SafePointer<juce::DocumentWindow> titleWindow; // standalone only
    void styleTitleBar();
    juce::Image background;
    juce::Image titleImage; // the Death-style title, redrawn when the colour changes
    juce::Rectangle<float> titleArea;
    Content content { *this };
    ValueKnob preampKnob;

    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::TextEditor search;
    HeadphoneModel headphoneModel { *this };
    juce::ListBox headphoneList { "Headphones", &headphoneModel };
    std::vector<size_t> results;

    TargetModel targetModel { *this };
    juce::ListBox targetList { "Targets", &targetModel };
    std::vector<TargetRow> targetRows;

    juce::TextButton parametricTab { "Parametric" }, graphicTab { "Graphic" };

    ResponseGraph graph;
    FiltersPanel filters;

    juce::Slider correction;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> correctionAttachment;
    std::array<juce::TextButton, Theme::numAccents> swatches;

    juce::Label preampValue;
    juce::TextButton resetButton { "Reset" };
    juce::TextButton collapseButton { juce::String::fromUTF8("\xc2\xab") }, expandButton { "Show" }; // the left side
    juce::TextButton savedButton;
    juce::TextButton backgroundButton { "Background" };
    juce::TextButton apoButton; // standalone only: send the EQ to Equalizer APO
    juce::String shownApoError;
    juce::String loadedSavedName;      // the saved profile shown on the dropdown
    juce::String loadedSavedSelection; // headphone + profile it was loaded for
    std::unique_ptr<juce::AlertWindow> nameWindow;

    std::unique_ptr<juce::DocumentWindow> analyserWindow; // the pop-out spectrum
    void openAnalyserWindow();
    juce::String shownHeadphone, shownMatch;
    juce::Component::SafePointer<juce::CallOutBox> matchBox;
    int shownProfile = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BteqEditor)
};
