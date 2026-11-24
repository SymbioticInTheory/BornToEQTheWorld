#include "PluginEditor.h"

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
    static const juce::uint32 colours[numAccents] = { 0xffE46A2E, 0xffC42B2B, 0xffD9D5CD, 0xff8FA3B0 };
    return juce::Colour(colours[juce::jlimit(0, numAccents - 1, index)]);
}

juce::String Theme::accentName(int index) {
    static const char* names[numAccents] = { "Ember", "Blood red", "Bone", "Steel blue" };
    return names[juce::jlimit(0, numAccents - 1, index)];
}

namespace {
// Layout of the 1080 x 680 window (matches the agreed mockup).
const juce::Rectangle<int> kHeadphonePanel { 18, 90, 312, 254 }, kTargetPanel { 18, 358, 312, 304 },
    kTabsPanel { 346, 90, 276, 48 }, kGraphPanel { 346, 150, 716, 290 }, kFiltersPanel { 346, 452, 466, 210 },
    kCorrectionPanel { 826, 452, 236, 210 };

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
            g.setColour(Theme::textBright);
            g.drawEllipse(r.reduced(1.0f), 2.0f);
        }
        g.setColour(c);
        g.fillEllipse(r.reduced(6.0f));
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
ResponseGraph::ResponseGraph(BteqProcessor& p, const Theme& t) : proc(p), theme(t) {
    advancedButton.getProperties().set("kind", "pill");
    advancedButton.setClickingTogglesState(true);
    advancedButton.setTooltip("Show the target, your headphone's measurement and the corrected result");
    advancedButton.onClick = [this] { setAdvanced(advancedButton.getToggleState()); };
    addAndMakeVisible(advancedButton);
    setAdvanced(proc.advancedView);
}

void ResponseGraph::setAdvanced(bool on) {
    proc.advancedView = on;
    advancedButton.setToggleState(on, juce::dontSendNotification);
    if (on && curves == nullptr) curves = std::make_unique<juce::SharedResourcePointer<CurvesHolder>>();
    repaint();
}

void ResponseGraph::resized() { advancedButton.setBounds(getWidth() - 96, 2, 96, 24); }

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
    if (p == nullptr) {
        g.setColour(curveColour);
        g.drawLine(a.getX(), yOf(0), a.getRight(), yOf(0), 3.0f);
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(15.0f));
        g.drawText("Search for your headphones to start", a.withTrimmedTop(a.getHeight() * 0.25f).withHeight(24),
                   juce::Justification::centred);
        return;
    }
    if (proc.advancedView) {
        paintAdvanced(g, *p, k, curveColour);
        return;
    }
    paintEq(g, k, curveColour, 3.0f, true);
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
    if (!nodes) return;

    int num = 1;
    g.setFont(theme.labelFont(12.0f));
    for (const auto& f : preset.filters) {
        if (f.type == bteq::FilterType::LowPass) { ++num; continue; }
        const auto pos = nodePosition(f, k);
        const float x = pos.x, y = pos.y;
        g.setColour(num - 1 == dragIndex ? colour.withAlpha(0.45f) : juce::Colour(0xff0B0C0D));
        g.fillEllipse(x - 9, y - 9, 18, 18);
        g.setColour(colour);
        g.drawEllipse(x - 9, y - 9, 18, 18, 2.0f);
        g.setColour(Theme::textBright);
        g.drawText(juce::String(num++), juce::Rectangle<float>(x - 9, y - 9, 18, 18), juce::Justification::centred);
    }
}

