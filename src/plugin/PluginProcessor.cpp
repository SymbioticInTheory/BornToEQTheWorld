#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

LibraryHolder::LibraryHolder() {
    juce::MemoryInputStream compressed(BinaryData::headphones_tsv_gz, BinaryData::headphones_tsv_gzSize, false);
    juce::GZIPDecompressorInputStream gz(&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock text;
    gz.readIntoMemoryBlock(text);
    loaded = library.loadFromText(std::string(static_cast<const char*>(text.getData()), text.getSize()));
}

CurvesHolder::CurvesHolder() {
    juce::MemoryInputStream compressed(BinaryData::curves_tsv_gz, BinaryData::curves_tsv_gzSize, false);
    juce::GZIPDecompressorInputStream gz(&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock text;
    gz.readIntoMemoryBlock(text);
    loaded = curves.loadFromText(std::string(static_cast<const char*>(text.getData()), text.getSize()));
}

namespace {
const juce::String kAmount = "amount", kBypass = "bypass", kMode = "mode", kTrim = "trim";

juce::String profileKey(const bteq::HeadphoneProfile& p) {
    return juce::String(p.source) + "|" + juce::String(p.rig) + "|" + (p.originalPreset ? "O" : "A") + "|" +
           juce::String(p.target);
}
// "type,fc,gain,q,enabled;..." with type P, L, H or W (low pass).
juce::String filtersToString(const std::vector<bteq::FilterSpec>& fs) {
    juce::StringArray parts;
    for (const auto& f : fs) {
        const char t = f.type == bteq::FilterType::LowShelf ? 'L' : f.type == bteq::FilterType::HighShelf ? 'H'
                     : f.type == bteq::FilterType::LowPass ? 'W' : 'P';
        parts.add(juce::String::charToString(t) + "," + juce::String(f.fc, 3) + "," + juce::String(f.gainDb, 3) + "," +
                  juce::String(f.q, 4) + "," + (f.enabled ? "1" : "0"));
    }
    return parts.joinIntoString(";");
}

std::vector<bteq::FilterSpec> filtersFromString(const juce::String& s) {
    std::vector<bteq::FilterSpec> fs;
    for (const auto& part : juce::StringArray::fromTokens(s, ";", "")) {
        const auto v = juce::StringArray::fromTokens(part, ",", "");
        if (v.size() < 4) continue;
        bteq::FilterSpec f;
        const auto t = v[0].isNotEmpty() ? v[0][0] : 'P';
        f.type = t == 'L' ? bteq::FilterType::LowShelf : t == 'H' ? bteq::FilterType::HighShelf
               : t == 'W' ? bteq::FilterType::LowPass : bteq::FilterType::Peaking;
        f.fc = v[1].getDoubleValue();
        f.gainDb = v[2].getDoubleValue();
        f.q = v[3].getDoubleValue();
        f.enabled = v.size() < 5 || v[4] != "0";
        fs.push_back(f);
    }
    return fs;
}
void writeEdit(juce::ValueTree& t, const BteqProcessor::ProfileEdit& e) {
    if (e.preampEdited) t.setProperty("preamp", e.preampDb, nullptr);
    if (e.filtersEdited) t.setProperty("filters", filtersToString(e.filters), nullptr);
}

BteqProcessor::ProfileEdit readEdit(const juce::ValueTree& t) {
    BteqProcessor::ProfileEdit e;
    if (t.hasProperty("preamp")) {
        e.preampEdited = true;
        e.preampDb = (double)t.getProperty("preamp");
    }
    if (t.hasProperty("filters")) {
        e.filtersEdited = true;
        e.filters = filtersFromString(t.getProperty("filters").toString());
    }
    return e;
}
} // namespace

BteqProcessor::BteqProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "BTEQ", createLayout()) {
    amountParam = params.getRawParameterValue(kAmount);
    bypassParam = params.getRawParameterValue(kBypass);
    modeParam = params.getRawParameterValue(kMode);
    trimParam = params.getRawParameterValue(kTrim);
    myEqSettings.bands = defaultMyEqBands();
    startTimerHz(30);
}

BteqProcessor::~BteqProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout BteqProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kAmount, 1 }, "Correction", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { kBypass, 1 }, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { kMode, 1 }, "EQ mode",
                                                            juce::StringArray { "Parametric", "Graphic" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kTrim, 1 }, "Output trim", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    return layout;
}

