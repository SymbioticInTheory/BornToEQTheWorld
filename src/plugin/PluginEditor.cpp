#include "PluginEditor.h"

#include "ImageTheme.h"

#include <BinaryData.h>

// ===========================================================================
// Theme
// ===========================================================================
Theme::Theme() {
    using juce::Typeface;
    blackletter = Typeface::createSystemTypefaceFor(BinaryData::PirataOneRegular_ttf, BinaryData::PirataOneRegular_ttfSize);
    condSemi = Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedSemiBold_ttf,
                                                 BinaryData::BarlowCondensedSemiBold_ttfSize);
    condBold = Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
    body = Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
    bodySemi = Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
}

juce::Colour Theme::accent(int index) {
    if (index == imageAccent) return imageColour;
    static const juce::uint32 colours[numBuiltIn] = { 0xffC42B2B, 0xffE46A2E, 0xffD9D5CD, 0xff7D78D6 };
    return juce::Colour(colours[juce::jlimit(0, numBuiltIn - 1, index)]);
}

// Measured: slate, or a clearer blue / neutral grey where slate would sit too close to the accent.
juce::Colour Theme::measuredFor(int index) {
    if (index == imageAccent) return imageMeasured;
    static const juce::uint32 colours[numBuiltIn] = { 0xff6F8296, 0xff6F8296, 0xff4F86D6, 0xff9A9EA4 };
    return juce::Colour(colours[juce::jlimit(0, numBuiltIn - 1, index)]);
}

// EQ: teal against the warm and violet accents, rose against Bone.
juce::Colour Theme::eqFor(int index) {
    if (index == imageAccent) return imageEq;
    static const juce::uint32 colours[numBuiltIn] = { 0xff3CBFA4, 0xff3CBFA4, 0xffD2607E, 0xff3CBFA4 };
    return juce::Colour(colours[juce::jlimit(0, numBuiltIn - 1, index)]);
}

juce::Colour Theme::myEqFor(int index) {
    const bool lightAccent = index == 2 || (index == imageAccent && !imageVivid);
    return lightAccent ? juce::Colour(0xffB4A6EC) : text;
}

juce::String Theme::accentName(int index) {
    if (index == imageAccent) return "From your image";
    static const char* names[numBuiltIn] = { "Blood red", "Ember", "Bone", "Frost" };
    return names[juce::jlimit(0, numBuiltIn - 1, index)];
}

namespace {
// Layout of the 1080 x 680 window (matches the agreed mockup).

void drawPanel(juce::Graphics& g, juce::Rectangle<int> r) {
    g.setColour(Theme::panel);
    g.fillRoundedRectangle(r.toFloat(), 10.0f);
    g.setColour(Theme::panelBorder);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
}

juce::String formatFreq(double f) {
    return f >= 1000 ? juce::String(f / 1000.0, f >= 10000 ? 1 : 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + " kHz"
                     : juce::String((int)std::round(f)) + " Hz";
}

juce::String formatDb(double db) { return (db > 0.05 ? "+" : "") + juce::String(db, 1) + " dB"; }

juce::String formatQ(double q) { return "Q " + juce::String(q, 2).trimCharactersAtEnd("0").trimCharactersAtEnd("."); }

// Reads what someone typed: "1.2k", "1200 Hz", "+3.5 dB", "Q 0.7". Returns NaN if there is no number.
double parseTyped(const juce::String& typed, bool allowKilo) {
    const auto t = typed.trim().toLowerCase();
    const auto digits = t.retainCharacters("0123456789.-");
    if (!digits.containsAnyOf("0123456789")) return std::numeric_limits<double>::quiet_NaN();
    const double v = digits.getDoubleValue();
    return allowKilo && t.containsChar('k') ? v * 1000.0 : v;
}

double roundFreq(double f) { return f < 1000 ? std::round(f) : std::round(f / 10.0) * 10.0; }
} // namespace

// ===========================================================================
// Look and feel
// ===========================================================================
BteqLookAndFeel::BteqLookAndFeel(const Theme& t) : accentColour(Theme::accent(0)), theme(t) {
    setColour(juce::TextEditor::textColourId, Theme::text);
    setColour(juce::TextEditor::highlightColourId, Theme::selected);
    setColour(juce::CaretComponent::caretColourId, Theme::textBright);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xf2141517));
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::headerTextColourId, Theme::muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2A2C30));
    setColour(juce::PopupMenu::highlightedTextColourId, Theme::textBright);
    setColour(juce::Label::textWhenEditingColourId, Theme::textBright);
    setColour(juce::TextEditor::backgroundColourId, Theme::field);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff141517));
    setColour(juce::AlertWindow::textColourId, Theme::text);
    setColour(juce::AlertWindow::outlineColourId, juce::Colour(0x29ffffff));
}

void BteqLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) {
    const auto kind = b.getProperties()["kind"].toString();
    auto r = b.getLocalBounds().toFloat();
    if (kind == "swatch") {
        const auto c = juce::Colour((juce::uint32)(juce::int64)b.getProperties()["swatch"]);
        if (b.getToggleState()) {
            g.setColour(Theme::soft);
            g.drawEllipse(r.reduced(1.0f), 1.2f);
        } else if (over) {
            g.setColour(Theme::muted.withAlpha(0.5f));
            g.drawEllipse(r.reduced(1.0f), 1.0f);
        }
        g.setColour(c);
        g.fillEllipse(r.reduced(4.0f));
        return;
    }
    if (kind == "tab") {
        if (b.getToggleState()) {
            g.setColour(Theme::text);
            g.fillRoundedRectangle(r, 7.0f);
        } else if (over && b.isEnabled()) {
            g.setColour(Theme::selected);
            g.fillRoundedRectangle(r, 7.0f);
        }
        return;
    }
    if (kind == "pill") {
        const bool on = b.getToggleState();
        g.setColour(on ? accentColour.withAlpha(0.22f) : over ? Theme::selected : juce::Colours::transparentBlack);
        g.fillRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f);
        g.setColour(on ? accentColour : juce::Colour(0x29ffffff));
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
        return;
    }
    if (kind == "dropdown") {
        g.setColour(over ? juce::Colour(0xcc101113) : Theme::field);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(juce::Colour(0x29ffffff));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
        return;
    }
    // Bypass and anything else.
    const bool on = b.getToggleState();
    g.setColour(on ? juce::Colour(0xb3781e18) : Theme::panel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(on ? juce::Colour(0xff8A3226) : juce::Colour(0x29ffffff));
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
}

void BteqLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                       juce::Slider& s) {
    const auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4.0f);
    const float radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float angle = start + pos * (end - start);
    // Arc track and value (from 0 dB).
    const float zeroPos = (float)s.valueToProportionOfLength(0.0);
    const float zeroAngle = start + zeroPos * (end - start);
    juce::Path track, value;
    track.addCentredArc(c.x, c.y, radius, radius, 0.0f, start, end, true);
    value.addCentredArc(c.x, c.y, radius, radius, 0.0f, juce::jmin(angle, zeroAngle), juce::jmax(angle, zeroAngle), true);
    g.setColour(juce::Colour(0x29ffffff));
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accentColour);
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Body and pointer.
    const float body = radius - 6.0f;
    g.setColour(juce::Colour(0xff15171A));
    g.fillEllipse(c.x - body, c.y - body, body * 2, body * 2);
    g.setColour(juce::Colour(0x33ffffff));
    g.drawEllipse(c.x - body, c.y - body, body * 2, body * 2, 1.0f);
    const juce::Point<float> tip(c.x + std::sin(angle) * (body - 3.0f), c.y - std::cos(angle) * (body - 3.0f));
    g.setColour(Theme::textBright);
    g.drawLine(c.x + std::sin(angle) * 3.0f, c.y - std::cos(angle) * 3.0f, tip.x, tip.y, 2.0f);
}

juce::Font BteqLookAndFeel::getPopupMenuFont() { return theme.bodyFont(15.0f); }

void BteqLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const auto kind = b.getProperties()["kind"].toString();
    if (kind == "swatch") return;
    if (kind == "dropdown") {
        auto r = b.getLocalBounds().reduced(12, 0);
        const auto chev = r.removeFromRight(12).toFloat();
        juce::Path p;
        p.startNewSubPath(chev.getX(), chev.getCentreY() - 2.5f);
        p.lineTo(chev.getCentreX(), chev.getCentreY() + 2.5f);
        p.lineTo(chev.getRight(), chev.getCentreY() - 2.5f);
        g.setColour(Theme::muted);
        g.strokePath(p, juce::PathStrokeType(1.6f));
        g.setColour(b.getProperties()["placeholder"] ? Theme::muted : Theme::text);
        g.setFont(theme.bodyFont(14.0f, true));
        g.drawText(b.getButtonText(), r.withTrimmedRight(6), juce::Justification::centredLeft, true);
        return;
    }
    if (kind == "pill") {
        g.setColour(b.getToggleState() ? Theme::textBright : Theme::soft);
        g.setFont(theme.labelFont(13.0f));
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (kind == "small") {
        g.setColour(Theme::text);
        g.setFont(theme.labelFont(13.0f));
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
        return;
    }
    juce::Colour c = Theme::text;
    if (kind == "tab") c = b.getToggleState() ? juce::Colour(0xff0B0C0D) : Theme::soft;
    if (kind == "bypass" && b.getToggleState()) c = juce::Colour(0xffFFB5A6);
    if (!b.isEnabled()) c = c.withAlpha(0.35f);
    g.setColour(c);
    g.setFont(theme.labelFont(17.0f));
    g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
}

void BteqLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                       juce::Slider::SliderStyle, juce::Slider&) {
    const float cy = (float)y + (float)h * 0.5f;
    const juce::Rectangle<float> track((float)x, cy - 3.0f, (float)w, 6.0f);
    g.setColour(juce::Colour(0x29ffffff));
    g.fillRoundedRectangle(track, 3.0f);
    g.setColour(accentColour);
    g.fillRoundedRectangle(track.withRight(pos), 3.0f);
    const juce::Rectangle<float> thumb(pos - 11.0f, cy - 11.0f, 22.0f, 22.0f);
    g.setColour(juce::Colour(0xffD9D5CD));
    g.fillEllipse(thumb);
    g.setColour(accentColour);
    g.drawEllipse(thumb.reduced(1.5f), 3.0f);
}

void BteqLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(Theme::field);
    g.fillRoundedRectangle(0.0f, 0.0f, (float)w, (float)h, 8.0f);
}

void BteqLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    g.setColour(e.hasKeyboardFocus(true) ? Theme::text : juce::Colour(0x29ffffff));
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, 8.0f, 1.0f);
}

void BteqLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                    int thumbStart, int thumbSize, bool over, bool) {
    if (thumbSize <= 0) return;
    juce::Rectangle<float> r = vertical ? juce::Rectangle<float>((float)x + (float)w - 5.0f, (float)thumbStart, 4.0f, (float)thumbSize)
                                        : juce::Rectangle<float>((float)thumbStart, (float)y + (float)h - 5.0f, (float)thumbSize, 4.0f);
    g.setColour(juce::Colour(over ? 0x55ffffff : 0x33ffffff));
    g.fillRoundedRectangle(r, 2.0f);
}

// ===========================================================================
// Response graph
// ===========================================================================
TitleBarLookAndFeel::TitleBarLookAndFeel(const Theme& t) : BteqLookAndFeel(t), font(t) {
    formula = juce::ImageCache::getFromMemory(BinaryData::lagrangian_png, BinaryData::lagrangian_pngSize);
    // The minimise and close buttons fill with this colour.
    auto scheme = getCurrentColourScheme();
    scheme.setUIColour(juce::LookAndFeel_V4::ColourScheme::widgetBackground, juce::Colours::black);
    setColourScheme(scheme);
}

void TitleBarLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) {
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(over ? juce::Colour(0x1fffffff) : juce::Colours::black);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(juce::Colour(0x33ffffff));
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
}

void TitleBarLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(Theme::soft);
    g.setFont(font.labelFont(11.0f));
    g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
}

void TitleBarLookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics& g, int w, int h, int titleSpaceX,
                                                     int titleSpaceW, const juce::Image*, bool) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colour(0x1fffffff));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
    if (!formula.isValid()) return;
    // Centred on the window, kept clear of the Options button on the left and
    // the window buttons on the right.
    const int left = juce::jmax(titleSpaceX, 76), right = juce::jmin(titleSpaceX + titleSpaceW, w - 8);
    const int half = juce::jmax(0, juce::jmin(w / 2 - left, right - w / 2));
    const auto area = juce::Rectangle<int>(w / 2 - half, 0, half * 2, h).toFloat().reduced(4.0f, h * 0.12f);
    // Scaled once per size from the large render, so the thin strokes stay smooth.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float fit = juce::jmin(area.getWidth() / formula.getWidth(), area.getHeight() / formula.getHeight(), 1.0f);
    const int pw = juce::jmax(1, juce::roundToInt(formula.getWidth() * fit * scale)),
              ph = juce::jmax(1, juce::roundToInt(formula.getHeight() * fit * scale));
    if (scaled.getWidth() != pw || scaled.getHeight() != ph)
        scaled = formula.rescaled(pw, ph, juce::Graphics::highResamplingQuality);
    const auto dest = juce::Rectangle<float>(pw / scale, ph / scale).withCentre(area.getCentre());
    g.setColour(accentColour.brighter(0.25f)); // a touch brighter: thin strokes on black
    g.drawImage(scaled, dest, juce::RectanglePlacement::stretchToFit, true);
}