// Target, measured and corrected curves like autoeq.app. The corrected curve
// is the measurement plus the EQ as it is set now (edits and correction amount
// included); both are shifted to sit on the target between 300 Hz and 3 kHz.
void ResponseGraph::paintAdvanced(juce::Graphics& g, const bteq::HeadphoneProfile& p, double k, juce::Colour curveColour) {
    const auto& freqs = proc.library().graphicFrequencies();
    bteq::CurveLibrary::Curves c;
    if (curves != nullptr && (*curves)->loaded) c = (*curves)->curves.find(p.name, p.source, p.rig, p.originalPreset, p.target);

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

    std::vector<double> measured, corrected;
    double offset = 0;
    if (c.measured != nullptr && c.measured->size() == freqs.size()) {
        for (size_t i = 0; i < freqs.size(); ++i) {
            measured.push_back((*c.measured)[i]);
            corrected.push_back((*c.measured)[i] + eq[i]);
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

    const juce::Colour targetColour(0xffD9D5CD), measuredColour(0xff7E8893);
    const bool hasMeasured = !measured.empty(), hasTarget = c.target != nullptr;
    auto visible = [this](Curve cv) { return (proc.hiddenCurves & (1 << cv)) == 0; };

    if (hasTarget && visible(Target)) {
        const std::vector<double> t(c.target->begin(), c.target->end());
        drawCurve(t, 0.0, targetColour.withAlpha(0.8f), 2.0f, true);
    }
    if (hasMeasured && visible(Measured)) drawCurve(measured, offset, measuredColour, 2.0f, false);
    if (visible(Eq)) paintEq(g, k, curveColour.withAlpha(0.55f), 1.8f, !proc.graphicModeActive());
    if (hasMeasured && visible(Corrected)) drawCurve(corrected, offset, curveColour, 3.0f, false);

    // Legend: click a name to show or hide its curve.
    struct Item { Curve id; const char* name; juce::Colour colour; bool available; };
    const Item items[] = { { Target, "Target", targetColour, hasTarget },
                           { Measured, "Measured", measuredColour, hasMeasured },
                           { Corrected, "Corrected", curveColour, hasMeasured },
                           { Eq, "EQ", curveColour.withAlpha(0.55f), true } };
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

int ResponseGraph::nodeAt(juce::Point<float> pos) const {
    if (proc.selectedProfile() == nullptr || proc.graphicModeActive()) return -1;
    if (proc.advancedView && (proc.hiddenCurves & (1 << Eq)) != 0) return -1;
    const auto preset = proc.effectiveParametric();
    const double k = proc.amount();
    int best = -1;
    float bestDist = 12.0f;
    for (int i = 0; i < (int)preset.filters.size(); ++i) {
        const auto& f = preset.filters[(size_t)i];
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

void ResponseGraph::mouseDrag(const juce::MouseEvent& e) {
    if (dragIndex < 0) return;
    const auto preset = proc.effectiveParametric();
    if (dragIndex >= (int)preset.filters.size()) return;
    auto f = preset.filters[(size_t)dragIndex];
    const auto a = plotArea();
    const double x = juce::jlimit(0.0, 1.0, (double)((e.position.x - a.getX()) / a.getWidth()));
    f.fc = roundFreq(20.0 * std::pow(1000.0, x));
    const double k = proc.amount();
    if (k > 0.01) {
        const double db = (a.getCentreY() - e.position.y) / (a.getHeight() * 0.5) * rangeDb();
        f.gainDb = juce::jlimit(-20.0, 20.0, std::round(db / k * 10.0) / 10.0);
    }
    proc.setFilter(dragIndex, f);
    repaint();
}

void ResponseGraph::mouseUp(const juce::MouseEvent&) { dragIndex = -1; repaint(); }

void ResponseGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const int i = nodeAt(e.position);
    if (i < 0 || w.deltaY == 0.0f) return;
    auto f = proc.effectiveParametric().filters[(size_t)i];
    f.q = juce::jlimit(0.1, 20.0, std::round(f.q * (w.deltaY > 0 ? 1.1 : 1.0 / 1.1) * 100.0) / 100.0);
    proc.setFilter(i, f);
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
            l.setMouseCursor(juce::MouseCursor::IBeamCursor);
            l.setTitle("Filter " + juce::String(i + 1) + (fi == Freq ? " frequency" : fi == Gain ? " gain" : " Q"));
            l.onTextChange = [this, i, fi, &l] { commit(i, (Field)fi, l.getText()); };
            addChildComponent(l);
        }
}

juce::Rectangle<int> FiltersPanel::cardBounds(int i) const {
    const int cols = 5, gap = 8, ch = 76;
    const int cw = (getWidth() - gap * (cols - 1)) / cols;
    return { (i % cols) * (cw + gap), (i / cols) * (ch + gap), cw, ch };
}

void FiltersPanel::resized() {
    for (int i = 0; i < maxCards; ++i) {
        auto r = cardBounds(i).reduced(9, 6);
        r.removeFromTop(16);
        values[(size_t)i][Freq].setBounds(r.removeFromTop(19));
        values[(size_t)i][Gain].setBounds(r.removeFromTop(16));
        values[(size_t)i][Q].setBounds(r.removeFromTop(16));
    }
}

void FiltersPanel::refresh() {
    const auto* p = proc.selectedProfile();
    const bool show = p != nullptr && !proc.graphicModeActive();
    const auto preset = proc.effectiveParametric();
    for (int i = 0; i < maxCards; ++i) {
        const bool has = show && i < (int)preset.filters.size();
        for (int fi = 0; fi < numFields; ++fi) {
            auto& l = values[(size_t)i][(size_t)fi];
            l.setVisible(has);
            if (!has || l.isBeingEdited()) continue;
            const auto& f = preset.filters[(size_t)i];
            const bool lowPass = f.type == bteq::FilterType::LowPass;
            const juce::String text = fi == Freq ? formatFreq(f.fc)
                                    : fi == Gain ? (lowPass ? juce::String("12 dB/oct") : formatDb(f.gainDb))
                                                 : formatQ(f.q);
            if (l.getText() != text) l.setText(text, juce::dontSendNotification);
            l.setEditable(!(lowPass && fi == Gain), false, false);
        }
    }
}

void FiltersPanel::commit(int card, Field field, const juce::String& text) {
    auto filters = proc.effectiveParametric().filters;
    if (card >= (int)filters.size()) return;
    auto f = filters[(size_t)card];
    const double v = parseTyped(text, field == Freq);
    if (!std::isnan(v)) {
        if (field == Freq) f.fc = juce::jlimit(10.0, 22000.0, v);
        if (field == Gain) f.gainDb = juce::jlimit(-30.0, 30.0, v);
        if (field == Q) f.q = juce::jlimit(0.05, 30.0, v);
        proc.setFilter(card, f);
    }
    refresh();
    getParentComponent()->repaint();
}

void FiltersPanel::paint(juce::Graphics& g) {
    const auto* p = proc.selectedProfile();
    if (p == nullptr) {
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(14.0f));
        g.drawFittedText("Pick a headphone and a target. The filters will show here.", getLocalBounds().reduced(8),
                         juce::Justification::topLeft, 3);
        return;
    }
    if (proc.graphicModeActive()) {
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

    const auto preset = proc.effectiveParametric();
    const auto& fs = preset.filters;
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
        g.setColour(accent.interpolatedWith(Theme::text, 0.25f));
        g.setFont(theme.labelFont(13.0f));
        g.drawText(juce::String(i + 1), top.removeFromLeft(16), juce::Justification::centredLeft);
        g.setFont(theme.condFont(13.0f));
        g.drawText(type, top, juce::Justification::centredRight);
    }
    auto footer = getLocalBounds().removeFromBottom(20);
    g.setColour(Theme::muted);
    g.setFont(theme.bodyFont(12.0f));
    juce::String hint = "Click a value to type, drag a point on the graph, scroll on a point for Q";
    if (fs.size() > (size_t)maxCards) hint = "+" + juce::String((int)fs.size() - maxCards) + " more filters. " + hint;
    g.drawText(hint, footer.withTrimmedRight(70), juce::Justification::centredLeft, true);
}

// ===========================================================================
// Editor
// ===========================================================================
BteqEditor::BteqEditor(BteqProcessor& p)
    : AudioProcessorEditor(&p), proc(p), lnf(*theme), graph(p, *theme), filters(p, *theme) {
    setLookAndFeel(&lnf);
    background = juce::ImageCache::getFromMemory(BinaryData::forest_png, BinaryData::forest_pngSize);

    bypassButton.setButtonText("Bypass");
    bypassButton.setClickingTogglesState(true);
    bypassButton.getProperties().set("kind", "bypass");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "bypass", bypassButton);

    search.setTextToShowWhenEmpty("Search headphones", Theme::muted);
    search.setFont(theme->bodyFont(16.0f));
    search.setIndents(36, 13);
    search.setTitle("Search headphones");
    search.onTextChange = [this] { refreshHeadphones(); };
    addAndMakeVisible(search);

    for (auto* lb : { &headphoneList, &targetList }) {
        lb->setRowHeight(34);
        lb->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(*lb);
    }
    headphoneList.setTitle("Matching headphones");
    targetList.setTitle("Targets");

    for (auto* tab : { &parametricTab, &graphicTab }) {
        tab->getProperties().set("kind", "tab");
        tab->setRadioGroupId(1);
        addAndMakeVisible(*tab);
    }
    parametricTab.onClick = [this] { setMode(false); };
    graphicTab.onClick = [this] { setMode(true); };

    addAndMakeVisible(graph);
    addAndMakeVisible(filters);

    correction.setSliderStyle(juce::Slider::LinearHorizontal);
    correction.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    correction.setTitle("Correction amount");
    addAndMakeVisible(correction);
    correctionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, "amount", correction);

    for (int i = 0; i < Theme::numAccents; ++i) {
        auto& s = swatches[(size_t)i];
        s.getProperties().set("kind", "swatch");
        s.getProperties().set("swatch", (juce::int64)Theme::accent(i).getARGB());
        s.setTitle(Theme::accentName(i));
        s.setTooltip(Theme::accentName(i));
        s.setRadioGroupId(2);
        s.onClick = [this, i] { setAccent(i); };
        addAndMakeVisible(s);
    }

    preampValue.setFont(theme->bodyFont(13.0f));
    preampValue.setColour(juce::Label::textColourId, Theme::text);
    preampValue.setJustificationType(juce::Justification::centredRight);
    preampValue.setBorderSize({ 0, 0, 0, 0 });
    preampValue.setEditable(true, false, false);
    preampValue.setMouseCursor(juce::MouseCursor::IBeamCursor);
    preampValue.setTitle("Preamp");
    preampValue.setTooltip("Click to type a preamp level");
    preampValue.onTextChange = [this] {
        const double v = parseTyped(preampValue.getText(), false);
        if (!std::isnan(v)) proc.setPreamp(v);
        refreshEditing();
    };
    addAndMakeVisible(preampValue);

    resetButton.getProperties().set("kind", "small");
    resetButton.setTooltip("Go back to the published settings for this target");
    resetButton.onClick = [this] { proc.resetEdits(); refreshEditing(); repaint(); };
    addChildComponent(resetButton);

    savedButton.getProperties().set("kind", "dropdown");
    savedButton.setTitle("Saved profiles");
    savedButton.onClick = [this] { showSavedMenu(); };
    addAndMakeVisible(savedButton);

    setAccent(proc.accentIndex);
    setSize(1080, 680);

    // Start with the selected headphone (if any) shown in the search box.
    if (proc.selectedHeadphone().isNotEmpty()) search.setText(proc.selectedHeadphone(), false);
    refreshHeadphones();
    refreshTargets();
    refreshEditing();
    startTimerHz(15);
}