bool BteqProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ---------------------------------------------------------------------------
// Selection (message thread)
// ---------------------------------------------------------------------------
void BteqProcessor::selectHeadphone(const juce::String& name) {
    const juce::ScopedLock sl(buildLock);
    headphoneName = name;
    profileIndex = 0;
    computeMatch();
    rebuild(false);
}

void BteqProcessor::selectProfile(int index) {
    const juce::ScopedLock sl(buildLock);
    profileIndex = juce::jmax(0, index);
    computeMatch();
    rebuild(false);
}

// ---------------------------------------------------------------------------
// Sounding like another headphone (message thread)
// ---------------------------------------------------------------------------
const bteq::CurveLibrary* BteqProcessor::curveLibrary() {
    if (curvesHolder == nullptr) curvesHolder = std::make_unique<juce::SharedResourcePointer<CurvesHolder>>();
    return (*curvesHolder)->loaded ? &(*curvesHolder)->curves : nullptr;
}

void BteqProcessor::setMatch(const juce::String& otherHeadphone) {
    const juce::ScopedLock sl(buildLock);
    matchName = otherHeadphone;
    computeMatch();
    rebuild(false);
}

// The EQ that turns this headphone's measurement into the other one's: the
// difference of the two curves, smoothed more in the treble (where
// measurements are least repeatable) and faded out above 12 kHz.
void BteqProcessor::computeMatch() {
    match = {};
    ++editVersion;
    const auto* p = selectedProfile();
    if (matchName.isEmpty() || p == nullptr) return;
    const auto* lib = curveLibrary();
    if (lib == nullptr) return;
    const auto& freqs = holder->library.graphicFrequencies();

    // This headphone: the selected profile's measurement, else its first one.
    auto own = lib->find(p->name, p->source, p->rig, p->originalPreset, p->target).measured;
    std::string ownSource = p->source, ownRig = p->rig;
    if (own == nullptr) {
        const auto all = lib->measurementsOf(p->name);
        if (all.empty()) return;
        own = all.front().curve;
        ownSource = all.front().source;
        ownRig = all.front().rig;
    }
    // The other headphone: prefer the same source and rig, then the same source.
    const auto others = lib->measurementsOf(matchName.toStdString());
    if (others.empty()) return;
    const auto* best = &others.front();
    int bestScore = -1;
    for (const auto& m : others) {
        const int score = (m.source == ownSource ? 2 : 0) + (m.rig == ownRig ? 1 : 0);
        if (score > bestScore) { bestScore = score; best = &m; }
    }
    if (own->size() != freqs.size() || best->curve->size() != freqs.size()) return;

    const size_t n = freqs.size();
    std::vector<double> diff(n), smooth(n);
    for (size_t i = 0; i < n; ++i) diff[i] = (double)(*best->curve)[i] - (double)(*own)[i];
    for (size_t i = 0; i < n; ++i) {
        const int half = freqs[i] < 2000 ? 1 : freqs[i] < 6000 ? 2 : 4; // about 1/6, 1/3 and 2/3 octave
        double sum = 0;
        int count = 0;
        for (int j = (int)i - half; j <= (int)i + half; ++j)
            if (j >= 0 && j < (int)n) { sum += diff[(size_t)j]; ++count; }
        double v = juce::jlimit(-18.0, 12.0, sum / count);
        if (freqs[i] > 12000) v *= juce::jlimit(0.0, 1.0, std::log(20000.0 / freqs[i]) / std::log(20000.0 / 12000.0));
        smooth[i] = v;
    }
    match.shape = smooth;
    match.preampDb = -std::max(0.0, *std::max_element(smooth.begin(), smooth.end()));
    match.ownMeasured.assign(own->begin(), own->end());
    match.otherMeasured.assign(best->curve->begin(), best->curve->end());
    match.source = juce::String(best->source) + (best->rig.empty() ? "" : " (" + juce::String(best->rig) + ")");
    match.sameRig = best->source == ownSource && best->rig == ownRig;
    match.active = true;
}

juce::String BteqProcessor::editKey() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return {};
    return match.active ? "MATCH|" + headphoneName + "|" + matchName : profileKey(*p);
}