ResponseGraph::ResponseGraph(BteqProcessor& p, const Theme& t) : proc(p), theme(t) {
    advancedButton.getProperties().set("kind", "pill");
    advancedButton.setClickingTogglesState(true);
    advancedButton.setTooltip("Show the target, your headphone's measurement and the corrected result");
    advancedButton.onClick = [this] { setAdvanced(advancedButton.getToggleState()); };
    addAndMakeVisible(advancedButton);
    myEqButton.getProperties().set("kind", "pill");
    myEqButton.setClickingTogglesState(true);
    myEqButton.setTooltip("Your own EQ on top of the correction. Turn on to edit it here and in the cards below.");
    myEqButton.onClick = [this] {
        proc.editingMyEq = myEqButton.getToggleState();
        refreshMyEqButton();
        dragIndex = -1;
        repaint();
        if (onMyEqToggled) onMyEqToggled();
    };
    addAndMakeVisible(myEqButton);
    scopeButton.getProperties().set("kind", "pill");
    scopeButton.setTooltip("Every target: one My EQ that stays as you change target or load a saved profile.\n"
                           "This target only: each target keeps its own My EQ, and saved profiles keep theirs.");
    scopeButton.onClick = [this] {
        proc.setMyEqPerTarget(!proc.myEqPerTarget());
        refreshMyEqButton();
        repaint();
        if (onMyEqToggled) onMyEqToggled();
    };
    addChildComponent(scopeButton);
    refreshMyEqButton();
    setAdvanced(proc.advancedView);
}

void ResponseGraph::refreshMyEqButton() {
    myEqButton.setToggleState(proc.editingMyEq, juce::dontSendNotification);
    scopeButton.setVisible(proc.editingMyEq);
    scopeButton.setButtonText(proc.myEqPerTarget() ? "This target only" : "Every target");
    scopeButton.setToggleState(proc.myEqPerTarget(), juce::dontSendNotification);
}

void ResponseGraph::setAdvanced(bool on) {
    proc.advancedView = on;
    advancedButton.setToggleState(on, juce::dontSendNotification);
    if (on && curves == nullptr) curves = std::make_unique<juce::SharedResourcePointer<CurvesHolder>>();
    repaint();
}

void ResponseGraph::resized() {
    advancedButton.setBounds(getWidth() - 96, 2, 96, 24);
    myEqButton.setBounds(getWidth() - 96 - 8 - 76, 2, 76, 24);
    scopeButton.setBounds(myEqButton.getX() - 8 - 140, 2, 140, 24);
}

juce::Rectangle<float> ResponseGraph::plotArea() const {
    return getLocalBounds().toFloat().withTrimmedLeft(34).withTrimmedBottom(22).withTrimmedTop(38).withTrimmedRight(6);
}

float ResponseGraph::xOf(double f) const {
    const auto a = plotArea();
    return a.getX() + (float)(std::log10(f / 20.0) / 3.0) * a.getWidth();
}

float ResponseGraph::yOf(double db) const {
    const auto a = plotArea();
    const double r = rangeDb();
    const double clamped = juce::jlimit(-r - 1.0, r + 1.0, db);
    return a.getCentreY() - (float)(clamped / r) * a.getHeight() * 0.5f;
}

void ResponseGraph::paint(juce::Graphics& g) {
    const auto a = plotArea();
    const double r = rangeDb();
    std::vector<double> lines;
    for (double db = -r; db <= r + 0.1; db += 6.0) lines.push_back(db);
    // Grid.
    g.setColour(Theme::grid);
    for (double db : lines)
        if (std::abs(db) > 0.1) g.drawHorizontalLine((int)yOf(db), a.getX(), a.getRight());
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine((int)xOf(f), a.getY(), a.getBottom());
    g.setColour(juce::Colour(0x33ffffff));
    g.drawHorizontalLine((int)yOf(0), a.getX(), a.getRight());

    g.setFont(theme.condFont(13.0f));
    g.setColour(Theme::muted);
    for (double db : lines)
        g.drawText((db > 0 ? "+" : "") + juce::String((int)db), juce::Rectangle<float>(0, yOf(db) - 8, a.getX() - 8, 16),
                   juce::Justification::centredRight);
    const std::pair<double, const char*> ticks[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                     { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    for (const auto& t : ticks)
        g.drawText(t.second, juce::Rectangle<float>(xOf(t.first) - 20, a.getBottom() + 4, 40, 16), juce::Justification::centred);

    for (auto& h : legendHit) h = {};
    const auto* p = proc.selectedProfile();
    const double k = proc.amount();
    const juce::Colour curveColour = proc.params.getRawParameterValue("bypass")->load() > 0.5f ? juce::Colour(0xff6E7178) : accent;
    // My EQ is off-white, except against Bone where that would match the main curve.
    const juce::Colour myEqColour = Theme::myEqFor(proc.accentIndex);
    if (proc.editingMyEq && !proc.advancedView) {
        // Editing My EQ: the correction stays in the background.
        if (p != nullptr) paintEq(g, k, juce::Colour(0xff6E7178).withAlpha(0.7f), 2.0f, false);
        paintMyEq(g, curveColour, 3.0f, true, false);
        return;
    }
    if (p == nullptr) {
        g.setColour(curveColour);
        g.drawLine(a.getX(), yOf(0), a.getRight(), yOf(0), 3.0f);
        if (proc.myEqActive()) paintMyEq(g, myEqColour.withAlpha(0.6f), 1.8f, false, true);
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(15.0f));
        g.drawText("Search for your headphones to start", a.withTrimmedTop(a.getHeight() * 0.25f).withHeight(24),
                   juce::Justification::centred);
        return;
    }
    if (proc.advancedView) {
        paintAdvanced(g, *p, k, curveColour);
        if (proc.editingMyEq) paintMyEq(g, myEqColour, 2.4f, true, false);
        return;
    }
    paintEq(g, k, curveColour, 3.0f, true);
    if (proc.myEqActive()) paintMyEq(g, myEqColour.withAlpha(0.6f), 1.8f, false, true);
}

// My EQ's curve (without its headroom), dashed when it is only shown for reference.
void ResponseGraph::paintMyEq(juce::Graphics& g, juce::Colour colour, float width, bool nodes, bool dashed) {
    const auto mine = proc.myEq();
    std::vector<bteq::BiquadCoeffs> coeffs;
    if (mine.on)
        for (const auto& f : mine.bands)
            if (f.enabled && std::abs(f.gainDb) > 0.01) coeffs.push_back(bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb, f.q, 48000.0));
    juce::Path curve;
    const int points = 300;
    for (int i = 0; i <= points; ++i) {
        const double f = 20.0 * std::pow(1000.0, i / (double)points);
        double db = 0;
        for (const auto& c : coeffs) db += c.magnitudeDb(f, 48000.0);
        const float x = xOf(f), y = yOf(db);
        i == 0 ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
    }
    const juce::PathStrokeType stroke(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(mine.on ? colour : colour.withAlpha(0.35f));
    if (dashed) {
        juce::Path d;
        const float dashes[] = { 6.0f, 5.0f };
        stroke.createDashedStroke(d, curve, dashes, 2);
        g.fillPath(d);
    } else {
        g.strokePath(curve, stroke);
    }
    if (nodes) paintNodes(g, mine.bands, 1.0, mine.on ? colour : colour.withAlpha(0.5f));
}

void ResponseGraph::paintNodes(juce::Graphics& g, const std::vector<bteq::FilterSpec>& filters, double k, juce::Colour colour) {
    int num = 1;
    g.setFont(theme.labelFont(12.0f));
    for (const auto& f : filters) {
        if (f.type == bteq::FilterType::LowPass) { ++num; continue; }
        const auto pos = nodePosition(f, k);
        const float x = pos.x, y = pos.y;
        const auto c = f.enabled ? colour : colour.withMultipliedAlpha(0.35f); // switched off: faded
        g.setColour(num - 1 == dragIndex ? c.withAlpha(0.45f) : juce::Colour(0xff0B0C0D));
        g.fillEllipse(x - 9, y - 9, 18, 18);
        g.setColour(c);
        g.drawEllipse(x - 9, y - 9, 18, 18, 2.0f);
        g.setColour(f.enabled ? Theme::textBright : Theme::muted);
        g.drawText(juce::String(num++), juce::Rectangle<float>(x - 9, y - 9, 18, 18), juce::Justification::centred);
    }
}

// The EQ curve (without preamp), and in parametric mode its numbered points.
void ResponseGraph::paintEq(juce::Graphics& g, double k, juce::Colour colour, float width, bool nodes) {
    const auto a = plotArea();
    juce::Path curve;
    const int points = 300;
    if (proc.graphicModeActive()) {
        // Shown without the preamp, the same way as the parametric curve.
        const auto& freqs = proc.library().graphicFrequencies();
        auto gains = proc.graphicShapeDb();
        for (auto& gn : gains) gn *= k;
        if (nodes)
            for (size_t i = 0; i < freqs.size(); i += 4) {
                g.setColour(colour.withAlpha(0.35f));
                g.drawLine(xOf(freqs[i]), yOf(0), xOf(freqs[i]), yOf(gains[i]), 5.0f);
            }
        for (int i = 0; i <= points; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / (double)points);
            const float x = xOf(f), y = yOf(bteq::graphicGainAt(freqs, gains, f));
            i == 0 ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
        }
        g.setColour(colour);
        g.strokePath(curve, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    const auto preset = proc.effectiveParametric();
    std::vector<bteq::BiquadCoeffs> coeffs;
    for (const auto& f : preset.filters)
        if (f.enabled) coeffs.push_back(bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb * k, f.q, 48000.0));
    for (int i = 0; i <= points; ++i) {
        const double f = 20.0 * std::pow(1000.0, i / (double)points);
        double db = 0;
        for (const auto& c : coeffs) db += c.magnitudeDb(f, 48000.0);
        const float x = xOf(f), y = yOf(db);
        i == 0 ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
    }
    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (nodes && !proc.editingMyEq) paintNodes(g, preset.filters, k, colour);
}

// Target, measured and corrected curves like autoeq.app. The corrected curve
// is the measurement plus the EQ as it is set now (edits and correction amount
// included); both are shifted to sit on the target between 300 Hz and 3 kHz.
void ResponseGraph::paintAdvanced(juce::Graphics& g, const bteq::HeadphoneProfile& p, double k, juce::Colour curveColour) {
    const auto& freqs = proc.library().graphicFrequencies();
    bteq::CurveLibrary::Curves c;
    std::vector<float> matchOwn, matchOther;
    if (const auto& m = proc.currentMatch(); m.active) {
        // Sounding like another headphone: its measurement is the target.
        matchOwn.assign(m.ownMeasured.begin(), m.ownMeasured.end());
        matchOther.assign(m.otherMeasured.begin(), m.otherMeasured.end());
        c.measured = &matchOwn;
        c.target = &matchOther;
    } else if (curves != nullptr && (*curves)->loaded) {
        c = (*curves)->curves.find(p.name, p.source, p.rig, p.originalPreset, p.target);
    }

    // EQ response on the curve frequencies.
    std::vector<double> eq(freqs.size(), 0.0);
    if (proc.graphicModeActive()) {
        const auto shape = proc.graphicShapeDb();
        for (size_t i = 0; i < eq.size() && i < shape.size(); ++i) eq[i] = shape[i] * k;
    } else {
        for (const auto& f : proc.effectiveParametric().filters) {
            if (!f.enabled) continue;
            const auto co = bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb * k, f.q, 48000.0);
            for (size_t i = 0; i < eq.size(); ++i) eq[i] += co.magnitudeDb(freqs[i], 48000.0);
        }
    }

    // What you hear also includes My EQ.
    std::vector<double> mine(freqs.size(), 0.0);
    if (const auto my = proc.myEq(); my.on)
        for (const auto& f : my.bands) {
            if (!f.enabled || std::abs(f.gainDb) <= 0.01) continue;
            const auto co = bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb, f.q, 48000.0);
            for (size_t i = 0; i < mine.size(); ++i) mine[i] += co.magnitudeDb(freqs[i], 48000.0);
        }

    std::vector<double> measured, corrected;
    double offset = 0;
    if (c.measured != nullptr && c.measured->size() == freqs.size()) {
        for (size_t i = 0; i < freqs.size(); ++i) {
            measured.push_back((*c.measured)[i]);
            corrected.push_back((*c.measured)[i] + eq[i] + mine[i]);
        }
        if (c.target != nullptr) {
            double sum = 0;
            int n = 0;
            for (size_t i = 0; i < freqs.size(); ++i)
                if (freqs[i] >= 300 && freqs[i] <= 3000) { sum += (*c.target)[i] - corrected[i]; ++n; }
            offset = n > 0 ? sum / n : 0.0;
        }
    }

    auto drawCurve = [&](const std::vector<double>& v, double shift, juce::Colour col, float width, bool dashed) {
        juce::Path path;
        for (size_t i = 0; i < v.size(); ++i) {
            const float x = xOf(freqs[i]), y = yOf(v[i] + shift);
            i == 0 ? path.startNewSubPath(x, y) : path.lineTo(x, y);
        }
        const juce::PathStrokeType stroke(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(col);
        if (dashed) {
            juce::Path d;
            const float dashes[] = { 7.0f, 5.0f };
            stroke.createDashedStroke(d, path, dashes, 2);
            g.fillPath(d);
        } else {
            g.strokePath(path, stroke);
        }
    };

    const bool bypassed = proc.params.getRawParameterValue("bypass")->load() > 0.5f;
    const juce::Colour targetColour(0xffE3B23C), measuredColour(Theme::measuredFor(proc.accentIndex));
    const juce::Colour eqColour = bypassed ? curveColour.withAlpha(0.55f) : Theme::eqFor(proc.accentIndex).withAlpha(0.85f);
    const bool hasMeasured = !measured.empty(), hasTarget = c.target != nullptr;
    auto visible = [this](Curve cv) { return (proc.hiddenCurves & (1 << cv)) == 0; };

    if (hasTarget && visible(Target)) {
        const std::vector<double> t(c.target->begin(), c.target->end());
        drawCurve(t, 0.0, targetColour.withAlpha(0.8f), 2.0f, true);
    }
    if (hasMeasured && visible(Measured)) drawCurve(measured, offset, measuredColour, 2.0f, false);
    if (visible(Eq)) paintEq(g, k, eqColour, 1.8f, !proc.graphicModeActive());
    if (hasMeasured && visible(Corrected)) drawCurve(corrected, offset, curveColour, 3.0f, false);

    // Legend: click a name to show or hide its curve.
    struct Item { Curve id; const char* name; juce::Colour colour; bool available; };
    const Item items[] = { { Target, "Target", targetColour, hasTarget },
                           { Measured, "Measured", measuredColour, hasMeasured },
                           { Corrected, "Corrected", curveColour, hasMeasured },
                           { Eq, "EQ", eqColour, true } };
    float x = plotArea().getX();
    g.setFont(theme.bodyFont(14.0f, true));
    for (const auto& it : items) {
        const float w = 30.0f + juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), it.name);
        const juce::Rectangle<float> box(x, 2.0f, w, 24.0f);
        const bool on = it.available && visible(it.id);
        if (it.available) legendHit[(size_t)it.id] = box;
        const auto sw = juce::Rectangle<float>(box.getX() + 4, box.getCentreY() - 1.5f, 16, 3);
        g.setColour(on ? it.colour : it.colour.withAlpha(0.25f));
        g.fillRoundedRectangle(sw, 1.5f);
        g.setColour(!it.available ? Theme::muted.withAlpha(0.4f) : on ? Theme::text : Theme::muted.withAlpha(0.6f));
        g.drawText(it.name, box.withTrimmedLeft(26), juce::Justification::centredLeft);
        if (it.available && !on) g.drawHorizontalLine((int)box.getCentreY(), box.getX() + 26, box.getRight() - 4);
        x += w + 12;
    }
    if (curves != nullptr && !(*curves)->loaded) {
        g.setColour(juce::Colour(0xffFFB5A6));
        g.drawText("Curves could not be loaded", juce::Rectangle<float>(x, 2, 240, 24), juce::Justification::centredLeft);
    } else if (!hasMeasured) {
        g.setColour(Theme::muted);
        g.setFont(theme.bodyFont(13.0f));
        g.drawText("No measurement from this source", juce::Rectangle<float>(x, 2, getWidth() - x - 104, 24),
                   juce::Justification::centredLeft, true);
    }
}