BteqEditor::~BteqEditor() { setLookAndFeel(nullptr); }

void BteqEditor::setAccent(int index) {
    proc.accentIndex = juce::jlimit(0, Theme::numAccents - 1, index);
    lnf.accentColour = graph.accent = filters.accent = Theme::accent(proc.accentIndex);
    swatches[(size_t)proc.accentIndex].setToggleState(true, juce::dontSendNotification);
    repaint();
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
    if (const auto idx = proc.library().headphoneIndex(shownHeadphone.toStdString())) {
        const auto profs = proc.library().profilesFor(*idx);
        juce::String lastHeading;
        for (size_t i = 0; i < profs.size(); ++i) {
            const auto* pr = profs[i];
            const juce::String heading = pr->originalPreset ? juce::String(pr->source) + "'s own presets"
                                                            : juce::String(pr->sourceLabel()) + " via AutoEQ";
            if (heading != lastHeading) {
                targetRows.push_back({ true, heading, -1 });
                lastHeading = heading;
            }
            targetRows.push_back({ false, juce::String(pr->target), (int)i });
        }
    }
    targetList.updateContent();
    targetList.repaint();
}

void BteqEditor::timerCallback() {
    if (proc.selectedHeadphone() != shownHeadphone || proc.selectedProfileIndex() != shownProfile) refreshTargets();

    const auto* p = proc.selectedProfile();
    const bool hasP = p != nullptr && p->hasParametric, hasG = proc.graphicAvailable();
    parametricTab.setEnabled(hasP || p == nullptr);
    graphicTab.setEnabled(hasG);
    const bool graphic = proc.graphicModeActive();
    parametricTab.setToggleState(!graphic, juce::dontSendNotification);
    graphicTab.setToggleState(graphic, juce::dontSendNotification);
    bypassButton.setButtonText(bypassButton.getToggleState() ? "Bypassed" : "Bypass");

    refreshEditing();
    graph.repaint();
    filters.repaint();
    repaint(kCorrectionPanel.withHeight(40));
    repaint(kCorrectionPanel.withTrimmedTop(100).withHeight(30));
    repaint(juce::Rectangle<int>(700, 0, 250, 72));
    repaint(kTabsPanel.withX(kTabsPanel.getRight()).withWidth(kGraphPanel.getRight() - kTabsPanel.getRight()));
}