const bteq::HeadphoneProfile* BteqProcessor::selectedProfile() const {
    if (headphoneName.isEmpty() || !holder->loaded) return nullptr;
    const auto idx = holder->library.headphoneIndex(headphoneName.toStdString());
    if (!idx) return nullptr;
    const auto profs = holder->library.profilesFor(*idx);
    if (profs.empty()) return nullptr;
    return profs[static_cast<size_t>(juce::jlimit(0, (int)profs.size() - 1, profileIndex))];
}

bool BteqProcessor::graphicAvailable() const {
    if (match.active) return true;
    const auto* p = selectedProfile();
    return p != nullptr && !p->graphicGainsDb.empty() && !holder->library.graphicFrequencies().empty();
}

bool BteqProcessor::graphicModeActive() const {
    if (match.active) return true;
    const auto* p = selectedProfile();
    const bool wantGraphic = modeParam->load() > 0.5f;
    // Profiles without a GraphicEQ curve (oratory1990's own presets) always run parametric.
    return p != nullptr && (p->hasParametric ? wantGraphic && graphicAvailable() : graphicAvailable());
}

double BteqProcessor::amount() const {
    return bypassParam->load() > 0.5f ? 0.0 : juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
}

double BteqProcessor::currentPreampDb() const { return preampDb; }

// ---------------------------------------------------------------------------
// Your edits (message thread)
// ---------------------------------------------------------------------------
const BteqProcessor::ProfileEdit* BteqProcessor::currentEdit() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return nullptr;
    const auto it = edits.find(editKey());
    return it == edits.end() ? nullptr : &it->second;
}

double BteqProcessor::publishedPreampDb() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return 0;
    if (match.active) return match.preampDb;
    if (p->hasParametric) return p->parametric.preampDb;
    if (!p->graphicGainsDb.empty()) return *std::max_element(p->graphicGainsDb.begin(), p->graphicGainsDb.end());
    return 0;
}

double BteqProcessor::preampSettingDb() const {
    const auto* e = currentEdit();
    return e != nullptr && e->preampEdited ? e->preampDb : publishedPreampDb();
}

bteq::ParametricPreset BteqProcessor::effectiveParametric() const {
    bteq::ParametricPreset preset;
    const auto* p = selectedProfile();
    if (p == nullptr || !p->hasParametric) return preset;
    preset = p->parametric;
    if (const auto* e = currentEdit(); e != nullptr && e->filtersEdited) preset.filters = e->filters;
    preset.preampDb = preampSettingDb();
    return preset;
}

std::vector<double> BteqProcessor::graphicShapeDb() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return {};
    if (match.active) return match.shape;
    const double pre = publishedPreampDb();
    std::vector<double> shape;
    shape.reserve(p->graphicGainsDb.size());
    for (const auto g : p->graphicGainsDb) shape.push_back((double)g - pre);
    return shape;
}

bool BteqProcessor::selectionEdited() const {
    const auto* e = currentEdit();
    return e != nullptr && (e->filtersEdited || e->preampEdited);
}

void BteqProcessor::setFilter(int index, const bteq::FilterSpec& spec) {
    const juce::ScopedLock sl(buildLock);
    const auto* p = selectedProfile();
    if (p == nullptr || !p->hasParametric || match.active) return;
    auto& e = edits[profileKey(*p)];
    if (!e.filtersEdited) {
        e.filters = p->parametric.filters;
        e.filtersEdited = true;
    }
    if (index < 0 || index >= (int)e.filters.size()) return;
    e.filters[(size_t)index] = spec;
    ++editVersion;
    rebuild(false);
}

void BteqProcessor::setPreamp(double db) {
    const juce::ScopedLock sl(buildLock);
    const auto* p = selectedProfile();
    if (p == nullptr) return;
    auto& e = edits[editKey()];
    e.preampEdited = true;
    e.preampDb = juce::jlimit(-30.0, 12.0, db);
    ++editVersion;
    rebuild(false);
}

void BteqProcessor::resetPreamp() {
    const juce::ScopedLock sl(buildLock);
    const auto* p = selectedProfile();
    if (p == nullptr) return;
    const auto it = edits.find(editKey());
    if (it == edits.end()) return;
    it->second.preampEdited = false;
    ++editVersion;
    rebuild(false);
}