juce::Point<float> ResponseGraph::nodePosition(const bteq::FilterSpec& f, double k) const {
    return { juce::jmax(plotArea().getX() + 9.0f, xOf(f.fc)), yOf(f.gainDb * k) };
}

std::vector<bteq::FilterSpec> ResponseGraph::nodeFilters(double& k) const {
    if (proc.editingMyEq) {
        k = 1.0;
        return proc.myEq().bands;
    }
    k = proc.amount();
    if (proc.selectedProfile() == nullptr || proc.graphicModeActive()) return {};
    if (proc.advancedView && (proc.hiddenCurves & (1 << Eq)) != 0) return {};
    return proc.effectiveParametric().filters;
}

void ResponseGraph::setNodeFilter(int index, const bteq::FilterSpec& f) {
    if (proc.editingMyEq) proc.setMyEqBand(index, f);
    else proc.setFilter(index, f);
}

int ResponseGraph::nodeAt(juce::Point<float> pos) const {
    double k = 1.0;
    const auto filters = nodeFilters(k);
    int best = -1;
    float bestDist = 12.0f;
    for (int i = 0; i < (int)filters.size(); ++i) {
        const auto& f = filters[(size_t)i];
        if (f.type == bteq::FilterType::LowPass) continue;
        const float d = nodePosition(f, k).getDistanceFrom(pos);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void ResponseGraph::mouseMove(const juce::MouseEvent& e) {
    bool overLegend = false;
    if (proc.advancedView)
        for (const auto& h : legendHit) overLegend = overLegend || h.contains(e.position);
    setMouseCursor(nodeAt(e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor
                   : overLegend            ? juce::MouseCursor::PointingHandCursor
                                           : juce::MouseCursor::NormalCursor);
}

void ResponseGraph::mouseDown(const juce::MouseEvent& e) {
    if (proc.advancedView)
        for (int i = 0; i < numCurves; ++i)
            if (legendHit[(size_t)i].contains(e.position)) {
                proc.hiddenCurves ^= 1 << i;
                repaint();
                return;
            }
    dragIndex = nodeAt(e.position);
    repaint();
}

void ResponseGraph::mouseDoubleClick(const juce::MouseEvent& e) {
    const int i = nodeAt(e.position);
    if (i < 0) return;
    double k = 1.0;
    auto filters = nodeFilters(k);
    auto f = filters[(size_t)i];
    f.enabled = !f.enabled;
    setNodeFilter(i, f);
    dragIndex = -1;
    repaint();
    if (auto* parent = getParentComponent()) parent->repaint();
}

void ResponseGraph::mouseDrag(const juce::MouseEvent& e) {
    if (dragIndex < 0) return;
    double k = 1.0;
    const auto filters = nodeFilters(k);
    if (dragIndex >= (int)filters.size()) return;
    auto f = filters[(size_t)dragIndex];
    const auto a = plotArea();
    const double x = juce::jlimit(0.0, 1.0, (double)((e.position.x - a.getX()) / a.getWidth()));
    f.fc = roundFreq(20.0 * std::pow(1000.0, x));
    if (k > 0.01) {
        const double db = (a.getCentreY() - e.position.y) / (a.getHeight() * 0.5) * rangeDb();
        f.gainDb = juce::jlimit(-20.0, 20.0, std::round(db / k * 10.0) / 10.0);
    }
    setNodeFilter(dragIndex, f);
    repaint();
}

void ResponseGraph::mouseUp(const juce::MouseEvent&) { dragIndex = -1; repaint(); }

void ResponseGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const int i = nodeAt(e.position);
    if (i < 0 || w.deltaY == 0.0f) return;
    double k = 1.0;
    auto f = nodeFilters(k)[(size_t)i];
    f.q = juce::jlimit(0.1, 20.0, std::round(f.q * (w.deltaY > 0 ? 1.1 : 1.0 / 1.1) * 100.0) / 100.0);
    setNodeFilter(i, f);
    repaint();
}

// ===========================================================================
// Filters panel
// ===========================================================================
FiltersPanel::FiltersPanel(BteqProcessor& p, const Theme& t) : proc(p), theme(t) {
    for (int i = 0; i < maxCards; ++i)
        for (int fi = 0; fi < numFields; ++fi) {
            auto& l = values[(size_t)i][(size_t)fi];
            l.setFont(fi == Freq ? theme.bodyFont(14.0f, true) : theme.bodyFont(13.0f));
            l.setColour(juce::Label::textColourId, fi == Freq ? Theme::text : juce::Colour(0xffB2AFA9));
            l.setBorderSize({ 0, 0, 0, 0 });
            l.setMinimumHorizontalScale(1.0f);
            l.setEditable(true, false, false);
            l.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            l.setTitle("Filter " + juce::String(i + 1) + (fi == Freq ? " frequency" : fi == Gain ? " gain" : " Q"));
            l.setTooltip("Drag up or down to change it (hold Shift for fine steps), or click to type a value");
            l.onTextChange = [this, i, fi, &l] { commit(i, (Field)fi, l.getText()); };
            l.onDragStart = [this, i, fi] {
                const auto shown = shownFilters();
                if (i >= (int)shown.size()) return;
                const auto& f = shown[(size_t)i];
                dragPosition = fi == Freq ? std::log2(f.fc) : fi == Gain ? f.gainDb : std::log2(f.q);
            };
            l.onDrag = [this, i, fi](int up, bool fine) { dragValue(i, (Field)fi, up, fine); };
            addChildComponent(l);
        }
    myEqOnButton.getProperties().set("kind", "pill");
    myEqOnButton.setClickingTogglesState(true);
    myEqOnButton.setTooltip("Switch My EQ on or off");
    myEqOnButton.onClick = [this] {
        proc.setMyEqOn(myEqOnButton.getToggleState());
        refresh();
        getParentComponent()->repaint();
    };
    addChildComponent(myEqOnButton);

    // My EQ's gain: a knob, or automatic (down by its highest boost).
    autoGainButton.getProperties().set("kind", "pill");
    autoGainButton.setClickingTogglesState(true);
    autoGainButton.setTooltip("Automatic gain: follows the recommended level, down by My EQ's highest boost so it "
                              "can't clip. Off: the knob sets it.");
    autoGainButton.onClick = [this] {
        proc.setMyEqAutoGain(autoGainButton.getToggleState());
        refresh();
        getParentComponent()->repaint();
    };
    addChildComponent(autoGainButton);
    gainKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainKnob.setRange(-24.0, 12.0, 0.1);
    gainKnob.setMouseDragSensitivity(360);
    gainKnob.setTitle("My EQ gain");
    gainKnob.setTooltip("My EQ's gain. Drag to set it, right-click to type, double-click for 0 dB.");
    gainKnob.onValueChange = [this] {
        if (std::abs(gainKnob.getValue() - proc.myEqGainNowDb()) > 0.04) proc.setMyEqGain(gainKnob.getValue());
        refresh();
    };
    gainKnob.onRightClick = [this] { gainValue.showEditor(); };
    gainKnob.onDoubleClick = [this] { proc.setMyEqGain(0.0); refresh(); };
    addChildComponent(gainKnob);
    gainValue.setFont(theme.labelFont(14.0f));
    gainValue.setColour(juce::Label::textColourId, Theme::text);
    gainValue.setBorderSize({ 0, 0, 0, 0 });
    gainValue.setEditable(false, true, false);
    gainValue.setTooltip("Double-click to type My EQ's gain");
    gainValue.onTextChange = [this] {
        const double v = parseTyped(gainValue.getText(), false);
        if (!std::isnan(v)) proc.setMyEqGain(v);
        refresh();
    };
    addChildComponent(gainValue);
}

std::vector<bteq::FilterSpec> FiltersPanel::shownFilters() const {
    if (proc.editingMyEq) return proc.myEq().bands;
    if (proc.selectedProfile() == nullptr || proc.graphicModeActive()) return {};
    return proc.effectiveParametric().filters;
}

void FiltersPanel::mouseDown(const juce::MouseEvent& e) {
    const auto shown = shownFilters();
    for (int i = 0; i < (int)shown.size() && i < maxCards; ++i) {
        const auto top = cardBounds(i).withHeight(24);
        if (!top.contains(e.getPosition())) continue;
        auto f = shown[(size_t)i];
        if (e.x < top.getX() + 30) {
            // The number is the filter's on/off switch.
            f.enabled = !f.enabled;
            if (proc.editingMyEq) proc.setMyEqBand(i, f);
            else proc.setFilter(i, f);
            refresh();
            getParentComponent()->repaint();
            return;
        }
        if (!proc.editingMyEq && !proc.selectedIsCustom()) return; // the type can change in My EQ and your own presets
        using T = bteq::FilterType;
        f.type = f.type == T::Peaking ? T::LowShelf : f.type == T::LowShelf ? T::HighShelf : T::Peaking;
        if (f.type != T::Peaking && f.q > 2.0) f.q = 0.7;
        if (proc.editingMyEq) proc.setMyEqBand(i, f);
        else proc.setFilter(i, f);
        refresh();
        getParentComponent()->repaint();
        return;
    }
}

juce::Rectangle<int> FiltersPanel::cardBounds(int i) const {
    const int cols = 5, gap = 8, ch = 76;
    const int cw = (getWidth() - gap * (cols - 1)) / cols;
    return { (i % cols) * (cw + gap), (i / cols) * (ch + gap), cw, ch };
}

void FiltersPanel::resized() {
    myEqOnButton.setBounds(getWidth() - 64 - 8 - 56, getHeight() - 22, 56, 22);
    gainKnob.setBounds(40, getHeight() - 26, 26, 26);
    gainValue.setBounds(70, getHeight() - 22, 64, 22);
    autoGainButton.setBounds(136, getHeight() - 22, 56, 22);
    for (int i = 0; i < maxCards; ++i) {
        auto r = cardBounds(i).reduced(9, 6);
        r.removeFromTop(16);
        values[(size_t)i][Freq].setBounds(r.removeFromTop(19));
        values[(size_t)i][Gain].setBounds(r.removeFromTop(16));
        values[(size_t)i][Q].setBounds(r.removeFromTop(16));
    }
}

void FiltersPanel::refresh() {
    const auto filters = shownFilters();
    myEqOnButton.setVisible(proc.editingMyEq);
    for (juce::Component* c : { (juce::Component*)&gainKnob, (juce::Component*)&gainValue, (juce::Component*)&autoGainButton })
        c->setVisible(proc.editingMyEq);
    const auto mine = proc.myEq();
    autoGainButton.setToggleState(mine.autoGain, juce::dontSendNotification);
    const double gainNow = proc.myEqGainNowDb();
    if (!gainKnob.isMouseButtonDown() && std::abs(gainKnob.getValue() - gainNow) > 0.04)
        gainKnob.setValue(gainNow, juce::dontSendNotification);
    if (!gainValue.isBeingEdited() && gainValue.getText() != formatDb(gainNow))
        gainValue.setText(formatDb(gainNow), juce::dontSendNotification);
    myEqOnButton.setToggleState(proc.myEq().on, juce::dontSendNotification);
    myEqOnButton.setButtonText(proc.myEq().on ? "On" : "Off");
    for (int i = 0; i < maxCards; ++i) {
        const bool has = i < (int)filters.size();
        for (int fi = 0; fi < numFields; ++fi) {
            auto& l = values[(size_t)i][(size_t)fi];
            l.setVisible(has);
            if (!has || l.isBeingEdited()) continue;
            const auto& f = filters[(size_t)i];
            const auto base = fi == Freq ? Theme::text : juce::Colour(0xffB2AFA9);
            l.setColour(juce::Label::textColourId, f.enabled ? base : base.withAlpha(0.35f));
            const bool lowPass = f.type == bteq::FilterType::LowPass;
            const juce::String text = fi == Freq ? formatFreq(f.fc)
                                    : fi == Gain ? (lowPass ? juce::String("12 dB/oct") : formatDb(f.gainDb))
                                                 : formatQ(f.q);
            if (l.getText() != text) l.setText(text, juce::dontSendNotification);
            l.setEditable(!(lowPass && fi == Gain), false, false);
        }
    }
}

// Frequency: 100 pixels per octave. Gain: 20 pixels per dB. Q: 150 pixels per
// doubling. Shift makes each step five times finer.
void FiltersPanel::dragValue(int card, Field field, int pixelsUp, bool fine) {
    auto filters = shownFilters();
    if (card >= (int)filters.size()) return;
    auto f = filters[(size_t)card];
    const double step = (fine ? 0.2 : 1.0) * pixelsUp;
    if (field == Freq) {
        dragPosition = juce::jlimit(std::log2(10.0), std::log2(22000.0), dragPosition + step / 100.0);
        const double fc = std::pow(2.0, dragPosition);
        f.fc = fc < 100 ? std::round(fc * 10.0) / 10.0 : std::round(fc);
    } else if (field == Gain) {
        if (f.type == bteq::FilterType::LowPass) return;
        dragPosition = juce::jlimit(-30.0, 30.0, dragPosition + step * 0.05);
        f.gainDb = std::round(dragPosition * 10.0) / 10.0;
    } else {
        dragPosition = juce::jlimit(std::log2(0.05), std::log2(30.0), dragPosition + step / 150.0);
        f.q = std::round(std::pow(2.0, dragPosition) * 100.0) / 100.0;
    }
    if (proc.editingMyEq) proc.setMyEqBand(card, f);
    else proc.setFilter(card, f);
    refresh();
    getParentComponent()->repaint();
}

void FiltersPanel::commit(int card, Field field, const juce::String& text) {
    auto filters = shownFilters();
    if (card >= (int)filters.size()) return;
    auto f = filters[(size_t)card];
    const double v = parseTyped(text, field == Freq);
    if (!std::isnan(v)) {
        if (field == Freq) f.fc = juce::jlimit(10.0, 22000.0, v);
        if (field == Gain) f.gainDb = juce::jlimit(-30.0, 30.0, v);
        if (field == Q) f.q = juce::jlimit(0.05, 30.0, v);
        if (proc.editingMyEq) proc.setMyEqBand(card, f);
        else proc.setFilter(card, f);
    }
    refresh();
    getParentComponent()->repaint();
}

void FiltersPanel::paint(juce::Graphics& g) {
    const auto* p = proc.selectedProfile();
    if (p == nullptr && !proc.editingMyEq) {
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(14.0f));
        g.drawFittedText("Pick a headphone and a target. The filters will show here.", getLocalBounds().reduced(8),
                         juce::Justification::topLeft, 3);
        return;
    }
    if (const auto& m = proc.currentMatch(); m.active && !proc.editingMyEq) {
        auto r = getLocalBounds().reduced(6, 4);
        g.setColour(Theme::text);
        g.setFont(theme.labelFont(19.0f));
        g.drawText("Sounding like " + proc.matchedHeadphone(), r.removeFromTop(26), juce::Justification::centredLeft, true);
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(14.0f));
        g.drawFittedText("The difference between the two headphones' measurements, smoothed in the treble and faded "
                         "out above 12 kHz, applied as a 127-point graphic EQ. Its measurement comes from " + m.source + ".",
                         r.removeFromTop(60), juce::Justification::topLeft, 3);
        g.setColour(m.sameRig ? Theme::text : juce::Colour(0xffFFB5A6));
        g.drawFittedText(m.sameRig ? juce::String("Both measured on the same rig, so the match is as close as it gets. "
                                                  "Turn on Advanced to see both curves.")
                                   : juce::String("Measured on different rigs or by different people, so treat the "
                                                  "treble as approximate."),
                         r.removeFromTop(44), juce::Justification::topLeft, 2);
        return;
    }
    if (proc.graphicModeActive() && !proc.editingMyEq) {
        auto r = getLocalBounds().reduced(6, 4);
        g.setColour(Theme::text);
        g.setFont(theme.labelFont(19.0f));
        g.drawText("127-point GraphicEQ", r.removeFromTop(26), juce::Justification::centredLeft);
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(14.0f));
        g.drawFittedText("The same curve Wavelet and Equalizer APO's GraphicEQ use, applied as a minimum-phase "
                         "filter exactly like Equalizer APO. Bars show every fourth point.",
                         r.removeFromTop(60), juce::Justification::topLeft, 3);
        g.setColour(Theme::text);
        g.drawFittedText("The graph shows the curve without the preamp, like Parametric. "
                         "Change the preamp on the right; it is kept separately for each target.",
                         r.removeFromTop(44), juce::Justification::topLeft, 2);
        return;
    }

    const auto fs = shownFilters();
    for (int i = 0; i < (int)fs.size() && i < maxCards; ++i) {
        const auto& f = fs[(size_t)i];
        const auto card = cardBounds(i).toFloat();
        g.setColour(juce::Colour(0x99060708));
        g.fillRoundedRectangle(card, 8.0f);
        g.setColour(juce::Colour(0x14ffffff));
        g.drawRoundedRectangle(card.reduced(0.5f), 8.0f, 1.0f);
        auto r = card.reduced(9, 6);
        const char* type = f.type == bteq::FilterType::Peaking ? "PEAK"
                         : f.type == bteq::FilterType::LowShelf ? "LOW SHELF"
                         : f.type == bteq::FilterType::HighShelf ? "HIGH SHELF" : "LOW PASS";
        auto top = r.removeFromTop(16);
        // The number doubles as the on/off switch: filled when on, hollow when off.
        const auto badge = top.removeFromLeft(18).withSizeKeepingCentre(18, 16);
        if (f.enabled) {
            g.setColour(accent);
            g.fillRoundedRectangle(badge, 8.0f);
            g.setColour(juce::Colour(0xff0B0C0D));
        } else {
            g.setColour(Theme::muted.withAlpha(0.6f));
            g.drawRoundedRectangle(badge.reduced(0.5f), 8.0f, 1.0f);
        }
        g.setFont(theme.labelFont(12.0f));
        g.drawText(juce::String(i + 1), badge, juce::Justification::centred);
        g.setFont(theme.condFont(13.0f));
        g.setColour(accent.interpolatedWith(Theme::text, 0.25f));
        if (!f.enabled) g.setOpacity(0.4f);
        if (proc.editingMyEq || proc.selectedIsCustom()) {
            // The type can be changed: draw it as a small switch.
            const auto chip = top.removeFromRight(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), type) + 14);
            g.setColour(juce::Colour(0x1fffffff));
            g.fillRoundedRectangle(chip, 4.0f);
            g.setColour(Theme::soft);
            g.drawText(type, chip, juce::Justification::centred);
        } else {
            g.drawText(type, top, juce::Justification::centredRight);
        }
        g.setOpacity(1.0f);
    }
    auto footer = getLocalBounds().removeFromBottom(20);
    g.setColour(Theme::muted);
    g.setFont(theme.bodyFont(12.0f));
    juce::String hint;
    if (proc.editingMyEq) {
        // Gain controls along the bottom; the recommended level next to them.
        g.setFont(theme.labelFont(12.0f));
        g.drawText("GAIN", footer.withWidth(38), juce::Justification::centredLeft);
        g.setFont(theme.bodyFont(12.0f));
        if (!proc.myEq().autoGain)
            g.drawText("Recommended " + formatDb(proc.myEqRecommendedDb()), footer.withTrimmedLeft(200).withTrimmedRight(64 + 8 + 56 + 6),
                       juce::Justification::centredLeft, true);
        return;
    }
    hint = "Number: on/off. Value: drag up or down, or click to type. Graph point: drag, scroll for Q, double-click on/off";
    if (fs.size() > (size_t)maxCards) hint = "+" + juce::String((int)fs.size() - maxCards) + " more filters. " + hint;
    g.drawText(hint, footer.withTrimmedRight(70), juce::Justification::centredLeft, true);
}