void BteqEditor::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff0B0C0D));
    if (background.isValid())
        g.drawImageWithin(background, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::fillDestination);
    juce::ColourGradient fog(juce::Colour(0x400A0B0C), bounds.getWidth() * 0.55f, bounds.getHeight() * 0.3f,
                             juce::Colour(0xe608090A), 0.0f, bounds.getHeight() * 1.05f, true);
    fog.addColour(0.55, juce::Colour(0x9e0A0B0C));
    g.setGradientFill(fog);
    g.fillRect(bounds);

    // Header.
    g.setColour(juce::Colour(0x14ffffff));
    g.drawHorizontalLine(72, 0.0f, bounds.getWidth());
    g.setColour(juce::Colour(0xffECE8E0));
    const auto titleFont = theme->titleFont(42.0f);
    g.setFont(titleFont);
    g.drawText("Born To EQ The World", 26, 12, 460, 48, juce::Justification::centredLeft);
    const int titleW = (int)juce::GlyphArrangement::getStringWidth(titleFont, "Born To EQ The World");
    g.setColour(Theme::muted);
    g.setFont(theme->labelFont(15.0f));
    g.drawText("HEADPHONE CORRECTION", 26 + titleW + 18, 27, 260, 24, juce::Justification::centredLeft);

    const double pre = proc.currentPreampDb() * (proc.params.getRawParameterValue("bypass")->load() > 0.5f ? 0.0 : 1.0);
    g.setFont(theme->labelFont(16.0f));
    g.setColour(Theme::muted);
    g.drawText("PREAMP", 760, 20, 80, 32, juce::Justification::centredRight);
    g.setColour(Theme::text);
    g.drawText(formatDb(pre), 846, 20, 82, 32, juce::Justification::centredLeft);

    // Panels.
    for (auto r : { kHeadphonePanel, kTargetPanel, kTabsPanel, kGraphPanel, kFiltersPanel, kCorrectionPanel }) drawPanel(g, r);

    g.setFont(theme->labelFont(15.0f));
    g.setColour(Theme::muted);
    g.drawText("HEADPHONE", kHeadphonePanel.getX() + 14, kHeadphonePanel.getY() + 12, 200, 18, juce::Justification::centredLeft);
    g.drawText("TARGET", kTargetPanel.getX() + 14, kTargetPanel.getY() + 12, 200, 18, juce::Justification::centredLeft);
    g.drawText("CORRECTION", kCorrectionPanel.getX() + 14, kCorrectionPanel.getY() + 16, 120, 18, juce::Justification::centredLeft);
    g.drawText("COLOUR", kCorrectionPanel.getX() + 14, kCorrectionPanel.getBottom() - 46, 100, 32, juce::Justification::centredLeft);

    // Search icon.
    {
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
    if (p != nullptr) {
        if (proc.graphicModeActive()) note = "Wavelet-style 127-point curve";
        else if (p->originalPreset && !proc.selectionEdited()) note = juce::String((int)p->parametric.filters.size()) + " filters, exactly as " + juce::String(p->source) + " published them";
        else if (p->originalPreset) note = juce::String((int)p->parametric.filters.size()) + " filters from " + juce::String(p->source);
        else note = juce::String((int)p->parametric.filters.size()) + " filters from AutoEQ";
        if (!proc.graphicAvailable() && p->hasParametric) note << " (no graphic version)";
        if (proc.selectionEdited()) note << ", edited by you";
    }
    g.drawText(note, kTabsPanel.getRight() + 14, kTabsPanel.getY(), kGraphPanel.getRight() - kTabsPanel.getRight() - 14,
               kTabsPanel.getHeight(), juce::Justification::centredRight);

    // Correction readouts.
    const int pct = (int)std::round(proc.params.getRawParameterValue("amount")->load());
    g.setColour(Theme::textBright);
    g.setFont(theme->labelFont(36.0f).withExtraKerningFactor(0.0f));
    g.drawText(juce::String(pct) + "%", kCorrectionPanel.getRight() - 114, kCorrectionPanel.getY() + 8, 100, 38,
               juce::Justification::centredRight);
    g.setFont(theme->bodyFont(12.0f));
    g.setColour(Theme::muted);
    const auto sl = correction.getBounds();
    g.drawText("Off", sl.getX(), sl.getBottom() + 2, 40, 14, juce::Justification::centredLeft);
    g.drawText("50%", sl.getCentreX() - 20, sl.getBottom() + 2, 40, 14, juce::Justification::centred);
    g.drawText("Full", sl.getRight() - 40, sl.getBottom() + 2, 40, 14, juce::Justification::centredRight);
    g.setFont(theme->bodyFont(13.0f));
    g.setColour(juce::Colour(0xffB2AFA9));
    g.drawText(proc.selectionEdited() ? "Preamp at 100% (edited)" : "Preamp at 100%", kCorrectionPanel.getX() + 14,
               kCorrectionPanel.getY() + 104, 150, 20, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0x14ffffff));
    g.drawHorizontalLine(kCorrectionPanel.getBottom() - 54, (float)kCorrectionPanel.getX() + 14, (float)kCorrectionPanel.getRight() - 14);

    if (!proc.libraryLoaded()) {
        g.setColour(juce::Colour(0xffFFB5A6));
        g.setFont(theme->bodyFont(14.0f));
        g.drawText("The headphone library could not be loaded.", kTargetPanel.reduced(14), juce::Justification::centred);
    }
}