void BteqProcessor::resetEdits() {
    const juce::ScopedLock sl(buildLock);
    if (selectedProfile() != nullptr) edits.erase(editKey());
    ++editVersion;
    rebuild(false);
}

// ---------------------------------------------------------------------------
// My EQ (message thread)
// ---------------------------------------------------------------------------
std::vector<bteq::FilterSpec> BteqProcessor::defaultMyEqBands() {
    using T = bteq::FilterType;
    const std::pair<T, double> bands[] = { { T::LowShelf, 105 }, { T::Peaking, 200 },  { T::Peaking, 500 },
                                           { T::Peaking, 1000 }, { T::Peaking, 2000 }, { T::Peaking, 4000 },
                                           { T::Peaking, 8000 }, { T::HighShelf, 10000 } };
    std::vector<bteq::FilterSpec> out;
    for (const auto& [type, fc] : bands) {
        bteq::FilterSpec f;
        f.type = type;
        f.fc = fc;
        f.gainDb = 0;
        f.q = type == T::Peaking ? 1.0 : 0.7;
        out.push_back(f);
    }
    return out;
}

BteqProcessor::MyEq BteqProcessor::myEq() const {
    const juce::ScopedLock sl(buildLock);
    return myEqSettings;
}

void BteqProcessor::setMyEqBand(int index, const bteq::FilterSpec& spec) {
    const juce::ScopedLock sl(buildLock);
    if (index < 0 || index >= (int)myEqSettings.bands.size()) return;
    myEqSettings.bands[(size_t)index] = spec;
    ++myEqVersion;
    rebuild(false);
}

void BteqProcessor::setMyEqOn(bool on) {
    const juce::ScopedLock sl(buildLock);
    myEqSettings.on = on;
    ++myEqVersion;
    rebuild(false);
}

void BteqProcessor::resetMyEq() {
    const juce::ScopedLock sl(buildLock);
    myEqSettings.bands = defaultMyEqBands();
    ++myEqVersion;
    rebuild(false);
}

bool BteqProcessor::myEqEdited() const {
    const juce::ScopedLock sl(buildLock);
    for (const auto& b : myEqSettings.bands)
        if (std::abs(b.gainDb) > 0.01) return true;
    return false;
}

bool BteqProcessor::myEqActive() const { return myEqSettings.on && myEqEdited(); }

// ---------------------------------------------------------------------------
// Engine building (message thread, or prepareToPlay)
// ---------------------------------------------------------------------------
void BteqProcessor::timerCallback() {
    rebuild(false);
    freeAllRetired();
}

void BteqProcessor::rebuild(bool force) {
    const juce::ScopedLock sl(buildLock);
    if (force || myEqVersion != builtMyEqVersion || !juce::exactlyEqual(sampleRate, builtMyEqRate)) {
        buildMyEq(sampleRate);
        builtMyEqVersion = myEqVersion;
        builtMyEqRate = sampleRate;
    }

    const auto* p = selectedProfile();
    const double k = juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
    const bool graphic = graphicModeActive();
    if (!force && p == builtProfile && std::abs(k - builtAmount) < 1e-4 && juce::exactlyEqual(sampleRate, builtRate) &&
        graphic == builtGraphic && editVersion == builtEditVersion)
        return;

    if (graphic)
        buildGraphic(p, k, sampleRate);
    else
        buildParametric(p, k, sampleRate);

    builtProfile = p;
    builtAmount = k;
    builtRate = sampleRate;
    builtGraphic = graphic;
    builtEditVersion = editVersion;
}

template <class E> void BteqProcessor::publish(EngineSlot<E>& slot, std::unique_ptr<E> engine) {
    freeAllRetired();
    std::unique_ptr<E> replaced; // an older engine that never got used is freed here, outside the lock
    {
        const juce::SpinLock::ScopedLockType lock(swapLock);
        replaced = std::move(slot.pending);
        slot.pending = std::move(engine);
        slot.pendingReady = true;
    }
}

void BteqProcessor::buildParametric(const bteq::HeadphoneProfile*, double k, double sr) {
    auto engine = std::make_unique<IirEngine>();
    const auto preset = effectiveParametric(); // flat when nothing is selected
    // Preamp scales with the correction amount; at 100% it is exactly the published value.
    engine->eq.prepare(preset, sr, 2, k, /*autoPreamp*/ false);
    preampDb = engine->eq.getPreampDb();
    publish(eqSlot, std::move(engine));
    graphicReady = false;
}

