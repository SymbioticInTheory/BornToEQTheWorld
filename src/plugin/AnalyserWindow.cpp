#include "AnalyserWindow.h"

using bteq::spectrum::Settings;

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> a, const bteq::spectrum::Analyser& an, juce::Colour accent,
                   float scale) {
    if (!an.hasData() || a.getWidth() < 4) return;
    const auto& s = an.settings;
    const int points = juce::jmax(64, (int)(a.getWidth() / 2));
    auto path = [&](const std::vector<double>& db, bool closed) {
        juce::Path p;
        for (int j = 0; j < points; ++j) {
            const float x = a.getX() + a.getWidth() * (float)j / (float)(points - 1);
            const float y = juce::jlimit(a.getY() - 2, a.getBottom() + 2,
                                         a.getY() + (float)((s.hi - db[(size_t)j]) / (s.hi - s.lo)) * a.getHeight());
            if (j == 0) {
                if (closed) { p.startNewSubPath(x, a.getBottom() + 2); p.lineTo(x, y); }
                else p.startNewSubPath(x, y);
            } else {
                p.lineTo(x, y);
            }
        }
        if (closed) { p.lineTo(a.getRight(), a.getBottom() + 2); p.closeSubPath(); }
        return p;
    };
    if (s.view != 1) { // live
        const auto live = an.curve(false, points);
        g.setColour(s.view == 2 ? accent : juce::Colour(0x70D2D0CA));
        g.strokePath(path(live, false), juce::PathStrokeType(1.3f * scale));
    }
    if (s.view != 2) { // average
        const auto avg = an.curve(true, points);
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.30f), 0, a.getY(), accent.withAlpha(0.03f), 0, a.getBottom(), false));
        g.fillPath(path(avg, true));
        g.setColour(accent.withAlpha(0.9f));
        g.strokePath(path(avg, false), juce::PathStrokeType(1.8f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void connectAnalyser(bteq::spectrum::Analyser& an, const BteqProcessor& proc) {
    struct Cache { std::vector<double> freqs, db; int mode = 0; juce::uint32 when = 0; };
    auto cache = std::make_shared<Cache>();
    an.binOffsetDb = [&an, &proc, cache](const std::vector<double>& freqs) -> std::vector<double> {
        // What to do to the sound the tap has, to show the chosen point:
        //   plugin (tap at the input):  before any EQ: nothing; with only My EQ: add My EQ.
        //   standalone with Equalizer APO (what Windows plays, after all of BEQ's EQ):
        //     before any EQ: take all of it out; with only My EQ: take the correction out.
        const int where = an.settings.where;
        int mode = 0; // 0 nothing, 1 add My EQ, 2 remove everything, 3 remove the correction
        if (proc.analyserUndoesEq()) mode = where == 1 ? 0 : where == 0 ? 2 : 3;
        else mode = where == 2 ? 1 : 0; // no EQ in the sound yet (also Windows' sound while APO is off)
        if (mode == 0) return {};
        const auto now = juce::Time::getMillisecondCounter();
        if (cache->freqs != freqs || cache->mode != mode || now - cache->when > 250) { // follows the EQ as you change it
            cache->freqs = freqs;
            cache->mode = mode;
            cache->db = proc.appliedResponseDb(freqs, mode != 1, mode != 3);
            if (mode != 1)
                for (auto& d : cache->db) d = -d; // take it back out
            cache->when = now;
        }
        return cache->db;
    };
}

void feedAnalyser(bteq::spectrum::Analyser& an, const BteqProcessor& proc, int& seenGeneration) {
    const int gen = proc.spectrumTap.generation.load();
    if (gen != seenGeneration) {
        seenGeneration = gen;
        an.reset();
    }
    an.process(proc.spectrumTap, proc.analyserRate());
}

namespace {
juce::String freqText(double f) {
    return f >= 1000 ? juce::String(f / 1000.0, f >= 10000 ? 1 : 2) + " kHz" : juce::String((int)std::lround(f)) + " Hz";
}
juce::String smoothText(double oct) {
    const auto& st = bteq::spectrum::smoothSteps()[(size_t)bteq::spectrum::smoothIndex(oct)];
    return st.oct <= 0 ? juce::String("Off") : juce::String(st.name) + " oct";
}
const juce::StringArray kWhere { "Before any EQ", "After correction", "With My EQ only" }, kView { "Live + average", "Average", "Live" },
    kWindow { "Hann", "Blackman-Harris" }, kBlocks { "2048", "4096", "8192", "16384", "32768" };
} // namespace

// ---------------------------------------------------------------------------
// The panel in the pop-out window
// ---------------------------------------------------------------------------
AnalyserPanel::AnalyserPanel(BteqProcessor& p, const Theme& t, juce::LookAndFeel& lnf) : proc(p), theme(t) {
    setLookAndFeel(&lnf);
    proc.spectrumTap.addViewer();
    connectAnalyser(analyser, proc);
    for (auto* b : { &whereButton, &viewButton, &windowButton, &blockButton, &resetButton, &defaultsButton }) {
        b->getProperties().set("kind", "pill");
        addAndMakeVisible(b);
    }
    whereButton.setTooltip("Before any EQ: the sound as it comes in. With My EQ only: that plus My EQ, without the headphone "
                           "correction (see My EQ's effect as you tune). After correction: what you hear, everything applied");
    whereButton.onClick = [this] {
        showChoices(whereButton, kWhere, analyser.settings.where, [this](int i) { change([i](Settings& s) { s.where = i; }); });
    };
    viewButton.onClick = [this] {
        showChoices(viewButton, kView, analyser.settings.view, [this](int i) { change([i](Settings& s) { s.view = i; }); });
    };
    windowButton.setTooltip("FFT window. Hann is the usual choice; Blackman-Harris lets less of a loud tone spread into its neighbours");
    windowButton.onClick = [this] {
        showChoices(windowButton, kWindow, analyser.settings.window, [this](int i) { change([i](Settings& s) { s.window = i; }); });
    };
    blockButton.setTooltip("FFT block size. Bigger: finer detail in the bass, slower to react");
    blockButton.onClick = [this] {
        showChoices(blockButton, kBlocks, kBlocks.indexOf(juce::String(analyser.settings.block)),
                    [this](int i) { change([i](Settings& s) { s.block = kBlocks[i].getIntValue(); }); });
    };
    resetButton.setTooltip("Start the average again (or double-click the graph)");
    resetButton.onClick = [this] { analyser.reset(); repaint(); };
    defaultsButton.setTooltip("Back to the mastering defaults: 6 s average, 8192, 88%, 4.5 dB/oct, 2/3 oct, -69 to -12 dB");
    defaultsButton.onClick = [this] { change([](Settings& s) { const int open = s.open; s = Settings {}; s.open = open; }); };
    for (auto* b : { &myEqButton, &foldButton }) {
        b->getProperties().set("kind", "pill");
        addAndMakeVisible(b);
    }
    myEqButton.setClickingTogglesState(true);
    myEqButton.setTooltip("My EQ, drawn over the spectrum: drag its points to tune it (scroll on one for Q, double-click "
                          "to switch it off). This turns My EQ on or off.");
    myEqButton.onClick = [this] { proc.setMyEqOn(myEqButton.getToggleState()); repaint(); };
    foldButton.setTooltip("Show or hide the analyser settings");
    foldButton.onClick = [this] { change([](Settings& s) { s.open = s.open ? 0 : 1; }); resized(); };

    // Knobs: name, range, and how the value reads.
    const Settings d;
    struct Spec { const char* name; double lo, hi, step, mid, def; std::function<juce::String(double)> text; const char* tip; };
    const Spec specs[] = {
        { "Average", 0.5, 31, 0.1, 6, d.avgMs / 1000.0,
          [](double v) { return v >= 30.95 ? juce::String("Until reset") : juce::String(v, 1) + " s"; },
          "How long the average looks back. All the way up: until you reset it" },
        { "Overlap", 0, 95, 1, 50, d.overlap * 100, [](double v) { return juce::String((int)v) + "%"; },
          "How much each FFT block overlaps the last: higher updates more often" },
        { "Slope", -3, 9, 0.5, 3, d.slope, [](double v) { return juce::String(v, 1) + " dB/oct"; },
          "Tilts the display around 1 kHz, so a balanced mix looks flat (4.5 is common for mastering)" },
        { "Smoothing", 0, (double)bteq::spectrum::smoothSteps().size() - 1, 1, 7, (double)bteq::spectrum::smoothIndex(d.smooth),
          [](double v) { return smoothText(bteq::spectrum::smoothSteps()[(size_t)juce::jlimit(0, 14, (int)std::lround(v))].oct); },
          "Averages over this many octaves, so the shape is easier to read: Off, 1/48 up to 2.5 octaves, the same steps as SPAN" },
        { "Range low", -150, 10, 1, -70, d.lo, [](double v) { return juce::String((int)v) + " dB"; }, "Level at the bottom of the graph" },
        { "Range high", -140, 20, 1, -20, d.hi, [](double v) { return juce::String((int)v) + " dB"; }, "Level at the top of the graph" },
        { "Freq low", 10, 1000, 1, 60, d.fLo, [](double v) { return freqText(v); }, "Lowest frequency shown" },
        { "Freq high", 1000, 24000, 100, 8000, d.fHi, [](double v) { return freqText(v); }, "Highest frequency shown" },
    };
    for (int i = 0; i < (int)knobs.size(); ++i) {
        auto& k = knobs[(size_t)i];
        const auto& sp = specs[i];
        k.name = sp.name;
        k.text = sp.text;
        k.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        k.knob.setRange(sp.lo, sp.hi, sp.step);
        k.knob.setSkewFactorFromMidPoint(sp.mid);
        k.knob.setMouseDragSensitivity(220);
        k.knob.setTooltip(juce::String(sp.tip) + ". Shift-drag for fine steps, double-click for the default.");
        k.knob.setVelocityBasedMode(false);
        const double def = sp.def;
        k.knob.onDoubleClick = [&k, def] { k.knob.setValue(def, juce::sendNotificationSync); };
        k.knob.onValueChange = [this, i] {
            const double v = knobs[(size_t)i].knob.getValue();
            change([i, v](Settings& s) {
                switch (i) {
                    case kAvg: s.avgMs = v >= 30.95 ? 0.0 : v * 1000.0; break;
                    case kOverlap: s.overlap = v / 100.0; break;
                    case kSlope: s.slope = v; break;
                    case kSmooth: s.smooth = bteq::spectrum::smoothSteps()[(size_t)juce::jlimit(0, (int)bteq::spectrum::smoothSteps().size() - 1, (int)std::lround(v))].oct; break;
                    case kLo: s.lo = juce::jmin(v, s.hi - 6); break;
                    case kHi: s.hi = juce::jmax(v, s.lo + 6); break;
                    case kFLo: s.fLo = juce::jmin(v, s.fHi / 2); break;
                    case kFHi: s.fHi = juce::jmax(v, s.fLo * 2); break;
                    default: break;
                }
            });
        };
        addAndMakeVisible(k.knob);
    }
    knobs[kSmooth].knob.onRightClick = [this] { showSmoothMenu(); };
    followSettings();
    startTimerHz(30);
}

AnalyserPanel::~AnalyserPanel() {
    if (unsaved) BteqProcessor::setAnalyserSettings(analyser.settings);
    proc.spectrumTap.removeViewer();
    setLookAndFeel(nullptr);
}

void AnalyserPanel::change(const std::function<void(Settings&)>& edit) {
    auto s = BteqProcessor::analyserSettings();
    edit(s);
    BteqProcessor::setAnalyserSettings(s, false); // written to settings.xml once you stop turning
    unsaved = true;
    lastChange = juce::Time::getMillisecondCounter();
    followSettings();
}

// Picks up settings changed here or in another BEQ window.
void AnalyserPanel::followSettings() {
    if (seenVersion == BteqProcessor::analyserVersion()) return;
    seenVersion = BteqProcessor::analyserVersion();
    const auto& s = BteqProcessor::analyserSettings();
    const bool restart = s.block != analyser.settings.block || s.window != analyser.settings.window || s.where != analyser.settings.where;
    analyser.settings = s;
    if (restart) analyser.reset();
    proc.spectrumTap.where = bteq::spectrum::Tap::tapPoint(s.where);
    refreshControls();
    repaint();
}

void AnalyserPanel::refreshControls() {
    const auto& s = analyser.settings;
    foldButton.setButtonText(juce::String::fromUTF8(s.open ? "Settings \xe2\x96\xbe" : "Settings \xe2\x96\xb8"));
    myEqButton.setToggleState(proc.myEq().on, juce::dontSendNotification);
    const bool open = s.open != 0;
    for (juce::Component* c : { (juce::Component*)&whereButton, (juce::Component*)&viewButton, (juce::Component*)&windowButton,
                                (juce::Component*)&blockButton, (juce::Component*)&resetButton, (juce::Component*)&defaultsButton })
        c->setVisible(open);
    for (auto& k : knobs) k.knob.setVisible(open);
    if (open != !controlsArea.isEmpty()) resized();
    whereButton.setButtonText(kWhere[s.where]);
    viewButton.setButtonText(kView[s.view]);
    windowButton.setButtonText(kWindow[s.window]);
    blockButton.setButtonText("Block " + juce::String(s.block));
    const double vals[] = { s.avgMs <= 0 ? 31.0 : s.avgMs / 1000.0, s.overlap * 100, s.slope, (double)bteq::spectrum::smoothIndex(s.smooth), s.lo, s.hi, s.fLo, s.fHi };
    for (size_t i = 0; i < knobs.size(); ++i)
        if (!knobs[i].knob.isMouseButtonDown()) knobs[i].knob.setValue(vals[i], juce::dontSendNotification);
}

void AnalyserPanel::showChoices(juce::TextButton& b, const juce::StringArray& names, int current, std::function<void(int)> pick) {
    juce::PopupMenu m;
    for (int i = 0; i < names.size(); ++i) m.addItem(i + 1, names[i], true, i == current);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&b), [pick](int id) {
        if (id > 0) pick(id - 1);
    });
}