// Death-logo style title ---------------------------------------------------------
namespace {
// Chamfer distance (in pixels) from every pixel where `inside` is true to the
// nearest pixel where it is false.
std::vector<float> distanceTo(const std::vector<bool>& inside, int w, int h) {
    const float big = 1e6f, d1 = 1.0f, d2 = 1.4142f;
    std::vector<float> d((size_t)(w * h));
    for (size_t i = 0; i < d.size(); ++i) d[i] = inside[i] ? big : 0.0f;
    auto at = [&](int x, int y) -> float& { return d[(size_t)(y * w + x)]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float& v = at(x, y);
            if (v == 0.0f) continue;
            if (x > 0) v = std::min(v, at(x - 1, y) + d1);
            if (y > 0) {
                v = std::min(v, at(x, y - 1) + d1);
                if (x > 0) v = std::min(v, at(x - 1, y - 1) + d2);
                if (x + 1 < w) v = std::min(v, at(x + 1, y - 1) + d2);
            }
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            float& v = at(x, y);
            if (v == 0.0f) continue;
            if (x + 1 < w) v = std::min(v, at(x + 1, y) + d1);
            if (y + 1 < h) {
                v = std::min(v, at(x, y + 1) + d1);
                if (x + 1 < w) v = std::min(v, at(x + 1, y + 1) + d2);
                if (x > 0) v = std::min(v, at(x - 1, y + 1) + d2);
            }
        }
    return d;
}

// "Born To EQ The World" in the title font, styled like the old Death logo:
// black outline, a band of the accent colour just inside each letter's edge,
// black cores in the thick strokes, and blood running off the letters.
// Drawn at `scale` times the 1x size; `area` is where it goes at 1x.
juce::Image makeDeathTitle(const Theme& theme, juce::Colour accent, float scale, juce::Rectangle<float>& area) {
    const juce::String text = "Born To EQ The World";
    const float h1 = 42.0f, S = scale;
    const auto font = theme.titleFont(h1 * S);
    const float pad = 6.0f * S, drip = 19.0f * S;
    const float width = juce::GlyphArrangement::getStringWidth(font, text);
    const float baseline = pad + font.getAscent();
    const int w = (int)std::ceil(width + pad * 2), h = (int)std::ceil(baseline + font.getDescent() + drip);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, pad, baseline);
    juce::Path letters;
    glyphs.createPath(letters);