void BteqProcessor::buildGraphic(const bteq::HeadphoneProfile*, double k, double sr) {
    const auto& freqs = holder->library.graphicFrequencies();
    // Curve shape plus preamp, both scaled by the correction amount. Unedited at 100%
    // this is exactly the published GraphicEQ curve.
    const double pre = preampSettingDb();
    auto gains = graphicShapeDb();
    for (auto& g : gains) g = (g + pre) * k;
    const auto taps = bteq::designGraphicEqFir(freqs, gains, sr);
    juce::AudioBuffer<float> ir(1, (int)taps.size());
    std::copy(taps.begin(), taps.end(), ir.getWritePointer(0));

    // Load first, then prepare: prepare runs the queued load and builds the
    // engine right here, so the new filter is complete before the swap.
    auto engine = std::make_unique<GraphicEngine>();
    engine->conv.loadImpulseResponse(std::move(ir), sr, juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    engine->blockSize = juce::jmax(1, maxBlock);
    engine->conv.prepare({ sr, (juce::uint32)engine->blockSize, 2 });
    publish(graphicSlot, std::move(engine));
    preampDb = pre * k; // already inside the filter; shown so both modes read the same way
    graphicReady = true;
}

void BteqProcessor::buildMyEq(double sr) {
    bteq::ParametricPreset preset;
    if (myEqSettings.on)
        for (const auto& b : myEqSettings.bands)
            if (std::abs(b.gainDb) > 0.01) preset.filters.push_back(b);
    auto engine = std::make_unique<IirEngine>();
    // Its own headroom: the level drops by its highest boost, so it can't clip
    // more than the correction alone.
    engine->eq.prepare(preset, sr, 2, 1.0, /*autoPreamp*/ true);
    myEqPreampDb = preset.filters.empty() ? 0.0 : engine->eq.getPreampDb();
    myEqRunning = !preset.filters.empty();
    publish(extraSlot, std::move(engine));
}

template <class E> void BteqProcessor::freeRetired(EngineSlot<E>& slot) {
    std::array<std::unique_ptr<E>, 8> toFree;
    {
        const juce::SpinLock::ScopedLockType lock(swapLock);
        for (size_t i = 0; i < slot.retired.size(); ++i) toFree[i] = std::move(slot.retired[i]);
    }
}

void BteqProcessor::freeAllRetired() {
    freeRetired(eqSlot);
    freeRetired(graphicSlot);
    freeRetired(extraSlot);
}

void BteqProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    sampleRate = sr;
    maxBlock = samplesPerBlock;
    fadeBuffer.setSize(2, juce::jmax(1, samplesPerBlock));
    fadeLength = juce::jmax(1, juce::roundToInt(sr * 0.02));
    trimGain.reset(sr, 0.02);
    trimGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(trimParam->load()));
    rebuild(true);
    // Take the freshly built engines immediately (audio is stopped here).
    const juce::SpinLock::ScopedLockType lock(swapLock);
    auto install = [](auto& slot) {
        if (slot.pendingReady) {
            std::swap(slot.active, slot.pending);
            slot.pendingReady = false;
        }
        if (slot.active) slot.active->reset();
        slot.fading.reset();
        slot.usedLastBlock = false;
    };
    install(eqSlot);
    install(graphicSlot);
    install(extraSlot);
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

// Called with swapLock held. Takes a new engine only when the last crossfade
// is done and there is somewhere to hand the old engine back for freeing.
template <class E> void BteqProcessor::takePending(EngineSlot<E>& slot) {
    if (!slot.pendingReady || slot.fading != nullptr) return;
    const auto free = std::find(slot.retired.begin(), slot.retired.end(), nullptr);
    if (free == slot.retired.end()) return;
    auto old = std::move(slot.active);
    slot.active = std::move(slot.pending);
    slot.pendingReady = false;
    if (old == nullptr) return;
    if (slot.usedLastBlock) {
        slot.fading = std::move(old); // fade from it so the change is smooth
        slot.fadePos = 0;
    } else {
        *free = std::move(old); // not playing: nothing to fade from
    }
}