void BteqEditor::resized() {
    bypassButton.setBounds(getWidth() - 26 - 110, 14, 110, 44);
    search.setBounds(kHeadphonePanel.getX() + 14, kHeadphonePanel.getY() + 38, kHeadphonePanel.getWidth() - 28, 44);
    headphoneList.setBounds(kHeadphonePanel.getX() + 10, kHeadphonePanel.getY() + 90, kHeadphonePanel.getWidth() - 20,
                            kHeadphonePanel.getHeight() - 100);
    targetList.setBounds(kTargetPanel.getX() + 10, kTargetPanel.getY() + 38, kTargetPanel.getWidth() - 20,
                         kTargetPanel.getHeight() - 48);
    parametricTab.setBounds(kTabsPanel.getX() + 4, kTabsPanel.getY() + 4, 132, 40);
    graphicTab.setBounds(kTabsPanel.getX() + 140, kTabsPanel.getY() + 4, 132, 40);
    graph.setBounds(kGraphPanel.reduced(12, 8));
    filters.setBounds(kFiltersPanel.reduced(12));
    correction.setBounds(kCorrectionPanel.getX() + 12, kCorrectionPanel.getY() + 58, kCorrectionPanel.getWidth() - 24, 26);
    preampValue.setBounds(kCorrectionPanel.getRight() - 94, kCorrectionPanel.getY() + 104, 80, 20);
    resetButton.setBounds(kFiltersPanel.getRight() - 12 - 64, kFiltersPanel.getBottom() - 12 - 20, 64, 22);
    savedButton.setBounds(462, 18, 210, 36);
    for (int i = 0; i < Theme::numAccents; ++i)
        swatches[(size_t)i].setBounds(kCorrectionPanel.getRight() - 14 - (Theme::numAccents - i) * 36 + 2,
                                      kCorrectionPanel.getBottom() - 46, 34, 34);
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
    resetButton.setVisible(proc.selectionEdited());

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
            ed.repaint();
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
            const juce::String suggestion = p ? ed.proc.selectedHeadphone() + ", " + juce::String(p->target) : juce::String();
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
    ed.repaint();
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
    const bool on = r.profile == ed.proc.selectedProfileIndex();
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

void BteqEditor::TargetModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || row >= (int)ed.targetRows.size() || ed.targetRows[(size_t)row].header) return;
    ed.proc.selectProfile(ed.targetRows[(size_t)row].profile);
    ed.shownProfile = ed.proc.selectedProfileIndex();
    ed.targetList.repaint();
    ed.repaint();
}
