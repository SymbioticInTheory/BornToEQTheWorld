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
    static constexpr int numAccents = 4;

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

// The response curve. In parametric mode the numbered points can be dragged
// (frequency and gain), and scrolling over a point changes its Q.
class ResponseGraph : public juce::Component {
public:
    ResponseGraph(BteqProcessor& p, const Theme& t);
    void paint(juce::Graphics&) override;
    void resized() override;
    void setAdvanced(bool on);
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
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

// Filter cards. Each value is a label you can click and type into.
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
    std::array<std::array<juce::Label, numFields>, maxCards> values;
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
    void showScaleMenu();
    void setUiScale(float scale);
    void timerCallback() override;
    void refreshHeadphones();
    void refreshTargets();
    void showMatchChooser(juce::Rectangle<int> rowInList);
    static constexpr int kMatchRow = -2; // target list row for "sound like another headphone"
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
    juce::String loadedSavedName;      // the saved profile shown on the dropdown
    juce::String loadedSavedSelection; // headphone + profile it was loaded for
    std::unique_ptr<juce::AlertWindow> nameWindow;

    juce::String shownHeadphone, shownMatch;
    juce::Component::SafePointer<juce::CallOutBox> matchBox;
    int shownProfile = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BteqEditor)
};