// Smoothing as a list, like SPAN's: right-click its knob or click its value.
void AnalyserPanel::showSmoothMenu() {
    juce::PopupMenu m;
    const auto& st = bteq::spectrum::smoothSteps();
    const int current = bteq::spectrum::smoothIndex(analyser.settings.smooth);
    for (int i = 0; i < (int)st.size(); ++i) m.addItem(i + 1, i == 0 ? juce::String("Off") : juce::String(st[(size_t)i].name) + " oct", true, i == current);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&knobs[kSmooth].knob), [this](int id) {
        if (id > 0) change([id](Settings& s) { s.smooth = bteq::spectrum::smoothSteps()[(size_t)(id - 1)].oct; });
    });
}

juce::Rectangle<int> AnalyserPanel::knobValueArea(int i) const {
    const auto kb = knobs[(size_t)i].knob.getBounds();
    return { kb.getCentreX() - 45, kb.getBottom() + 2, 90, 16 };
}

void AnalyserPanel::timerCallback() {
    followSettings();
    if (unsaved && juce::Time::getMillisecondCounter() - lastChange > 600) {
        BteqProcessor::setAnalyserSettings(analyser.settings);
        unsaved = false;
    }
    if (getPeer() != nullptr && !isShowing()) return; // minimised or hidden
    feedAnalyser(analyser, proc, seenGeneration);
    repaint(plotArea().toNearestInt().expanded(40, 30));
}