    // Blood: runs from the bottom of some letters, with a drop at the end, and a few loose drops.
    struct Run { int glyph; float length, width; };
    const Run runs[] = { { 0, 14.0f, 2.9f }, { 3, 8.0f, 2.2f }, { 8, 16.0f, 3.1f }, { 9, 7.0f, 2.0f },
                         { 15, 13.0f, 2.8f }, { 19, 10.0f, 2.5f } };
    juce::Image probe(juce::Image::SingleChannel, w, h, true);
    {
        juce::Graphics pg(probe);
        pg.setColour(juce::Colours::white);
        pg.fillPath(letters);
    }
    juce::Path blood;
    for (const auto& r : runs) {
        if (r.glyph >= glyphs.getNumGlyphs()) continue;
        const auto gb = glyphs.getBoundingBox(r.glyph, 1, true);
        // The lowest point of the letter near the middle of the glyph.
        int bestX = (int)gb.getCentreX(), bestY = (int)baseline;
        for (int x = (int)(gb.getX() + gb.getWidth() * 0.2f); x < (int)(gb.getRight() - gb.getWidth() * 0.2f); ++x)
            for (int y = h - 1; y > 0; --y)
                if (probe.getPixelAt(juce::jlimit(0, w - 1, x), y).getAlpha() > 160) {
                    if (y > bestY) { bestY = y; bestX = x; }
                    break;
                }
        const float x = (float)bestX, top = (float)bestY - 2.0f * S, len = r.length * S, rw = r.width * S;
        juce::Path p;
        // Wide where it leaves the letter, narrowing, then a round drop at the end.
        p.startNewSubPath(x - rw * 1.3f, top);
        p.quadraticTo(x - rw * 0.35f, top + len * 0.45f, x - rw * 0.45f, top + len * 0.8f);
        p.quadraticTo(x - rw * 1.1f, top + len * 1.05f, x, top + len * 1.2f);
        p.quadraticTo(x + rw * 1.1f, top + len * 1.05f, x + rw * 0.45f, top + len * 0.8f);
        p.quadraticTo(x + rw * 0.35f, top + len * 0.45f, x + rw * 1.3f, top);
        p.closeSubPath();
        blood.addPath(p);
    }
    for (const auto& d : { juce::Point<float>(0.33f, 0.0f), juce::Point<float>(0.72f, 1.0f), juce::Point<float>(0.08f, 0.5f) }) {
        const float x = pad + width * d.x, y = baseline + (10.0f + 4.0f * d.y) * S, r = 1.4f * S;
        blood.addEllipse(x - r, y - r * 1.2f, r * 2.0f, r * 2.4f);
    }
    letters.addPath(blood);

    // Distance from each letter's edge, inside and outside.
    juce::Image mask(juce::Image::SingleChannel, w, h, true);
    {
        juce::Graphics mg(mask);
        mg.setColour(juce::Colours::white);
        mg.fillPath(letters);
    }
    std::vector<bool> in((size_t)(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) in[(size_t)(y * w + x)] = mask.getPixelAt(x, y).getAlpha() >= 128;
    const auto din = distanceTo(in, w, h);
    std::vector<bool> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = !in[i];
    const auto dout = distanceTo(out, w, h);

    // Signed distance (positive inside), then the layers by threshold.
    const float outline = 2.2f * S, edge = 0.5f * S, core = 1.35f * S;
    const auto top = accent.brighter(0.35f), bottom = accent.darker(0.55f);
    const auto black = juce::Colour(0xff050506);
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y) {
        const float t = juce::jlimit(0.0f, 1.0f, ((float)y - pad) / (baseline - pad));
        const auto band = top.interpolatedWith(bottom, t);
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)(y * w + x);
            const float sd = in[i] ? din[i] - 0.5f : -(dout[i] - 0.5f);
            auto cover = [&](float threshold) { return juce::jlimit(0.0f, 1.0f, sd - threshold + 0.5f); };
            const float a = cover(-outline), b = cover(edge), c = cover(core);
            if (a <= 0.0f) continue;
            const auto col = black.interpolatedWith(band, b).interpolatedWith(black, c);
            bd.setPixelColour(x, y, col.withAlpha(a * 0.96f));
        }
    }
    // Same place as plain text drawn centred in (26, 12, 460, 48): baseline at 15 + ascent.
    area = { 26.0f - pad / S, 15.0f - pad / S, (float)w / S, (float)h / S };
    return img;
}
} // namespace

// ===========================================================================
// Editor
// ===========================================================================
BteqEditor::BteqEditor(BteqProcessor& p)
    : AudioProcessorEditor(&p), proc(p), lnf(*theme), graph(p, *theme), filters(p, *theme) {
    setLookAndFeel(&lnf);
    loadBackground();

    bypassButton.setButtonText("Bypass");
    bypassButton.setClickingTogglesState(true);
    bypassButton.getProperties().set("kind", "bypass");
    content.addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "bypass", bypassButton);

    // Standalone: one switch makes Equalizer APO apply the EQ to the whole system.
    apoButton.getProperties().set("kind", "pill");
    apoButton.setTitle("Equalizer APO");
    apoButton.setTooltip("Send this EQ to Equalizer APO so it applies to everything you play, like Peace");
    apoButton.onClick = [this] {
        proc.setApoOutput(!proc.apoOutput());
        apoButton.setToggleState(proc.apoOutput(), juce::dontSendNotification);
    };
    if (proc.standaloneUi) content.addAndMakeVisible(apoButton);

    search.setTextToShowWhenEmpty("Search headphones", Theme::muted);
    search.setFont(theme->bodyFont(16.0f));
    search.setIndents(36, 13);
    search.setTitle("Search headphones");
    search.onTextChange = [this] { refreshHeadphones(); };
    content.addAndMakeVisible(search);

    for (auto* lb : { &headphoneList, &targetList }) {
        lb->setRowHeight(34);
        lb->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        content.addAndMakeVisible(*lb);
    }
    headphoneList.setTitle("Matching headphones");
    targetList.setTitle("Targets");

    for (auto* tab : { &parametricTab, &graphicTab }) {
        tab->getProperties().set("kind", "tab");
        tab->setRadioGroupId(1);
        content.addAndMakeVisible(*tab);
    }
    parametricTab.onClick = [this] { setMode(false); };
    graphicTab.onClick = [this] { setMode(true); };

    content.addAndMakeVisible(graph);
    content.addAndMakeVisible(filters);
    graph.onMyEqToggled = [this] {
        refreshEditing();
        filters.repaint();
        content.repaint();
    };

    correction.setSliderStyle(juce::Slider::LinearHorizontal);
    correction.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    correction.setTitle("Correction amount");
    content.addAndMakeVisible(correction);
    correctionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, "amount", correction);

    for (int i = 0; i < Theme::numAccents; ++i) {
        auto& s = swatches[(size_t)i];
        s.getProperties().set("kind", "swatch");
        s.getProperties().set("swatch", (juce::int64)Theme::accent(i).getARGB());
        s.setTitle(Theme::accentName(i));
        s.setTooltip(Theme::accentName(i));
        s.setRadioGroupId(2);
        s.onClick = [this, i] { setAccent(i); };
        content.addAndMakeVisible(s);
    }

    preampValue.setFont(theme->labelFont(16.0f));
    preampValue.setColour(juce::Label::textColourId, Theme::text);
    preampValue.setJustificationType(juce::Justification::centredLeft);
    preampValue.setBorderSize({ 0, 0, 0, 0 });
    preampValue.setEditable(true, false, false);
    preampValue.setMouseCursor(juce::MouseCursor::IBeamCursor);
    preampValue.setTitle("Preamp");
    preampValue.setTooltip("Double-click to type a preamp level");
    preampValue.onTextChange = [this] {
        const double v = parseTyped(preampValue.getText(), false);
        if (!std::isnan(v)) proc.setPreamp(v);
        refreshEditing();
    };
    content.addAndMakeVisible(preampValue);

    resetButton.getProperties().set("kind", "small");
    resetButton.setTooltip("Go back to the published settings for this target");
    resetButton.onClick = [this] {
        if (proc.editingMyEq) proc.resetMyEq();
        else proc.resetEdits();
        refreshEditing();
        content.repaint();
    };
    content.addChildComponent(resetButton);

    savedButton.getProperties().set("kind", "dropdown");
    savedButton.setTitle("Saved profiles");
    savedButton.onClick = [this] { showSavedMenu(); };
    content.addAndMakeVisible(savedButton);

    preampKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    preampKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    preampKnob.setRange(-30.0, 12.0, 0.1);
    preampKnob.setMouseDragSensitivity(300);
    preampKnob.setTitle("Preamp");
    preampKnob.setTooltip("Drag to set the preamp. Right-click to type a value, double-click for the published value.");
    preampKnob.onValueChange = [this] {
        if (std::abs(preampKnob.getValue() - proc.preampSettingDb()) > 0.04) proc.setPreamp(preampKnob.getValue());
        refreshEditing();
    };
    preampKnob.onRightClick = [this] { preampValue.showEditor(); };
    preampKnob.onDoubleClick = [this] { proc.resetPreamp(); refreshEditing(); };
    content.addAndMakeVisible(preampKnob);
    preampValue.setEditable(false, true, false);

    for (auto* b : { &collapseButton, &expandButton }) {
        b->getProperties().set("kind", "small");
        content.addChildComponent(*b);
    }
    collapseButton.setTooltip("Hide the headphone and target boxes to make the EQ wider");
    expandButton.setTooltip("Show the headphone and target boxes");
    collapseButton.onClick = [this] { toggleCollapse(0); };
    expandButton.onClick = [this] { toggleCollapse(0); };

    content.addMouseListener(this, true);
    addAndMakeVisible(content);
    setAccent(proc.accentIndex);
    setUiScale(proc.uiScale);

    // Start with the selected headphone (if any) shown in the search box.
    if (proc.selectedHeadphone().isNotEmpty()) search.setText(proc.selectedHeadphone(), false);
    refreshHeadphones();
    refreshTargets();
    refreshEditing();
    startTimerHz(15);
}

BteqEditor::~BteqEditor() {
    if (titleWindow != nullptr) titleWindow->setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// Standalone: the window around the editor gets the black Lagrangian title bar.
void BteqEditor::styleTitleBar() {
    if (!proc.standaloneUi || titleWindow != nullptr) return;
    if (auto* w = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
        titleLnf.accentColour = Theme::accent(proc.accentIndex);
        titleWindow = w;
        w->setLookAndFeel(&titleLnf);
        w->setBackgroundColour(juce::Colours::black);
        w->repaint();
    }
}

void BteqEditor::setAccent(int index) {
    proc.accentIndex = juce::jlimit(0, Theme::numAccents - 1, index);
    if (proc.accentIndex == Theme::imageAccent && !Theme::hasImage) proc.accentIndex = 0; // the image was removed
    swatches[(size_t)Theme::imageAccent].getProperties().set("swatch", (juce::int64)Theme::imageColour.getARGB());
    lnf.accentColour = graph.accent = filters.accent = Theme::accent(proc.accentIndex);
    titleImage = makeDeathTitle(*theme, Theme::accent(proc.accentIndex), 3.0f, titleArea);
    swatches[(size_t)proc.accentIndex].setToggleState(true, juce::dontSendNotification);
    titleLnf.accentColour = Theme::accent(proc.accentIndex);
    if (titleWindow != nullptr) titleWindow->repaint();
    content.repaint();
}

void BteqEditor::setMode(bool graphicMode) {
    if (auto* prm = proc.params.getParameter("mode")) prm->setValueNotifyingHost(graphicMode ? 1.0f : 0.0f);
}

void BteqEditor::refreshHeadphones() {
    results = proc.library().search(search.getText().toStdString(), 500);
    headphoneList.updateContent();
    headphoneList.repaint();
}

void BteqEditor::refreshTargets() {
    targetRows.clear();
    shownHeadphone = proc.selectedHeadphone();
    shownProfile = proc.selectedProfileIndex();
    shownMatch = proc.matchedHeadphone();
    if (const auto idx = proc.library().headphoneIndex(shownHeadphone.toStdString())) {
        targetRows.push_back({ true, "Sound like another headphone", -1 });
        targetRows.push_back({ false, proc.matchedHeadphone().isNotEmpty() ? proc.matchedHeadphone()
                                                                           : juce::String("Choose a headphone..."), kMatchRow });
        const auto profs = proc.profilesFor(shownHeadphone);
        juce::String lastHeading;
        for (size_t i = 0; i < profs.size(); ++i) {
            const auto* pr = profs[i];
            const juce::String heading = BteqProcessor::isCustom(pr) ? juce::String("Your presets")
                                       : pr->originalPreset ? juce::String(pr->source) + "'s own presets"
                                                            : juce::String(pr->sourceLabel()) + " via AutoEQ";
            if (heading != lastHeading) {
                targetRows.push_back({ true, heading, -1 });
                lastHeading = heading;
            }
            targetRows.push_back({ false, juce::String(pr->target), (int)i });
        }
        if (lastHeading != "Your presets") targetRows.push_back({ true, "Your presets", -1 });
        targetRows.push_back({ false, "New preset...", kNewPresetRow });
    }
    targetList.updateContent();
    targetList.repaint();
}

void BteqEditor::timerCallback() {
    styleTitleBar();
    if (proc.selectedHeadphone() != shownHeadphone || proc.selectedProfileIndex() != shownProfile ||
        proc.matchedHeadphone() != shownMatch)
        refreshTargets();

    const auto* p = proc.selectedProfile();
    const bool hasP = p != nullptr && p->hasParametric, hasG = proc.graphicAvailable();
    parametricTab.setEnabled((hasP || p == nullptr) && !proc.currentMatch().active);
    graphicTab.setEnabled(hasG);
    const bool graphic = proc.graphicModeActive();
    parametricTab.setToggleState(!graphic, juce::dontSendNotification);
    graphicTab.setToggleState(graphic, juce::dontSendNotification);
    bypassButton.setButtonText(bypassButton.getToggleState() ? "Bypassed" : "Bypass");
    if (proc.standaloneUi) {
        apoButton.setToggleState(proc.apoOutput(), juce::dontSendNotification);
        const auto apoText = proc.apoOutput() ? juce::String::fromUTF8("Equalizer APO \xc2\xb7 on") : "Equalizer APO";
        if (apoButton.getButtonText() != apoText) apoButton.setButtonText(apoText);
        const auto error = proc.apoError();
        if (error != shownApoError) {
            shownApoError = error;
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Equalizer APO", error, "OK", this);
        }
    }

    refreshEditing();
    graph.repaint();
    filters.repaint();
    content.repaint(rCorrection.withHeight(40));
    content.repaint(rCorrection.withTrimmedTop(100).withHeight(30));
    content.repaint(juce::Rectangle<int>(680, 0, 250, 72));
    content.repaint(rTabs.withX(rTabs.getRight()).withWidth(rGraph.getRight() - rTabs.getRight()));
    content.repaint(rHeadphone.withHeight(44));
    content.repaint(rTarget.withHeight(44));
}

void BteqEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0B0C0D)); }

