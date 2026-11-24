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
    rebuild(false);
}

void BteqProcessor::selectProfile(int index) {
    const juce::ScopedLock sl(buildLock);
    profileIndex = juce::jmax(0, index);
    rebuild(false);
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
    const auto* p = selectedProfile();
    return p != nullptr && !p->graphicGainsDb.empty() && !holder->library.graphicFrequencies().empty();
}

bool BteqProcessor::graphicModeActive() const {
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
    const auto it = edits.find(profileKey(*p));
    return it == edits.end() ? nullptr : &it->second;
}

double BteqProcessor::publishedPreampDb() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return 0;
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
    if (p == nullptr || !p->hasParametric) return;
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
    auto& e = edits[profileKey(*p)];
    e.preampEdited = true;
    e.preampDb = juce::jlimit(-30.0, 12.0, db);
    ++editVersion;
    rebuild(false);
}

void BteqProcessor::resetEdits() {
    const juce::ScopedLock sl(buildLock);
    if (const auto* p = selectedProfile()) edits.erase(profileKey(*p));
    ++editVersion;
    rebuild(false);
}

// ---------------------------------------------------------------------------
// Engine building (message thread, or prepareToPlay)
// ---------------------------------------------------------------------------
void BteqProcessor::timerCallback() { rebuild(false); }

void BteqProcessor::rebuild(bool force) {
    const juce::ScopedLock sl(buildLock);
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

void BteqProcessor::buildParametric(const bteq::HeadphoneProfile*, double k, double sr) {
    auto eq = std::make_unique<bteq::ParametricEq>();
    const auto preset = effectiveParametric(); // flat when nothing is selected
    // Preamp scales with the correction amount; at 100% it is exactly the published value.
    eq->prepare(preset, sr, 2, k, /*autoPreamp*/ false);
    preampDb = eq->getPreampDb();
    {
        const juce::SpinLock::ScopedLockType lock(swapLock);
        pendingEq = std::move(eq);
        pendingReady = true;
    }
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
    convolution.loadImpulseResponse(std::move(ir), sr, juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    preampDb = pre * k; // already inside the filter; shown so both modes read the same way
    graphicReady = true;
}

void BteqProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    sampleRate = sr;
    maxBlock = samplesPerBlock;
    convolution.prepare({ sr, (juce::uint32)samplesPerBlock, 2 });
    trimGain.reset(sr, 0.02);
    trimGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(trimParam->load()));
    rebuild(true);
    // Take the freshly built parametric engine immediately (audio is stopped here).
    const juce::SpinLock::ScopedLockType lock(swapLock);
    if (pendingReady) {
        std::swap(activeEq, pendingEq);
        pendingReady = false;
    }
    if (activeEq) activeEq->reset();
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------
void BteqProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear(ch, 0, n);

    if (pendingReady.load()) {
        const juce::SpinLock::ScopedTryLockType lock(swapLock);
        if (lock.isLocked()) {
            std::swap(activeEq, pendingEq);
            pendingReady = false;
        }
    }

    const bool bypass = bypassParam->load() > 0.5f;
    const bool graphic = graphicReady.load();
    if (!bypass) {
        if (graphic) {
            if (!lastBlockGraphic) convolution.reset();
            juce::dsp::AudioBlock<float> block(buffer);
            convolution.process(juce::dsp::ProcessContextReplacing<float>(block));
        } else if (activeEq) {
            if (lastBlockGraphic) activeEq->reset();
            const int chans = juce::jmin(buffer.getNumChannels(), activeEq->numChannels());
            for (int ch = 0; ch < chans; ++ch) activeEq->processChannel(ch, buffer.getWritePointer(ch), n);
        }
    }
    lastBlockGraphic = graphic && !bypass;

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
    {
        const juce::ScopedLock sl(buildLock);
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
    profileIndex = indexForKey(headphoneName, state.getProperty("profile").toString());
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
    if (const auto* e = currentEdit()) writeEdit(t, *e);
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
    if (const auto* p = selectedProfile()) {
        const auto key = profileKey(*p);
        const auto e = readEdit(t);
        if (e.filtersEdited || e.preampEdited) edits[key] = e;
        else edits.erase(key);
    }
    ++editVersion;
    rebuild(false);
}

juce::AudioProcessorEditor* BteqProcessor::createEditor() { return new BteqEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BteqProcessor(); }