juce::Rectangle<float> AnalyserPanel::plotArea() const {
    return getLocalBounds().toFloat().withTrimmedTop(40).withTrimmedBottom((float)controlsArea.getHeight() + 30).reduced(14, 0)
        .withTrimmedLeft(30).withTrimmedRight(30 + 22);
}

// ---------------------------------------------------------------------------
// The dB range bar on the right, like SPAN's: drag its ends to zoom in or out,
// the middle to move it, scroll on it to zoom, double-click for the default.
// ---------------------------------------------------------------------------
juce::Rectangle<float> AnalyserPanel::rangeBar() const {
    const auto a = plotArea();
    return { a.getRight() + 36, a.getY(), 10, a.getHeight() };
}
float AnalyserPanel::rangeY(double db) const {
    const auto b = rangeBar();
    return b.getY() + (float)((kRangeTop - db) / (kRangeTop - kRangeBottom)) * b.getHeight();
}
void AnalyserPanel::setRange(double lo, double hi) {
    const double span = juce::jlimit(6.0, kRangeTop - kRangeBottom, hi - lo);
    hi = juce::jlimit(kRangeBottom + span, kRangeTop, hi);
    lo = hi - span;
    lo = std::round(lo * 2) / 2;
    hi = std::round(hi * 2) / 2;
    change([lo, hi](Settings& s) { s.lo = lo; s.hi = hi; });
}
void AnalyserPanel::paintRangeBar(juce::Graphics& g) {
    const auto b = rangeBar();
    const auto& s = analyser.settings;
    g.setColour(juce::Colour(0x14ffffff));
    g.fillRoundedRectangle(b, 5.0f);
    const auto thumb = juce::Rectangle<float>(b.getX(), rangeY(s.hi), b.getWidth(), juce::jmax(8.0f, rangeY(s.lo) - rangeY(s.hi)));
    const bool active = rangeDrag != 0 || overRangeBar;
    g.setColour(accent.withAlpha(active ? 0.75f : 0.5f));
    g.fillRoundedRectangle(thumb, 5.0f);
    g.setColour(Theme::textBright.withAlpha(active ? 0.9f : 0.6f)); // grips at the ends
    for (float y : { thumb.getY() + 4.0f, thumb.getBottom() - 4.0f })
        g.drawHorizontalLine((int)y, thumb.getX() + 2.5f, thumb.getRight() - 2.5f);
}