void BteqEditor::paintContent(juce::Graphics& g) {
    const auto bounds = content.getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff0B0C0D));
    if (background.isValid())
        g.drawImageWithin(background, 0, 0, content.getWidth(), content.getHeight(), juce::RectanglePlacement::fillDestination);
    // Haze over the image, tinted by the image's dark tones (neutral for the forest).
    juce::ColourGradient fog(Theme::fog.withAlpha((juce::uint8)0x40), bounds.getWidth() * 0.55f, bounds.getHeight() * 0.3f,
                             Theme::fog.darker(0.25f).withAlpha((juce::uint8)0xe6), 0.0f, bounds.getHeight() * 1.05f, true);
    fog.addColour(0.55, Theme::fog.withAlpha((juce::uint8)0x9e));
    g.setGradientFill(fog);
    g.fillRect(bounds);

    // Header.
    g.setColour(juce::Colour(0x14ffffff));
    g.drawHorizontalLine(72, 0.0f, bounds.getWidth());
    g.setOpacity(1.0f);
    if (titleImage.isValid()) g.drawImage(titleImage, titleArea, juce::RectanglePlacement::stretchToFit);
    const auto titleFont = theme->titleFont(42.0f);
    const int titleW = (int)juce::GlyphArrangement::getStringWidth(titleFont, "Born To EQ The World");
    g.setColour(Theme::muted);
    g.setFont(theme->labelFont(15.0f));
    if (!proc.standaloneUi) // the standalone has its Equalizer APO switch here
        g.drawText("HEADPHONE CORRECTION", 26 + titleW + 18, 27, 260, 24, juce::Justification::centredLeft);

    const double pre = proc.currentPreampDb() * (proc.params.getRawParameterValue("bypass")->load() > 0.5f ? 0.0 : 1.0);
    g.setFont(theme->labelFont(16.0f));
    g.setColour(Theme::muted);
    g.drawText("PREAMP", 686, 20, 66, 32, juce::Justification::centredRight);

    // Panels.
    for (auto r : { rHeadphone, rTarget, rTabs, rGraph, rFilters, rCorrection, rSummary, rColour })
        if (!r.isEmpty()) drawPanel(g, r);

    g.setFont(theme->labelFont(15.0f));
    g.setColour(Theme::muted);
    // Box headings with a fold arrow; a folded box shows what is chosen.
    auto heading = [&](juce::Rectangle<int> r, const char* label, bool folded, const juce::String& chosen, int trimRight) {
        if (r.isEmpty()) return;
        juce::Path arrow;
        const float ax = (float)r.getX() + 18.0f, ay = (float)r.getY() + 21.0f;
        if (folded) arrow.addTriangle(ax - 3, ay - 5, ax - 3, ay + 5, ax + 4, ay);
        else arrow.addTriangle(ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
        g.setColour(Theme::muted);
        g.fillPath(arrow);
        g.setFont(theme->labelFont(15.0f));
        g.drawText(label, r.getX() + 30, r.getY() + 12, 120, 18, juce::Justification::centredLeft);
        if (folded) {
            const int lw = (int)juce::GlyphArrangement::getStringWidth(theme->labelFont(15.0f), label);
            g.setColour(Theme::text);
            g.setFont(theme->bodyFont(14.0f, true));
            g.drawText(chosen, r.getX() + 30 + lw + 12, r.getY() + 12, r.getWidth() - 30 - lw - 24 - trimRight, 18,
                       juce::Justification::centredLeft, true);
        }
    };
    heading(rHeadphone, "HEADPHONE", proc.headphoneCollapsed, headphoneSummary(), 36);
    heading(rTarget, "TARGET", proc.targetCollapsed, targetSummary(), 0);
    g.setFont(theme->labelFont(15.0f));
    g.setColour(Theme::muted);
    g.drawText("CORRECTION", rCorrection.getX() + 14, rCorrection.getY() + 16, 120, 18, juce::Justification::centredLeft);


    // Search icon.
    if (search.isVisible()) {
        const auto sb = search.getBounds().toFloat();
        g.setColour(Theme::muted);
        g.drawEllipse(sb.getX() + 13, sb.getCentreY() - 7, 11, 11, 1.8f);
        g.drawLine(sb.getX() + 22.5f, sb.getCentreY() + 2.5f, sb.getX() + 27, sb.getCentreY() + 7, 1.8f);
    }

    // Mode note next to the tabs.
    g.setFont(theme->bodyFont(14.0f));
    g.setColour(Theme::soft);
    const auto* p = proc.selectedProfile();
    juce::String note;
    if (proc.editingMyEq) {
        note = proc.myEqPerTarget() ? "Editing My EQ for this target" : "Editing My EQ for every target";
        if (!proc.myEq().on) note << " (switched off)";
    } else if (p != nullptr) {
        if (proc.currentMatch().active) note = "Sounding like " + proc.matchedHeadphone();
        else if (proc.matchedHeadphone().isNotEmpty()) note = "No measurement to match with for this source";
        else if (proc.graphicModeActive()) note = "Wavelet-style 127-point curve";
        else if (BteqProcessor::isCustom(p)) note = "Your preset \"" + juce::String(p->target) + "\": drag the values or points to shape it";
        else if (p->originalPreset && !proc.selectionEdited()) note = juce::String((int)p->parametric.filters.size()) + " filters, exactly as " + juce::String(p->source) + " published them";
        else if (p->originalPreset) note = juce::String((int)p->parametric.filters.size()) + " filters from " + juce::String(p->source);
        else note = juce::String((int)p->parametric.filters.size()) + " filters from AutoEQ";
        if (!proc.graphicAvailable() && p->hasParametric && !BteqProcessor::isCustom(p)) note << " (no graphic version)";
        if (proc.selectionEdited()) note << ", edited by you";
        if (proc.myEqActive()) note << ", plus My EQ";
    }
    if (rSummary.isEmpty()) {
        g.drawText(note, rTabs.getRight() + 14, rTabs.getY(), rGraph.getRight() - rTabs.getRight() - 14,
                   rTabs.getHeight(), juce::Justification::centredRight);
    } else {
        // Left side hidden: what is selected, then the note.
        auto r = rSummary.withTrimmedLeft(74).withTrimmedRight(14).reduced(0, 5);
        g.setColour(Theme::text);
        g.setFont(theme->bodyFont(14.0f, true));
        g.drawText(headphoneSummary() + "  /  " + targetSummary(), r.removeFromTop(19), juce::Justification::centredLeft, true);
        g.setColour(Theme::muted);
        g.setFont(theme->bodyFont(12.5f));
        g.drawText(note, r, juce::Justification::centredLeft, true);
    }

    // Correction readouts.
    const int pct = (int)std::round(proc.params.getRawParameterValue("amount")->load());
    g.setColour(Theme::textBright);
    g.setFont(theme->labelFont(36.0f).withExtraKerningFactor(0.0f));
    g.drawText(juce::String(pct) + "%", rCorrection.getRight() - 114, rCorrection.getY() + 8, 100, 38,
               juce::Justification::centredRight);
    g.setFont(theme->bodyFont(12.0f));
    g.setColour(Theme::muted);
    const auto sl = correction.getBounds();
    g.drawText("Off", sl.getX(), sl.getBottom() + 2, 40, 14, juce::Justification::centredLeft);
    g.drawText("50%", sl.getCentreX() - 20, sl.getBottom() + 2, 40, 14, juce::Justification::centred);
    g.drawText("Full", sl.getRight() - 40, sl.getBottom() + 2, 40, 14, juce::Justification::centredRight);
    g.setFont(theme->bodyFont(13.0f));
    g.setColour(juce::Colour(0xffB2AFA9));
    g.drawText("Preamp applied now", rCorrection.getX() + 14, rCorrection.getY() + 104, 150, 20,
               juce::Justification::centredLeft);
    g.setColour(Theme::text);
    g.drawText(formatDb(pre), rCorrection.getRight() - 114, rCorrection.getY() + 104, 100, 20,
               juce::Justification::centredRight);


    if (!proc.libraryLoaded()) {
        g.setColour(juce::Colour(0xffFFB5A6));
        g.setFont(theme->bodyFont(14.0f));
        g.drawText("The headphone library could not be loaded.", rTarget.reduced(14), juce::Justification::centred);
    }
}

void BteqEditor::resized() {}

void BteqEditor::layoutContent() {
    // Left side: headphone and target boxes, each can fold down to its heading.
    // With the whole left side hidden, the EQ takes the full width and a
    // summary next to the tabs says what is selected.
    const int top = 90, bottom = 662, left = 18, gap = 14, headH = 44, sideW = 312;
    const bool side = !proc.sidebarCollapsed;
    if (side) {
        const int hpH = proc.headphoneCollapsed ? headH
                      : proc.targetCollapsed     ? bottom - top - gap - headH
                                                 : 254;
        rHeadphone = { left, top, sideW, hpH };
        const int tgY = rHeadphone.getBottom() + gap;
        rTarget = { left, tgY, sideW, proc.targetCollapsed ? headH : bottom - tgY };
    } else {
        rHeadphone = rTarget = {};
    }
    const int x = side ? left + sideW + 16 : left, right = 1062;
    rTabs = { x, top, 276, 48 };
    rSummary = side ? juce::Rectangle<int>() : juce::Rectangle<int>(rTabs.getRight() + gap, top, right - rTabs.getRight() - gap, 48);
    rGraph = { x, 150, right - x, 290 };
    // Correction box, with the colour dots in their own small box under its right half.
    rColour = { 1062 - 118, bottom - 36, 118, 36 };
    rCorrection = { 826, 452, 236, rColour.getY() - gap - 452 };
    rFilters = { x, 452, rCorrection.getX() - 14 - x, bottom - 452 };

    const bool showHp = side && !proc.headphoneCollapsed, showTg = side && !proc.targetCollapsed;
    search.setVisible(showHp);
    headphoneList.setVisible(showHp);
    targetList.setVisible(showTg);
    collapseButton.setVisible(side);
    expandButton.setVisible(!side);
    collapseButton.setBounds(rHeadphone.getRight() - 10 - 30, rHeadphone.getY() + 10, 30, 22);
    expandButton.setBounds(rSummary.getX() + 10, rSummary.getY() + 13, 52, 22);

    bypassButton.setBounds(1080 - 26 - 110, 14, 110, 44);
    preampKnob.setBounds(756, 13, 46, 46);
    search.setBounds(rHeadphone.getX() + 14, rHeadphone.getY() + 38, rHeadphone.getWidth() - 28, 44);
    headphoneList.setBounds(rHeadphone.getX() + 10, rHeadphone.getY() + 90, rHeadphone.getWidth() - 20,
                            rHeadphone.getHeight() - 100);
    targetList.setBounds(rTarget.getX() + 10, rTarget.getY() + 38, rTarget.getWidth() - 20,
                         rTarget.getHeight() - 48);
    parametricTab.setBounds(rTabs.getX() + 4, rTabs.getY() + 4, 132, 40);
    graphicTab.setBounds(rTabs.getX() + 140, rTabs.getY() + 4, 132, 40);
    graph.setBounds(rGraph.reduced(12, 8));
    filters.setBounds(rFilters.reduced(12));
    correction.setBounds(rCorrection.getX() + 12, rCorrection.getY() + 58, rCorrection.getWidth() - 24, 26);
    preampValue.setBounds(808, 20, 100, 32);
    resetButton.setBounds(rFilters.getRight() - 12 - 64, rFilters.getBottom() - 12 - 20, 64, 22);
    savedButton.setBounds(462, 18, 210, 36);
    apoButton.setBounds(savedButton.getX() - 18 - 160, 21, 160, 30);
    const int shown = Theme::hasImage ? Theme::numAccents : Theme::numBuiltIn;
    const int dot = 20, dotGap = 6, dotsW = shown * dot + (shown - 1) * dotGap;
    swatches[(size_t)Theme::imageAccent].setVisible(Theme::hasImage);
    for (int i = 0; i < shown; ++i)
        swatches[(size_t)i].setBounds(rColour.getX() + (rColour.getWidth() - dotsW) / 2 + i * (dot + dotGap),
                                      rColour.getCentreY() - dot / 2, dot, dot);
}