template <class E>
void BteqProcessor::runSlot(EngineSlot<E>& slot, juce::AudioBuffer<float>& buffer, int chans, int n) {
    if (!slot.usedLastBlock) slot.active->reset(); // coming back after a pause: start clean
    const bool fade = slot.fading != nullptr && slot.fadePos < fadeLength && n <= fadeBuffer.getNumSamples();
    if (fade)
        for (int ch = 0; ch < chans; ++ch) fadeBuffer.copyFrom(ch, 0, buffer, ch, 0, n);
    slot.active->process(buffer.getArrayOfWritePointers(), chans, n);
    if (fade) {
        // The old filter fades out while the new one fades in.
        slot.fading->process(fadeBuffer.getArrayOfWritePointers(), chans, n);
        for (int ch = 0; ch < chans; ++ch) {
            auto* out = buffer.getWritePointer(ch);
            const auto* prev = fadeBuffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float t = juce::jmin(1.0f, (float)(slot.fadePos + i + 1) / (float)fadeLength);
                out[i] = out[i] * t + prev[i] * (1.0f - t);
            }
        }
        slot.fadePos += n;
    } else if (slot.fading != nullptr) {
        slot.fadePos = fadeLength; // block too large to fade: switch straight over
    }
}

// Hands a finished (or unused) fading engine back to the message thread.
template <class E> void BteqProcessor::retireFading(EngineSlot<E>& slot, bool usedThisBlock) {
    slot.usedLastBlock = usedThisBlock;
    if (slot.fading == nullptr || (usedThisBlock && slot.fadePos < fadeLength)) return;
    const juce::SpinLock::ScopedTryLockType lock(swapLock);
    if (!lock.isLocked()) return;
    const auto free = std::find(slot.retired.begin(), slot.retired.end(), nullptr);
    if (free != slot.retired.end()) *free = std::move(slot.fading);
}

void BteqProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear(ch, 0, n);

    if (eqSlot.pendingReady.load() || graphicSlot.pendingReady.load() || extraSlot.pendingReady.load()) {
        const juce::SpinLock::ScopedTryLockType lock(swapLock);
        if (lock.isLocked()) {
            takePending(eqSlot);
            takePending(graphicSlot);
            takePending(extraSlot);
        }
    }

    const bool bypass = bypassParam->load() > 0.5f;
    const int chans = juce::jmin(buffer.getNumChannels(), 2);
    bool usedGraphic = false, usedEq = false, usedExtra = false;
    if (!bypass) {
        if (graphicReady.load() && graphicSlot.active != nullptr) {
            runSlot(graphicSlot, buffer, chans, n);
            usedGraphic = true;
        } else if (eqSlot.active != nullptr) {
            runSlot(eqSlot, buffer, chans, n);
            usedEq = true;
        }
        // My EQ, on top of the correction. Kept running while its last change fades out.
        if (extraSlot.active != nullptr && (myEqRunning.load() || extraSlot.fading != nullptr)) {
            runSlot(extraSlot, buffer, chans, n);
            usedExtra = true;
        }
    }
    retireFading(graphicSlot, usedGraphic);
    retireFading(eqSlot, usedEq);
    retireFading(extraSlot, usedExtra);

    trimGain.setTargetValue(juce::Decibels::decibelsToGain(trimParam->load()));
    trimGain.applyGain(buffer, n);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void BteqProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = params.copyState();
    state.setProperty("headphone", headphoneName, nullptr);
    const auto* p = selectedProfile();
    state.setProperty("profile", p ? profileKey(*p) : juce::String(), nullptr);
    state.setProperty("accent", accentIndex, nullptr);
    state.setProperty("accentOrder", 2, nullptr);
    state.setProperty("advanced", advancedView, nullptr);
    state.setProperty("hiddenCurves", hiddenCurves, nullptr);
    state.setProperty("uiScale", uiScale, nullptr);
    state.setProperty("match", matchName, nullptr);
    state.setProperty("editingMyEq", editingMyEq, nullptr);
    {
        const juce::ScopedLock sl(buildLock);
        state.setProperty("myEq", filtersToString(myEqSettings.bands), nullptr);
        state.setProperty("myEqOn", myEqSettings.on, nullptr);
        juce::ValueTree list("EDITS");
        for (const auto& [key, e] : edits) {
            if (!e.filtersEdited && !e.preampEdited) continue;
            juce::ValueTree t("EDIT");
            t.setProperty("key", key, nullptr);
            writeEdit(t, e);
            list.appendChild(t, nullptr);
        }
        state.appendChild(list, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void BteqProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    const auto list = state.getChildWithName("EDITS");
    state.removeChild(list, nullptr);
    params.replaceState(state);
    {
        const juce::ScopedLock sl(buildLock);
        edits.clear();
        for (const auto& t : list) {
            edits[t.getProperty("key").toString()] = readEdit(t);
        }
        ++editVersion;
    }
    headphoneName = state.getProperty("headphone").toString();
    accentIndex = (int)state.getProperty("accent", 0);
    // Sessions from before red became the first colour had Ember first and Blood red second.
    if (!state.hasProperty("accentOrder") && accentIndex == 1) accentIndex = 0;
    advancedView = (bool)state.getProperty("advanced", false);
    hiddenCurves = (int)state.getProperty("hiddenCurves", 0);
    uiScale = (float)state.getProperty("uiScale", 0.0);
    editingMyEq = (bool)state.getProperty("editingMyEq", false);
    readMyEq(state);
    profileIndex = indexForKey(headphoneName, state.getProperty("profile").toString());
    {
        const juce::ScopedLock sl(buildLock);
        matchName = state.getProperty("match").toString();
        computeMatch();
    }
    rebuild(true);
}

int BteqProcessor::indexForKey(const juce::String& headphone, const juce::String& key) const {
    if (const auto idx = holder->library.headphoneIndex(headphone.toStdString())) {
        const auto profs = holder->library.profilesFor(*idx);
        for (size_t i = 0; i < profs.size(); ++i)
            if (profileKey(*profs[i]) == key) return (int)i;
    }
    return 0;
}

juce::ValueTree BteqProcessor::makeSnapshot() const {
    juce::ValueTree t("SNAPSHOT");
    t.setProperty("headphone", headphoneName, nullptr);
    const auto* p = selectedProfile();
    t.setProperty("profile", p ? profileKey(*p) : juce::String(), nullptr);
    t.setProperty("mode", modeParam->load() > 0.5f ? 1 : 0, nullptr);
    t.setProperty("amount", (double)amountParam->load(), nullptr);
    t.setProperty("match", matchName, nullptr);
    if (const auto* e = currentEdit()) writeEdit(t, *e);
    const auto mine = myEq();
    t.setProperty("myEq", filtersToString(mine.bands), nullptr);
    t.setProperty("myEqOn", mine.on, nullptr);
    return t;
}

void BteqProcessor::loadSnapshot(const juce::ValueTree& t) {
    auto setParam = [this](const juce::String& id, float value) {
        if (auto* prm = params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(value));
    };
    setParam(kMode, (float)(int)t.getProperty("mode", 0));
    setParam(kAmount, (float)(double)t.getProperty("amount", 100.0));
    const juce::ScopedLock sl(buildLock);
    headphoneName = t.getProperty("headphone").toString();
    profileIndex = indexForKey(headphoneName, t.getProperty("profile").toString());
    matchName = t.getProperty("match").toString();
    computeMatch();
    if (selectedProfile() != nullptr) {
        const auto key = editKey();
        const auto e = readEdit(t);
        if (e.filtersEdited || e.preampEdited) edits[key] = e;
        else edits.erase(key);
    }
    readMyEq(t);
    ++editVersion;
    rebuild(false);
}

// Reads My EQ from a session or saved profile; older ones without it leave it as it is.
void BteqProcessor::readMyEq(const juce::ValueTree& t) {
    if (!t.hasProperty("myEq")) return;
    const juce::ScopedLock sl(buildLock);
    auto bands = filtersFromString(t.getProperty("myEq").toString());
    if (bands.size() != defaultMyEqBands().size()) bands = defaultMyEqBands();
    myEqSettings.bands = bands;
    myEqSettings.on = (bool)t.getProperty("myEqOn", true);
    ++myEqVersion;
}

juce::AudioProcessorEditor* BteqProcessor::createEditor() { return new BteqEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BteqProcessor(); }