void AnalyserPanel::resized() {
    foldButton.setBounds(getWidth() - 14 - 104, 6, 104, 24);
    myEqButton.setBounds(foldButton.getX() - 8 - 76, 6, 76, 24);
    auto r = getLocalBounds().reduced(14, 10);
    controlsArea = analyser.settings.open ? r.removeFromBottom(108) : juce::Rectangle<int>();
    if (controlsArea.isEmpty()) return;
    auto c = controlsArea.reduced(0, 6);
    auto buttons = c.removeFromLeft(320);
    const int bh = 26, gap = 8;
    auto row1 = buttons.removeFromTop(bh + 10).withTrimmedTop(10), row2 = buttons.removeFromTop(bh + gap).withTrimmedTop(gap);
    whereButton.setBounds(row1.removeFromLeft(156));
    viewButton.setBounds(row1.withTrimmedLeft(8));
    windowButton.setBounds(row2.removeFromLeft(156));
    blockButton.setBounds(row2.withTrimmedLeft(8));
    auto row3 = buttons.removeFromTop(bh + gap).withTrimmedTop(gap);
    resetButton.setBounds(row3.removeFromLeft(74));
    defaultsButton.setBounds(row3.withTrimmedLeft(8).withWidth(90));
    c.removeFromLeft(10);
    const int kw = juce::jmin(84, c.getWidth() / (int)knobs.size());
    for (auto& k : knobs) {
        auto cell = c.removeFromLeft(kw);
        k.knob.setBounds(cell.withTrimmedTop(16).withHeight(52).withSizeKeepingCentre(52, 52));
    }
}