void BteqEditor::toggleCollapse(int what) {
    if (what == 0) proc.sidebarCollapsed = !proc.sidebarCollapsed;
    if (what == 1) proc.headphoneCollapsed = !proc.headphoneCollapsed;
    if (what == 2) proc.targetCollapsed = !proc.targetCollapsed;
    layoutContent();
    content.repaint();
}

juce::String BteqEditor::headphoneSummary() const {
    return proc.selectedHeadphone().isNotEmpty() ? proc.selectedHeadphone() : juce::String("None chosen");
}

juce::String BteqEditor::targetSummary() const {
    const auto* p = proc.selectedProfile();
    if (p == nullptr) return "None chosen";
    if (proc.currentMatch().active) return "Sounding like " + proc.matchedHeadphone();
    const juce::String from = p->originalPreset ? juce::String(p->source) + "'s own preset" : juce::String(p->sourceLabel());
    return juce::String(p->target) + " (" + from + ")";
}

// Sound like another headphone ---------------------------------------------------
namespace {
// Search box and list of headphones that have a measurement.
class MatchChooser : public juce::Component, private juce::ListBoxModel {
public:
    MatchChooser(BteqProcessor& p, const Theme& t, std::function<void(juce::String)> chosen)
        : proc(p), theme(t), onChosen(std::move(chosen)) {
        search.setTextToShowWhenEmpty("Search headphones to sound like", Theme::muted);
        search.setFont(theme.bodyFont(15.0f));
        search.setIndents(12, 10);
        search.onTextChange = [this] { refresh(); };
        addAndMakeVisible(search);
        list.setRowHeight(30);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(list);
        clear.getProperties().set("kind", "small");
        clear.onClick = [this] { onChosen({}); };
        addChildComponent(clear);
        clear.setVisible(proc.matchedHeadphone().isNotEmpty());
        setSize(340, 360);
        refresh();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(4);
        search.setBounds(r.removeFromTop(38));
        r.removeFromTop(6);
        if (clear.isVisible()) clear.setBounds(r.removeFromBottom(26).removeFromRight(120));
        list.setBounds(r);
    }
    void parentHierarchyChanged() override { search.grabKeyboardFocus(); }

private:
    void refresh() {
        results.clear();
        const auto* curves = proc.curveLibrary();
        if (curves == nullptr) return;
        for (const auto i : proc.library().search(search.getText().toStdString(), 400)) {
            const std::string name = proc.library().headphoneName(i);
            if (juce::String(name) != proc.selectedHeadphone() && curves->hasMeasurement(name)) results.push_back(name);
        }
        list.updateContent();
        list.repaint();
    }
    int getNumRows() override { return (int)results.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
        if (row < 0 || row >= (int)results.size()) return;
        const juce::String name(results[(size_t)row]);
        const bool on = name == proc.matchedHeadphone();
        if (on) {
            g.setColour(Theme::selected);
            g.fillRoundedRectangle(0.0f, 1.0f, (float)w, (float)h - 2.0f, 6.0f);
        }
        g.setColour(on ? Theme::textBright : Theme::soft);
        g.setFont(theme.bodyFont(15.0f, on));
        g.drawText(name, 10, 0, w - 16, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        if (row >= 0 && row < (int)results.size()) onChosen(juce::String(results[(size_t)row]));
    }

    BteqProcessor& proc;
    const Theme& theme;
    std::function<void(juce::String)> onChosen;
    juce::TextEditor search;
    juce::ListBox list { "Headphones to sound like", this };
    juce::TextButton clear { "Stop matching" };
    std::vector<std::string> results;
};
} // namespace

void BteqEditor::showMatchChooser(juce::Rectangle<int> rowInList) {
    juce::Component::SafePointer<BteqEditor> safe(this);
    auto chooser = std::make_unique<MatchChooser>(proc, *theme, [safe](juce::String name) {
        if (safe == nullptr) return;
        safe->proc.setMatch(name);
        safe->refreshTargets();
        safe->content.repaint();
        if (auto* box = safe->matchBox.getComponent()) box->dismiss();
    });
    const auto area = targetList.getBounds().getTopLeft() + rowInList.getPosition();
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(chooser),
                                                       juce::Rectangle<int>(area.x, area.y, rowInList.getWidth(), rowInList.getHeight()),
                                                       &content);
    matchBox = &box;
}

// Window scale ------------------------------------------------------------------
namespace {
juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Born To EQ The World")
        .getChildFile("settings.xml");
}
} // namespace

void BteqEditor::setUiScale(float scale) {
    if (scale <= 0.0f) { // new instance: use the last scale picked anywhere
        scale = 1.0f;
        if (auto xml = juce::XmlDocument::parse(settingsFile())) scale = (float)xml->getDoubleAttribute("uiScale", 1.0);
    }
    scale = juce::jlimit(0.5f, 1.25f, scale);
    proc.uiScale = scale;
    content.setBounds(0, 0, 1080, 680);
    content.setTransform(juce::AffineTransform::scale(scale));
    layoutContent();
    setSize(juce::roundToInt(1080 * scale), juce::roundToInt(680 * scale));
}

void BteqEditor::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        // A click on a box's heading folds it up or opens it.
        if (e.eventComponent != &content) return;
        const auto pos = e.getEventRelativeTo(&content).getPosition();
        if (rHeadphone.withHeight(44).withTrimmedRight(44).contains(pos)) toggleCollapse(1);
        else if (rTarget.withHeight(44).contains(pos)) toggleCollapse(2);
        else if (rSummary.contains(pos)) toggleCollapse(0);
        return;
    }
    // Leave right-clicks on knobs and text fields to them.
    for (auto* c = e.eventComponent; c != nullptr && c != &content; c = c->getParentComponent())
        if (dynamic_cast<juce::Slider*>(c) != nullptr || dynamic_cast<juce::TextEditor*>(c) != nullptr ||
            dynamic_cast<juce::Label*>(c) != nullptr)
            return;
    showScaleMenu();
}

void BteqEditor::showScaleMenu() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Window size");
    const int sizes[] = { 125, 110, 100, 90, 75, 60, 50 };
    for (int pct : sizes)
        menu.addItem(pct, juce::String(pct) + "%", true, juce::roundToInt(proc.uiScale * 100.0f) == pct);
    constexpr int kImage = 1001, kForest = 1002;
    menu.addSectionHeader("Background");
    menu.addItem(kImage, Theme::hasImage ? "Choose another image..." : "Your own image...");
    menu.addItem(kForest, "Forest", true, !Theme::hasImage);
    juce::Component::SafePointer<BteqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe](int id) {
        if (safe == nullptr || id <= 0) return;
        if (id == kImage) { safe->chooseBackground(); return; }
        if (id == kForest) { safe->useForest(); return; }
        safe->setUiScale((float)id / 100.0f);
        auto xml = juce::XmlDocument::parse(settingsFile());
        if (xml == nullptr) xml = std::make_unique<juce::XmlElement>("SETTINGS");
        xml->setAttribute("uiScale", (double)id / 100.0);
        settingsFile().getParentDirectory().createDirectory();
        xml->writeTo(settingsFile());
    });
}

// Background ------------------------------------------------------------------
juce::File BteqEditor::backgroundFile() { return settingsFile().getSiblingFile("background.png"); }

// Uses your image if there is one (picking the colours from it), else the forest.
void BteqEditor::loadBackground() {
    juce::Image img;
    if (backgroundFile().existsAsFile()) img = juce::ImageFileFormat::loadFrom(backgroundFile());
    Theme::hasImage = img.isValid();
    if (!img.isValid()) {
        background = juce::ImageCache::getFromMemory(BinaryData::forest_png, BinaryData::forest_pngSize);
        Theme::fog = juce::Colour(0xff0A0B0C);
        return;
    }
    background = img;
    // Colours from a small copy: plenty for picking, and quick.
    const int w = 96, h = juce::jmax(1, juce::roundToInt(96.0 * img.getHeight() / img.getWidth()));
    const auto small = img.rescaled(w, h, juce::Graphics::mediumResamplingQuality);
    std::vector<uint8_t> rgb;
    rgb.reserve((size_t)(w * h * 3));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto c = small.getPixelAt(x, y);
            rgb.push_back(c.getRed());
            rgb.push_back(c.getGreen());
            rgb.push_back(c.getBlue());
        }
    const auto picked = bteq::imagetheme::pick(rgb);
    auto colour = [](const bteq::imagetheme::Rgb& c) {
        return juce::Colour::fromFloatRGBA((float)c.r, (float)c.g, (float)c.b, 1.0f);
    };
    Theme::imageColour = colour(picked.accent);
    Theme::imageMeasured = colour(picked.measured);
    Theme::imageEq = colour(picked.eq);
    Theme::fog = colour(picked.fog);
    Theme::imageVivid = picked.vivid;
}

void BteqEditor::chooseBackground() {
    fileChooser = std::make_unique<juce::FileChooser>("Choose a background image", juce::File(), "*.jpg;*.jpeg;*.png;*.gif");
    juce::Component::SafePointer<BteqEditor> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        auto img = juce::ImageFileFormat::loadFrom(fc.getResult());
        if (!img.isValid()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Background image",
                                                   "That file couldn't be read as an image.");
            return;
        }
        // Keep a copy no bigger than the window at 2x, so it loads fast and survives the original moving.
        const double fit = juce::jmax(2160.0 / img.getWidth(), 1360.0 / img.getHeight());
        if (fit < 1.0)
            img = img.rescaled(juce::roundToInt(img.getWidth() * fit), juce::roundToInt(img.getHeight() * fit),
                               juce::Graphics::highResamplingQuality);
        backgroundFile().getParentDirectory().createDirectory();
        juce::FileOutputStream out(backgroundFile());
        if (!out.openedOk() || !out.setPosition(0) || !out.truncate().wasOk()) return;
        juce::PNGImageFormat().writeImageToStream(img.convertedToFormat(juce::Image::RGB), out);
        out.flush();
        safe->loadBackground();
        safe->setAccent(Theme::imageAccent); // show the picked colour straight away
        safe->backgroundChanged();
    });
}

void BteqEditor::useForest() {
    backgroundFile().deleteFile();
    loadBackground();
    if (proc.accentIndex == Theme::imageAccent) setAccent(0);
    backgroundChanged();
}

void BteqEditor::backgroundChanged() {
    setAccent(proc.accentIndex);
    layoutContent();
    graph.repaint();
    filters.repaint();
    content.repaint();
}

// Editing ---------------------------------------------------------------------
void BteqEditor::refreshEditing() {
    filters.refresh();
    const auto* p = proc.selectedProfile();
    preampValue.setVisible(p != nullptr);
    if (!preampValue.isBeingEdited()) {
        const auto text = formatDb(proc.preampSettingDb());
        if (preampValue.getText() != text) preampValue.setText(text, juce::dontSendNotification);
    }
    resetButton.setVisible(proc.editingMyEq ? proc.myEqEdited() : proc.selectionEdited());
    resetButton.setTooltip(proc.editingMyEq ? "Set every My EQ band back to 0 dB"
                                            : "Go back to the published settings for this target");
    preampKnob.setVisible(p != nullptr);
    if (!preampKnob.isMouseButtonDown() && std::abs(preampKnob.getValue() - proc.preampSettingDb()) > 0.04)
        preampKnob.setValue(proc.preampSettingDb(), juce::dontSendNotification);

    // Saved profiles dropdown: show the saved name until the selection changes.
    const auto selection = proc.selectedHeadphone() + "|" + juce::String(proc.selectedProfileIndex());
    if (loadedSavedName.isNotEmpty() && selection != loadedSavedSelection) loadedSavedName.clear();
    const auto label = loadedSavedName.isNotEmpty() ? loadedSavedName : juce::String("Saved profiles");
    if (savedButton.getButtonText() != label) {
        savedButton.setButtonText(label);
        savedButton.getProperties().set("placeholder", loadedSavedName.isEmpty());
        savedButton.repaint();
    }
}

// Saved profiles ----------------------------------------------------------------
// Kept in one file shared by every instance and session:
// %APPDATA%\Born To EQ The World\saved-profiles.xml on Windows.
juce::File BteqEditor::savedProfilesFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Born To EQ The World")
        .getChildFile("saved-profiles.xml");
}