void AnalyserPanel::paint(juce::Graphics& g) {
    g.fillAll(Theme::fog.interpolatedWith(juce::Colours::black, 0.3f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x12ffffff), 0, 0, juce::Colour(0x00ffffff), 0, (float)getHeight() * 0.6f, false));
    g.fillRect(getLocalBounds());

    g.setFont(theme.labelFont(13.0f));
    g.setColour(Theme::muted);
    g.drawText("SPECTRUM", 14, 8, 100, 20, juce::Justification::centredLeft);
    g.setFont(theme.bodyFont(14.0f));
    g.setColour(Theme::soft);
    const int where = analyser.settings.where;
    juce::String source = where == 0 ? "Input, before any EQ" : where == 2 ? "Input with My EQ only, no correction" : "Output, after the correction";
    if (proc.analyserHearsSystem())
        source = !proc.analyserUndoesEq() ? juce::String("What Windows is playing")
                 : where == 0 ? juce::String("What Windows plays, before BEQ's EQ")
                 : where == 2 ? juce::String("What Windows plays, with only My EQ")
                              : juce::String("What Windows plays, after BEQ's EQ");
    g.drawText(source, 104, 8, 320, 20, juce::Justification::centredLeft);

    const auto a = plotArea();
    const auto& s = analyser.settings;
    g.setColour(Theme::panel);
    g.fillRoundedRectangle(a.expanded(36, 8).withTrimmedRight(30).withTrimmedBottom(-20), 10.0f);
    // Grid.
    const double span = std::log2(s.fHi / s.fLo);
    auto xOf = [&](double f) { return a.getX() + (float)(std::log2(f / s.fLo) / span) * a.getWidth(); };
    auto yOf = [&](double db) { return a.getY() + (float)((s.hi - db) / (s.hi - s.lo)) * a.getHeight(); };
    g.setFont(theme.condFont(13.0f));
    for (double f : { 20.0, 30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0, 20000.0 }) {
        if (f <= s.fLo || f >= s.fHi) continue;
        const bool major = juce::String(f).startsWith("1") || juce::String(f).startsWith("2") || juce::String(f).startsWith("5");
        g.setColour(major ? juce::Colour(0x17ffffff) : juce::Colour(0x0affffff));
        g.drawVerticalLine((int)xOf(f), a.getY(), a.getBottom());
        if (major) {
            g.setColour(Theme::muted);
            g.drawText(f >= 1000 ? juce::String((int)(f / 1000)) + "k" : juce::String((int)f), juce::Rectangle<float>(xOf(f) - 20, a.getBottom() + 4, 40, 16),
                       juce::Justification::centred);
        }
    }
    const double step = s.hi - s.lo > 60 ? 12 : 6;
    for (double db = std::ceil(s.lo / step) * step; db <= s.hi + 0.01; db += step) {
        g.setColour(juce::Colour(0x12ffffff));
        g.drawHorizontalLine((int)yOf(db), a.getX(), a.getRight());
        g.setColour(Theme::muted);
        g.drawText(juce::String((int)db), juce::Rectangle<float>(a.getX() - 36, yOf(db) - 8, 30, 16), juce::Justification::centredRight);
    }
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(a.toNearestInt());
        paintSpectrum(g, a, analyser, accent);
    }
    paintMyEq(g);
    paintRangeBar(g);
    if (!analyser.hasData()) {
        g.setColour(Theme::soft);
        g.setFont(theme.bodyFont(15.0f));
        g.drawText("Waiting for sound...", a, juce::Justification::centred);
    }
    // Where the mouse is.
    if (mouseX >= a.getX() && mouseX <= a.getRight() && analyser.hasData()) {
        g.setColour(juce::Colour(0x40ffffff));
        g.drawVerticalLine((int)mouseX, a.getY(), a.getBottom());
        const double f = s.fLo * std::pow(2.0, span * (mouseX - a.getX()) / a.getWidth());
        const int points = juce::jmax(64, (int)(a.getWidth() / 2));
        const int j = juce::jlimit(0, points - 1, (int)std::lround((mouseX - a.getX()) / a.getWidth() * (points - 1)));
        const auto c = analyser.curve(s.view != 2, points);
        g.setColour(Theme::text);
        g.setFont(theme.condFont(15.0f));
        g.drawText(freqText(f) + "   " + juce::String(c[(size_t)j], 1) + " dB", myEqButton.getX() - 270, 8, 256, 20,
                   juce::Justification::centredRight);
    }
    // Knob names and values.
    if (analyser.settings.open)
    for (const auto& k : knobs) {
        const auto kb = k.knob.getBounds();
        g.setFont(theme.labelFont(11.0f));
        g.setColour(Theme::muted);
        g.drawText(k.name.toUpperCase(), kb.getCentreX() - 45, kb.getY() - 16, 90, 14, juce::Justification::centred);
        g.setFont(theme.condFont(14.0f));
        g.setColour(Theme::text);
        const bool list = &k == &knobs[kSmooth]; // a list too: click the value
        g.drawText(k.text(k.knob.getValue()) + (list ? juce::String::fromUTF8(" \xe2\x96\xbe") : juce::String()), kb.getCentreX() - 45,
                   kb.getBottom() + 2, 90, 16, juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------
// My EQ over the spectrum
// ---------------------------------------------------------------------------
float AnalyserPanel::xOf(double f) const {
    const auto a = plotArea();
    const auto& s = analyser.settings;
    return a.getX() + (float)(std::log2(f / s.fLo) / std::log2(s.fHi / s.fLo)) * a.getWidth();
}
double AnalyserPanel::freqAt(float x) const {
    const auto a = plotArea();
    const auto& s = analyser.settings;
    return s.fLo * std::pow(s.fHi / s.fLo, juce::jlimit(0.0, 1.0, (double)((x - a.getX()) / a.getWidth())));
}
float AnalyserPanel::eqY(double db) const {
    const auto a = plotArea();
    return a.getCentreY() - (float)(juce::jlimit(-kEqRange - 1, kEqRange + 1, db) / kEqRange) * a.getHeight() * 0.5f;
}
juce::Point<float> AnalyserPanel::nodePos(const bteq::FilterSpec& f) const { return { xOf(f.fc), eqY(f.gainDb) }; }
int AnalyserPanel::nodeAt(juce::Point<float> p) const {
    const auto bands = proc.myEq().bands;
    int best = -1;
    float bestDist = 12.0f;
    for (int i = 0; i < (int)bands.size(); ++i) {
        if (bands[(size_t)i].type == bteq::FilterType::LowPass) continue;
        const float d = nodePos(bands[(size_t)i]).getDistanceFrom(p);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void AnalyserPanel::paintMyEq(juce::Graphics& g) {
    const auto a = plotArea();
    const auto mine = proc.myEq();
    const auto colour = Theme::myEqFor(proc.accentIndex);
    // Its own scale on the right.
    g.setFont(theme.condFont(12.0f));
    g.setColour(colour.withAlpha(0.6f));
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
        g.drawText((db > 0 ? "+" : "") + juce::String((int)db), juce::Rectangle<float>(a.getRight() + 4, eqY(db) - 8, 28, 16),
                   juce::Justification::centredLeft);
    g.setColour(colour.withAlpha(0.18f));
    g.drawHorizontalLine((int)eqY(0), a.getX(), a.getRight());

    std::vector<bteq::BiquadCoeffs> coeffs;
    for (const auto& f : mine.bands)
        if (f.enabled && std::abs(f.gainDb) > 0.01) coeffs.push_back(bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb, f.q, 48000.0));
    juce::Path curve;
    const int points = juce::jmax(100, (int)(a.getWidth() / 3));
    for (int i = 0; i <= points; ++i) {
        const double f = freqAt(a.getX() + a.getWidth() * (float)i / (float)points);
        double db = 0;
        for (const auto& c : coeffs) db += c.magnitudeDb(f, 48000.0);
        const float x = a.getX() + a.getWidth() * (float)i / (float)points, y = eqY(db);
        i == 0 ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
    }
    const auto c = mine.on ? colour : colour.withAlpha(0.4f);
    g.setColour(c);
    g.strokePath(curve, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont(theme.labelFont(12.0f));
    for (int i = 0; i < (int)mine.bands.size(); ++i) {
        const auto& f = mine.bands[(size_t)i];
        if (f.type == bteq::FilterType::LowPass) continue;
        const auto p = nodePos(f);
        if (!a.expanded(9).contains(p)) continue;
        const auto nc = f.enabled ? c : c.withMultipliedAlpha(0.35f);
        g.setColour(i == dragIndex ? nc.withAlpha(0.45f) : juce::Colour(0xff0B0C0D));
        g.fillEllipse(p.x - 9, p.y - 9, 18, 18);
        g.setColour(nc);
        g.drawEllipse(p.x - 9, p.y - 9, 18, 18, 2.0f);
        g.setColour(f.enabled ? Theme::textBright : Theme::muted);
        g.drawText(juce::String(i + 1), juce::Rectangle<float>(p.x - 9, p.y - 9, 18, 18), juce::Justification::centred);
    }
    if (dragIndex >= 0 && dragIndex < (int)mine.bands.size()) {
        const auto& f = mine.bands[(size_t)dragIndex];
        g.setColour(Theme::text);
        g.setFont(theme.condFont(15.0f));
        g.drawText("Band " + juce::String(dragIndex + 1) + ":  " + freqText(f.fc) + "   " + (f.gainDb > 0 ? "+" : "") +
                       juce::String(f.gainDb, 1) + " dB   Q " + juce::String(f.q, 2),
                   (int)a.getX() + 8, (int)a.getY() + 6, 360, 18, juce::Justification::centredLeft);
    }
}

void AnalyserPanel::editBand(int i, const std::function<void(bteq::FilterSpec&)>& edit) {
    auto bands = proc.myEq().bands;
    if (i < 0 || i >= (int)bands.size()) return;
    auto f = bands[(size_t)i];
    edit(f);
    if (!proc.myEq().on) proc.setMyEqOn(true); // tuning it here means you want to hear it
    proc.setMyEqBand(i, f);
    myEqButton.setToggleState(true, juce::dontSendNotification);
    if (auto* ed = proc.getActiveEditor()) ed->repaint();
    repaint();
}

void AnalyserPanel::mouseDown(const juce::MouseEvent& e) {
    if (analyser.settings.open && knobValueArea(kSmooth).contains(e.getPosition())) { showSmoothMenu(); return; }
    if (rangeBar().expanded(6, 4).contains(e.position)) {
        const auto& s = analyser.settings;
        const float yHi = rangeY(s.hi), yLo = rangeY(s.lo), y = e.position.y;
        rangeDrag = std::abs(y - yHi) < 7 && std::abs(y - yHi) <= std::abs(y - yLo) ? 1 : std::abs(y - yLo) < 7 ? 2 : 3;
        if (rangeDrag == 3 && (y < yHi || y > yLo)) { // outside the thumb: move it there
            const double mid = kRangeTop - (y - rangeBar().getY()) / rangeBar().getHeight() * (kRangeTop - kRangeBottom);
            setRange(mid - (s.hi - s.lo) / 2, mid + (s.hi - s.lo) / 2);
        }
        rangeStartLo = analyser.settings.lo;
        rangeStartHi = analyser.settings.hi;
        return;
    }
    dragIndex = nodeAt(e.position);
    repaint();
}
void AnalyserPanel::mouseUp(const juce::MouseEvent&) { dragIndex = -1; rangeDrag = 0; repaint(); }
void AnalyserPanel::mouseDrag(const juce::MouseEvent& e) {
    if (rangeDrag != 0) {
        const double dbPerPx = (kRangeTop - kRangeBottom) / rangeBar().getHeight() * (e.mods.isShiftDown() ? 0.25 : 1.0);
        const double d = -(double)e.getDistanceFromDragStartY() * dbPerPx;
        if (rangeDrag == 1) setRange(rangeStartLo, juce::jmax(rangeStartLo + 6, rangeStartHi + d));
        else if (rangeDrag == 2) setRange(juce::jmin(rangeStartHi - 6, rangeStartLo + d), rangeStartHi);
        else setRange(rangeStartLo + d, rangeStartHi + d);
        return;
    }
    if (dragIndex < 0) return;
    const double f = freqAt(e.position.x);
    const double db = (plotArea().getCentreY() - e.position.y) / (plotArea().getHeight() * 0.5) * kEqRange;
    editBand(dragIndex, [&](bteq::FilterSpec& b) {
        b.fc = f < 1000 ? std::round(f) : std::round(f / 10.0) * 10.0;
        b.gainDb = juce::jlimit(-20.0, 20.0, std::round(db * 10.0) / 10.0);
    });
    mouseX = (float)e.position.x;
}
void AnalyserPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (rangeBar().expanded(6, 4).contains(e.position) && w.deltaY != 0.0f) { // zoom around the middle
        const auto& s = analyser.settings;
        const double mid = (s.lo + s.hi) / 2, half = (s.hi - s.lo) / 2 * (w.deltaY > 0 ? 1 / 1.15 : 1.15);
        setRange(mid - half, mid + half);
        return;
    }
    const int i = nodeAt(e.position);
    if (i < 0 || w.deltaY == 0.0f) return;
    editBand(i, [&](bteq::FilterSpec& b) { b.q = juce::jlimit(0.1, 20.0, std::round(b.q * (w.deltaY > 0 ? 1.1 : 1.0 / 1.1) * 100.0) / 100.0); });
}

void AnalyserPanel::mouseMove(const juce::MouseEvent& e) {
    const bool overBar = rangeBar().expanded(6, 4).contains(e.position);
    if (overBar != overRangeBar) { overRangeBar = overBar; repaint(); }
    if (overBar) {
        const auto& s = analyser.settings;
        const bool end = std::abs(e.position.y - rangeY(s.hi)) < 7 || std::abs(e.position.y - rangeY(s.lo)) < 7;
        setMouseCursor(end ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor);
        return;
    }
    mouseX = (float)e.position.x;
    setMouseCursor(nodeAt(e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}
void AnalyserPanel::mouseExit(const juce::MouseEvent&) { mouseX = -1; overRangeBar = false; repaint(); }
void AnalyserPanel::mouseDoubleClick(const juce::MouseEvent& e) {
    if (rangeBar().expanded(6, 4).contains(e.position)) { rangeDrag = 0; const Settings d; setRange(d.lo, d.hi); return; }
    const int i = nodeAt(e.position);
    if (i >= 0) { editBand(i, [](bteq::FilterSpec& b) { b.enabled = !b.enabled; }); dragIndex = -1; return; }
    if (plotArea().contains(e.position)) { analyser.reset(); repaint(); }
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
AnalyserWindow::AnalyserWindow(BteqProcessor& p, const Theme& t, juce::LookAndFeel& lnf, juce::Colour accent, std::function<void()> onClose)
    : juce::DocumentWindow("BEQ Spectrum", juce::Colour(0xff0B0C0D), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      closed(std::move(onClose)) {
    panel = new AnalyserPanel(p, t, lnf);
    panel->accent = accent;
    setContentOwned(panel, false);
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(780, 400, 4000, 3000);
    centreWithSize(940, 520);
    setVisible(true);
}

void AnalyserWindow::closeButtonPressed() {
    if (closed) closed(); // the editor deletes this window
}

void AnalyserWindow::setAccent(juce::Colour c) {
    panel->accent = c;
    panel->repaint();
}