juce::ValueTree BteqEditor::loadSavedProfiles() {
    if (auto xml = juce::XmlDocument::parse(savedProfilesFile()))
        if (xml->hasTagName("SAVED")) return juce::ValueTree::fromXml(*xml);
    return juce::ValueTree("SAVED");
}

void BteqEditor::storeSavedProfiles(const juce::ValueTree& saved) {
    const auto file = savedProfilesFile();
    file.getParentDirectory().createDirectory();
    if (auto xml = saved.createXml()) xml->writeTo(file);
}

void BteqEditor::askForName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> done) {
    nameWindow = std::make_unique<juce::AlertWindow>(title, "", juce::MessageBoxIconType::NoIcon, this);
    nameWindow->setLookAndFeel(&lnf);
    nameWindow->addTextEditor("name", initial, "Name");
    nameWindow->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    nameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (auto* te = nameWindow->getTextEditor("name")) te->setFont(theme->bodyFont(16.0f));
    juce::Component::SafePointer<BteqEditor> safe(this);
    nameWindow->enterModalState(true, juce::ModalCallbackFunction::create([safe, done](int result) {
        if (safe == nullptr) return;
        const auto name = safe->nameWindow->getTextEditorContents("name").trim();
        safe->nameWindow.reset();
        if (result == 1 && name.isNotEmpty()) done(name);
    }));
    if (auto* te = nameWindow->getTextEditor("name")) {
        te->grabKeyboardFocus();
        te->selectAll();
    }
}

// ---------------------------------------------------------------------------
// Your own presets
// ---------------------------------------------------------------------------
void BteqEditor::showNewPresetMenu() {
    const auto* p = proc.selectedProfile();
    const bool canCopy = p != nullptr && p->hasParametric && !proc.currentMatch().active;
    juce::PopupMenu menu;
    menu.addSectionHeader("New preset for " + proc.selectedHeadphone());
    menu.addItem(1, "Start flat (10 filters at 0 dB)");
    menu.addItem(2, canCopy ? "Copy the selected target's filters" : "Copy the selected target's filters (parametric only)", canCopy);
    menu.addItem(3, "Import a ParametricEQ.txt file...");
    juce::Component::SafePointer<BteqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&targetList), [safe](int result) {
        if (safe == nullptr || result == 0) return;
        if (result == 1) {
            bteq::ParametricPreset flat;
            flat.filters = BteqProcessor::defaultMyEqBands();
            for (auto& f : flat.filters) f.gainDb = 0;
            safe->newPreset(flat, "My preset");
        } else if (result == 2) {
            auto copy = safe->proc.effectiveParametric();
            safe->newPreset(copy, juce::String(safe->proc.selectedProfile()->target) + " (mine)");
        } else {
            safe->fileChooser = std::make_unique<juce::FileChooser>("Import a parametric EQ (Equalizer APO / AutoEQ format)",
                                                                    juce::File(), "*.txt");
            safe->fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                           [safe](const juce::FileChooser& fc) {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                const auto file = fc.getResult();
                std::vector<std::string> skipped;
                auto parsed = bteq::parseParametricText(file.loadFileAsString().toStdString(), &skipped);
                if (!parsed || parsed->filters.empty()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import",
                                                           "No filters were found in " + file.getFileName() + ".", "OK", safe.getComponent());
                    return;
                }
                if ((int)parsed->filters.size() > FiltersPanel::maxCards) parsed->filters.resize(FiltersPanel::maxCards);
                safe->newPreset(*parsed, file.getFileNameWithoutExtension());
            });
        }
    });
}

void BteqEditor::newPreset(const bteq::ParametricPreset& preset, const juce::String& suggestedName) {
    juce::Component::SafePointer<BteqEditor> safe(this);
    askForName("Name your preset", suggestedName, [safe, preset](juce::String name) {
        if (safe == nullptr) return;
        safe->proc.addCustomPreset(name, preset);
        safe->refreshTargets();
        safe->refreshEditing();
        safe->content.repaint();
    });
}

void BteqEditor::showCustomPresetMenu(int profileIndex) {
    const auto profs = proc.profilesFor(proc.selectedHeadphone());
    if (profileIndex >= (int)profs.size()) return;
    const juce::String name(profs[(size_t)profileIndex]->target);
    juce::PopupMenu menu;
    menu.addSectionHeader(name);
    menu.addItem(1, "Rename...");
    menu.addItem(2, "Delete");
    juce::Component::SafePointer<BteqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&targetList), [safe, profileIndex, name](int result) {
        if (safe == nullptr || result == 0) return;
        if (result == 1) {
            safe->askForName("Rename preset", name, [safe, profileIndex](juce::String newName) {
                if (safe == nullptr) return;
                safe->proc.renameCustomPreset(profileIndex, newName);
                safe->refreshTargets();
            });
        } else {
            safe->proc.deleteCustomPreset(profileIndex);
            safe->refreshTargets();
            safe->refreshEditing();
            safe->content.repaint();
        }
    });
}

void BteqEditor::showSavedMenu() {
    auto saved = loadSavedProfiles();
    const bool canSave = proc.selectedProfile() != nullptr;
    enum { kSaveNew = 1, kUpdate = 2, kLoad = 100, kRename = 1000, kDelete = 2000 };

    juce::PopupMenu menu, renameMenu, deleteMenu;
    if (saved.getNumChildren() == 0) menu.addItem(3, "Nothing saved yet", false);
    for (int i = 0; i < saved.getNumChildren(); ++i) {
        const auto name = saved.getChild(i).getProperty("name").toString();
        menu.addItem(kLoad + i, name, true, name == loadedSavedName);
        renameMenu.addItem(kRename + i, name);
        deleteMenu.addItem(kDelete + i, name);
    }
    menu.addSeparator();
    menu.addItem(kSaveNew, "Save current as new...", canSave);
    if (loadedSavedName.isNotEmpty()) menu.addItem(kUpdate, "Update \"" + loadedSavedName + "\" with current settings", canSave);
    if (saved.getNumChildren() > 0) {
        menu.addSubMenu("Rename", renameMenu);
        menu.addSubMenu("Delete", deleteMenu);
    }

    juce::Component::SafePointer<BteqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&savedButton).withMinimumWidth(savedButton.getWidth()),
                       [safe, saved](int id) mutable {
        if (safe == nullptr || id <= 0) return;
        auto& ed = *safe;
        auto remember = [safe](const juce::String& name) {
            if (safe == nullptr) return;
            safe->loadedSavedName = name;
            safe->loadedSavedSelection = safe->proc.selectedHeadphone() + "|" + juce::String(safe->proc.selectedProfileIndex());
            safe->refreshEditing();
        };
        auto snapshotNamed = [proc = &ed.proc](const juce::String& name) {
            auto t = proc->makeSnapshot().createCopy();
            t.setProperty("name", name, nullptr);
            return t;
        };
        if (id >= kDelete) {
            const auto name = saved.getChild(id - kDelete).getProperty("name").toString();
            saved.removeChild(id - kDelete, nullptr);
            storeSavedProfiles(saved);
            if (name == ed.loadedSavedName) ed.loadedSavedName.clear();
            ed.refreshEditing();
        } else if (id >= kRename) {
            const auto old = saved.getChild(id - kRename).getProperty("name").toString();
            ed.askForName("Rename saved profile", old, [safe, old](juce::String name) {
                if (safe == nullptr) return;
                auto fresh = loadSavedProfiles();
                for (auto child : fresh)
                    if (child.getProperty("name").toString() == old) { child.setProperty("name", name, nullptr); break; }
                storeSavedProfiles(fresh);
                if (safe->loadedSavedName == old) safe->loadedSavedName = name;
                safe->refreshEditing();
            });
        } else if (id >= kLoad) {
            const auto t = saved.getChild(id - kLoad);
            ed.proc.loadSnapshot(t);
            ed.search.setText(ed.proc.selectedHeadphone(), false);
            ed.refreshHeadphones();
            ed.refreshTargets();
            remember(t.getProperty("name").toString());
            ed.content.repaint();
        } else if (id == kUpdate) {
            for (int i = 0; i < saved.getNumChildren(); ++i)
                if (saved.getChild(i).getProperty("name").toString() == ed.loadedSavedName) {
                    saved.removeChild(i, nullptr);
                    saved.addChild(snapshotNamed(ed.loadedSavedName), i, nullptr);
                    break;
                }
            storeSavedProfiles(saved);
            remember(ed.loadedSavedName);
        } else if (id == kSaveNew) {
            const auto* p = ed.proc.selectedProfile();
            const juce::String suggestion = p == nullptr ? juce::String()
                                          : ed.proc.matchedHeadphone().isNotEmpty() ? ed.proc.selectedHeadphone() + " as " + ed.proc.matchedHeadphone()
                                                                                    : ed.proc.selectedHeadphone() + ", " + juce::String(p->target);
            ed.askForName("Save profile", suggestion, [safe, snapshotNamed, remember](juce::String name) {
                if (safe == nullptr) return;
                auto fresh = loadSavedProfiles();
                fresh.appendChild(snapshotNamed(name), nullptr);
                storeSavedProfiles(fresh);
                remember(name);
            });
        }
    });
}

// List models -----------------------------------------------------------------
void BteqEditor::HeadphoneModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) {
    if (row < 0 || row >= (int)ed.results.size()) return;
    const juce::String name(ed.proc.library().headphoneName(ed.results[(size_t)row]));
    const bool on = name == ed.proc.selectedHeadphone();
    if (on) {
        g.setColour(Theme::selected);
        g.fillRoundedRectangle(0.0f, 1.0f, (float)w, (float)h - 2.0f, 6.0f);
    }
    g.setColour(on ? Theme::textBright : Theme::soft);
    g.setFont(ed.theme->bodyFont(15.0f, on));
    g.drawText(name, 10, 0, w - 16, h, juce::Justification::centredLeft, true);
}

void BteqEditor::HeadphoneModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || row >= (int)ed.results.size()) return;
    ed.proc.selectHeadphone(ed.proc.library().headphoneName(ed.results[(size_t)row]));
    ed.refreshTargets();
    ed.headphoneList.repaint();
    ed.content.repaint();
}

void BteqEditor::TargetModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) {
    if (row < 0 || row >= (int)ed.targetRows.size()) return;
    const auto& r = ed.targetRows[(size_t)row];
    if (r.header) {
        g.setColour(Theme::muted);
        g.setFont(ed.theme->bodyFont(13.0f));
        g.drawText(r.text, 2, h / 2 - 2, w - 4, h / 2, juce::Justification::centredLeft, true);
        return;
    }
    if (r.profile == kNewPresetRow) { // a plus instead of a dot
        g.setColour(Theme::soft);
        g.fillRect(10.0f, h * 0.5f - 1.0f, 10.0f, 2.0f);
        g.fillRect(14.0f, h * 0.5f - 5.0f, 2.0f, 10.0f);
        g.setFont(ed.theme->bodyFont(15.0f));
        g.drawText(r.text, 30, 0, w - 34, h, juce::Justification::centredLeft, true);
        return;
    }
    const bool matching = ed.proc.matchedHeadphone().isNotEmpty();
    const bool on = r.profile == kMatchRow ? matching : !matching && r.profile == ed.proc.selectedProfileIndex();
    if (on) {
        g.setColour(Theme::selected);
        g.fillRoundedRectangle(0.0f, 1.0f, (float)w, (float)h - 2.0f, 6.0f);
    }
    const auto accent = Theme::accent(ed.proc.accentIndex);
    const juce::Rectangle<float> dot(10.0f, h * 0.5f - 5.0f, 10.0f, 10.0f);
    g.setColour(on ? accent : juce::Colour(0xff6E7178));
    if (on) g.fillEllipse(dot);
    g.drawEllipse(dot, 2.0f);
    g.setColour(on ? Theme::textBright : Theme::soft);
    g.setFont(ed.theme->bodyFont(15.0f, on));
    g.drawText(r.text, 30, 0, w - 34, h, juce::Justification::centredLeft, true);
}

void BteqEditor::TargetModel::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= (int)ed.targetRows.size() || ed.targetRows[(size_t)row].header) return;
    const int profile = ed.targetRows[(size_t)row].profile;
    if (profile == kMatchRow) {
        ed.showMatchChooser(ed.targetList.getRowPosition(row, false));
        return;
    }
    if (profile == kNewPresetRow) {
        ed.showNewPresetMenu();
        return;
    }
    if (e.mods.isPopupMenu()) { // right-click one of your presets: rename or delete it
        const auto profs = ed.proc.profilesFor(ed.proc.selectedHeadphone());
        if (profile < (int)profs.size() && BteqProcessor::isCustom(profs[(size_t)profile])) ed.showCustomPresetMenu(profile);
        return;
    }
    ed.proc.setMatch({});
    ed.proc.selectProfile(ed.targetRows[(size_t)row].profile);
    ed.shownProfile = ed.proc.selectedProfileIndex();
    ed.targetList.repaint();
    ed.content.repaint();
}
