#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ApoConfig.h"

void setStandaloneAudioOpen(bool open); // StandaloneAudio.cpp
bool standaloneAudioHasInput();          // StandaloneAudio.cpp

#include <BinaryData.h>

LibraryHolder::LibraryHolder() {
    juce::MemoryInputStream compressed(BinaryData::headphones_tsv_gz, BinaryData::headphones_tsv_gzSize, false);
    juce::GZIPDecompressorInputStream gz(&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock text;
    gz.readIntoMemoryBlock(text);
    loaded = library.loadFromText(static_cast<const char*>(text.getData()), text.getSize());
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

// ---------------------------------------------------------------------------
// Your own presets
// ---------------------------------------------------------------------------
juce::File CustomPresetStore::file() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Born To EQ The World")
        .getChildFile("custom-presets.xml");
}

namespace {
bteq::HeadphoneProfile makeCustom(const std::string& headphone, const std::string& name, const bteq::ParametricPreset& preset) {
    bteq::HeadphoneProfile p;
    p.name = headphone;
    p.source = CustomPresetStore::kSource;
    p.originalPreset = true;
    p.target = name;
    p.hasParametric = true;
    p.parametric = preset;
    return p;
}
} // namespace

CustomPresetStore::CustomPresetStore() {
    if (auto xml = juce::XmlDocument::parse(file()))
        for (auto* e : xml->getChildWithTagNameIterator("PRESET")) {
            bteq::ParametricPreset preset;
            preset.preampDb = e->getDoubleAttribute("preamp");
            preset.filters = filtersFromString(e->getStringAttribute("filters"));
            presets.push_back(makeCustom(e->getStringAttribute("headphone").toStdString(),
                                         e->getStringAttribute("name").toStdString(), preset));
        }
}

bteq::HeadphoneProfile* CustomPresetStore::find(const std::string& headphone, const std::string& name) {
    for (auto& p : presets)
        if (p.name == headphone && p.target == name) return &p;
    return nullptr;
}

void CustomPresetStore::save() const {
    juce::XmlElement xml("CUSTOMPRESETS");
    for (const auto& p : presets) {
        auto* e = xml.createNewChildElement("PRESET");
        e->setAttribute("headphone", juce::String(p.name));
        e->setAttribute("name", juce::String(p.target));
        e->setAttribute("preamp", p.parametric.preampDb);
        e->setAttribute("filters", filtersToString(p.parametric.filters));
    }
    file().getParentDirectory().createDirectory();
    xml.writeTo(file());
}

// ---------------------------------------------------------------------------
// The library, kept only while needed in Low memory mode
// ---------------------------------------------------------------------------
juce::File BteqProcessor::settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Born To EQ The World")
        .getChildFile("settings.xml");
}

namespace {
bteq::spectrum::Settings& analyserCache() {
    static bteq::spectrum::Settings s = [] {
        if (auto xml = juce::XmlDocument::parse(BteqProcessor::settingsFile()))
            if (xml->hasAttribute("analyser"))
                return bteq::spectrum::Settings::fromString(xml->getStringAttribute("analyser").toStdString());
        return bteq::spectrum::Settings {};
    }();
    return s;
}
int analyserVersionCounter = 0;
} // namespace

const bteq::spectrum::Settings& BteqProcessor::analyserSettings() { return analyserCache(); }
int BteqProcessor::analyserVersion() { return analyserVersionCounter; }

void BteqProcessor::setAnalyserSettings(const bteq::spectrum::Settings& s, bool writeFile) {
    analyserCache() = s;
    ++analyserVersionCounter;
    if (!writeFile) return;
    auto xml = juce::XmlDocument::parse(settingsFile());
    if (xml == nullptr) xml = std::make_unique<juce::XmlElement>("SETTINGS");
    xml->setAttribute("analyser", juce::String(s.toString()));
    settingsFile().getParentDirectory().createDirectory();
    xml->writeTo(settingsFile());
}

std::atomic<bool>& BteqProcessor::lowMemoryFlag() {
    static std::atomic<bool> flag { [] {
        if (auto xml = juce::XmlDocument::parse(settingsFile())) return xml->getBoolAttribute("lowMemory", false);
        return false;
    }() };
    return flag;
}

void BteqProcessor::setLowMemoryMode(bool on) { lowMemoryFlag() = on; }

const LibraryHolder& BteqProcessor::lib() const {
    const juce::ScopedLock sl(libLock);
    if (!holder.has_value()) holder.emplace();
    if (graphicFreqs.empty()) graphicFreqs = (*holder)->library.graphicFrequencies();
    return **holder;
}

const std::vector<double>& BteqProcessor::graphicFrequencies() const {
    if (graphicFreqs.empty()) lib();
    return graphicFreqs;
}

void BteqProcessor::releaseLibraryIfUnused() {
    if (!lowMemoryMode() || getActiveEditor() != nullptr) return;
    const juce::ScopedLock sl(libLock);
    holder.reset(); // freed once no other BEQ in this process holds it
}

std::vector<const bteq::HeadphoneProfile*> BteqProcessor::profilesFor(const juce::String& headphone) const {
    std::vector<const bteq::HeadphoneProfile*> out;
    if (headphone.isEmpty()) return out;
    const auto name = headphone.toStdString();
    {
        const juce::ScopedLock sl(libLock);
        // The selected headphone comes from the processor's own copy.
        if (headphone == headphoneName && cachedHeadphone != name) {
            const auto& l = lib();
            cachedProfiles.clear();
            if (l.loaded)
                if (const auto idx = l.library.headphoneIndex(name))
                    for (const auto* p : l.library.profilesFor(*idx)) cachedProfiles.push_back(*p);
            cachedHeadphone = name;
            ++cacheVersion;
        }
        if (name == cachedHeadphone) {
            for (const auto& p : cachedProfiles) out.push_back(&p);
        } else {
            const auto& l = lib();
            if (l.loaded)
                if (const auto idx = l.library.headphoneIndex(name)) out = l.library.profilesFor(*idx);
        }
    }
    for (const auto& c : customs->presets)
        if (c.name == name) out.push_back(&c);
    return out;
}

bool BteqProcessor::selectedIsCustom() const { return isCustom(selectedProfile()); }

bteq::HeadphoneProfile* BteqProcessor::selectedCustom() {
    const auto* p = selectedProfile();
    return isCustom(p) && !match.active ? const_cast<bteq::HeadphoneProfile*>(p) : nullptr;
}

juce::String BteqProcessor::uniqueCustomName(const juce::String& wanted, const bteq::HeadphoneProfile* except) const {
    const auto base = wanted.trim().isEmpty() ? juce::String("My preset") : wanted.trim();
    auto taken = [&](const juce::String& n) {
        for (const auto& c : customs->presets)
            if (&c != except && c.name == headphoneName.toStdString() && c.target == n.toStdString()) return true;
        return false;
    };
    auto name = base;
    for (int i = 2; taken(name); ++i) name = base + " " + juce::String(i);
    return name;
}

juce::String BteqProcessor::addCustomPreset(const juce::String& wanted, const bteq::ParametricPreset& preset) {
    const juce::ScopedLock sl(buildLock);
    if (headphoneName.isEmpty()) return {};
    const auto name = uniqueCustomName(wanted, nullptr);
    customs->presets.push_back(makeCustom(headphoneName.toStdString(), name.toStdString(), preset));
    customs->save();
    const auto profs = profilesFor(headphoneName);
    profileIndex = (int)profs.size() - 1;
    matchName.clear();
    computeMatch();
    rebuild(false);
    return name;
}

void BteqProcessor::renameCustomPreset(int index, const juce::String& wanted) {
    const juce::ScopedLock sl(buildLock);
    const auto profs = profilesFor(headphoneName);
    if (index < 0 || index >= (int)profs.size() || !isCustom(profs[(size_t)index])) return;
    auto* c = const_cast<bteq::HeadphoneProfile*>(profs[(size_t)index]);
    const auto oldKey = profileKey(*c);
    c->target = uniqueCustomName(wanted, c).toStdString();
    const auto newKey = profileKey(*c);
    // Its own My EQ (per-target mode) follows the new name.
    if (auto it = myEqByTarget.find(oldKey); it != myEqByTarget.end()) {
        myEqByTarget[newKey] = it->second;
        myEqByTarget.erase(oldKey);
    }
    if (myEqKey == oldKey) myEqKey = newKey;
    customs->save();
}

void BteqProcessor::deleteCustomPreset(int index) {
    const juce::ScopedLock sl(buildLock);
    const auto profs = profilesFor(headphoneName);
    if (index < 0 || index >= (int)profs.size() || !isCustom(profs[(size_t)index])) return;
    const auto* doomed = profs[(size_t)index];
    if (index == profileIndex) profileIndex = 0;
    else if (index < profileIndex) --profileIndex;
    customs->presets.remove_if([doomed](const bteq::HeadphoneProfile& p) { return &p == doomed; });
    customs->save();
    builtProfile = nullptr;
    computeMatch();
    rebuild(false);
}

void BteqProcessor::writeCustom(juce::ValueTree& t) const {
    const auto* p = selectedProfile();
    if (!isCustom(p)) return;
    juce::ValueTree c("CUSTOM");
    c.setProperty("headphone", juce::String(p->name), nullptr);
    c.setProperty("name", juce::String(p->target), nullptr);
    c.setProperty("preamp", p->parametric.preampDb, nullptr);
    c.setProperty("filters", filtersToString(p->parametric.filters), nullptr);
    t.appendChild(c, nullptr);
}

void BteqProcessor::readCustom(const juce::ValueTree& t) {
    const auto c = t.getChildWithName("CUSTOM");
    if (!c.isValid()) return;
    const auto headphone = c.getProperty("headphone").toString().toStdString();
    const auto name = c.getProperty("name").toString().toStdString();
    if (headphone.empty() || name.empty() || customs->find(headphone, name) != nullptr) return;
    bteq::ParametricPreset preset;
    preset.preampDb = (double)c.getProperty("preamp", 0.0);
    preset.filters = filtersFromString(c.getProperty("filters").toString());
    customs->presets.push_back(makeCustom(headphone, name, preset));
    customs->save();
}

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
    standaloneUi = wrapperType == wrapperType_Standalone;
    startTimerHz(30);
}

BteqProcessor::~BteqProcessor() {
    stopTimer();
    if (customsDirty) customs->save();
}

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
    const auto& freqs = graphicFrequencies();

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
    curvesHolder.reset(); // everything needed is copied; free the curves unless a window is showing them
}

juce::String BteqProcessor::editKey() const {
    const auto* p = selectedProfile();
    if (p == nullptr) return {};
    return match.active ? "MATCH|" + headphoneName + "|" + matchName : profileKey(*p);
}

const bteq::HeadphoneProfile* BteqProcessor::selectedProfile() const {
    const auto profs = profilesFor(headphoneName);
    if (profs.empty()) return nullptr;
    return profs[static_cast<size_t>(juce::jlimit(0, (int)profs.size() - 1, profileIndex))];
}

bool BteqProcessor::graphicAvailable() const {
    if (match.active) return true;
    const auto* p = selectedProfile();
    return p != nullptr && p->hasGraphic() && !graphicFrequencies().empty();
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
    if (p->hasGraphic()) {
        float mx = p->graphicDb(0);
        for (size_t i = 1; i < p->graphicSize(); ++i) mx = std::max(mx, p->graphicDb(i));
        return mx;
    }
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
    shape.reserve(p->graphicSize());
    for (size_t i = 0; i < p->graphicSize(); ++i) shape.push_back((double)p->graphicDb(i) - pre);
    return shape;
}

bool BteqProcessor::selectionEdited() const {
    const auto* e = currentEdit();
    return e != nullptr && (e->filtersEdited || e->preampEdited);
}

void BteqProcessor::setFilter(int index, const bteq::FilterSpec& spec) {
    const juce::ScopedLock sl(buildLock);
    if (auto* c = selectedCustom()) { // your own preset: change it directly
        if (index < 0 || index >= (int)c->parametric.filters.size()) return;
        c->parametric.filters[(size_t)index] = spec;
        customsDirty = true;
        ++editVersion;
        rebuild(false);
        return;
    }
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
    if (auto* c = selectedCustom()) {
        c->parametric.preampDb = juce::jlimit(-30.0, 12.0, db);
        customsDirty = true;
        ++editVersion;
        rebuild(false);
        return;
    }
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
                                           { T::Peaking, 8000 }, { T::HighShelf, 10000 },
                                           // Added later; kept at the end so older My EQ settings keep their numbering.
                                           { T::Peaking, 60 }, { T::Peaking, 14000 } };
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
    myEqSettings.gainDb = 0;
    ++myEqVersion;
    rebuild(false);
}

void BteqProcessor::setMyEqPerTarget(bool perTarget) {
    const juce::ScopedLock sl(buildLock);
    if (perTarget == myEqPerTargetFlag) return;
    myEqPerTargetFlag = perTarget;
    // Either way the My EQ you hear now stays: switching on makes it this target's.
    myEqKey = editKey();
    rebuild(false);
}

void BteqProcessor::syncMyEq() {
    if (!myEqPerTargetFlag) return;
    const auto key = editKey();
    if (key == myEqKey) return;
    if (myEqKey.isNotEmpty()) myEqByTarget[myEqKey] = myEqSettings;
    const auto it = myEqByTarget.find(key);
    if (it != myEqByTarget.end()) {
        myEqSettings = it->second;
    } else {
        myEqSettings = {};
        myEqSettings.bands = defaultMyEqBands();
    }
    myEqKey = key;
    ++myEqVersion;
}

void BteqProcessor::setMyEqGain(double db) {
    const juce::ScopedLock sl(buildLock);
    myEqSettings.gainDb = juce::jlimit(-24.0, 12.0, db);
    myEqSettings.autoGain = false; // turning the knob takes over from automatic gain
    ++myEqVersion;
    rebuild(false);
}

void BteqProcessor::setMyEqAutoGain(bool on) {
    const juce::ScopedLock sl(buildLock);
    myEqSettings.autoGain = on;
    if (!on) myEqSettings.gainDb = myEqRecommended.load(); // keep the level where it was
    ++myEqVersion;
    rebuild(false);
}

bool BteqProcessor::myEqEdited() const {
    const juce::ScopedLock sl(buildLock);
    for (const auto& b : myEqSettings.bands)
        if (std::abs(b.gainDb) > 0.01 || !b.enabled) return true;
    return std::abs(myEqSettings.gainDb) > 0.01 || myEqSettings.autoGain;
}

bool BteqProcessor::myEqActive() const {
    const juce::ScopedLock sl(buildLock);
    if (!myEqSettings.on) return false;
    for (const auto& b : myEqSettings.bands)
        if (b.enabled && std::abs(b.gainDb) > 0.01) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Engine building (message thread, or prepareToPlay)
// ---------------------------------------------------------------------------
void BteqProcessor::timerCallback() {
    rebuild(false);
    freeAllRetired();
    // Your presets are written to disk once a drag has settled, not on every step.
    if (customsDirty && !juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) {
        customsDirty = false;
        customs->save();
    }
    updateApo();
    updateAnalyserSource();
}

// Standalone: while an analyser view is open and BEQ's own audio device has no
// input (it is closed while Equalizer APO does the EQ), listen to what Windows
// plays instead. Stops when the last view closes.
void BteqProcessor::updateAnalyserSource() {
    if (!standaloneUi) return;
    const bool wanted = spectrumTap.wanted();
    const auto now = juce::Time::getMillisecondCounter();
    if (wanted && now - loopbackCheck < 500 && loopback.running()) return; // the device check twice a second is plenty
    loopbackCheck = now;
    const bool listen = wanted && !standaloneAudioHasInput();
    if (listen && !loopback.running()) loopback.start(spectrumTap);
    else if (!listen && loopback.running()) loopback.stop();
}

std::vector<double> BteqProcessor::appliedResponseDb(const std::vector<double>& freqs, bool correction, bool myEq) const {
    std::vector<double> out(freqs.size(), 0.0);
    const juce::ScopedLock sl(buildLock);
    if (bypassParam->load() > 0.5f) return out;
    const double k = juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
    double preamp = correction ? (double)trimParam->load() : 0.0;
    std::vector<bteq::BiquadCoeffs> coeffs;
    constexpr double fs = 48000.0;
    if (const auto* p = correction ? selectedProfile() : nullptr) {
        if (graphicModeActive()) {
            const double pre = preampSettingDb();
            auto gains = graphicShapeDb();
            for (auto& g : gains) g = (g + pre) * k;
            const auto& gf = graphicFrequencies();
            for (size_t i = 0; i < freqs.size(); ++i) out[i] += bteq::graphicGainAt(gf, gains, freqs[i]);
        } else {
            juce::ignoreUnused(p);
            const auto preset = effectiveParametric();
            preamp += preset.preampDb * k;
            for (const auto& f : preset.filters)
                if (f.enabled) coeffs.push_back(bteq::BiquadCoeffs::make(f.type, f.fc, f.gainDb * k, f.q, fs));
        }
    }
    if (myEq && myEqSettings.on) {
        bool any = false;
        for (const auto& b : myEqSettings.bands)
            if (b.enabled && std::abs(b.gainDb) > 0.01) {
                any = true;
                coeffs.push_back(bteq::BiquadCoeffs::make(b.type, b.fc, b.gainDb, b.q, fs));
            }
        if (any || std::abs(myEqSettings.gainDb) > 0.001)
            preamp += myEqSettings.autoGain ? myEqRecommended.load() : myEqSettings.gainDb;
    }
    for (size_t i = 0; i < freqs.size(); ++i) {
        double db = preamp;
        for (const auto& c : coeffs) db += c.magnitudeDb(freqs[i], fs);
        out[i] += db;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Equalizer APO (standalone, message thread)
// ---------------------------------------------------------------------------
void BteqProcessor::setApoOutput(bool on) {
    apoWanted = on;
    apoProblem.clear();
    updateApo();
    updateAnalyserSource();
}

void BteqProcessor::updateApo() {
    if (!standaloneUi) return;
    if (apoWanted && !apo.running()) {
        juce::String error;
        if (!apo.start(error)) {
            apoWanted = false;
            apoProblem = error;
            return;
        }
        apoLastText.clear();
    } else if (!apoWanted && apo.running()) {
        apo.stop();
    }
    // While APO applies the EQ, the standalone needs no audio device. Holding
    // one open (ASIO or a small buffer on the same interface) can make
    // Windows' own audio stutter, so it is closed, and reopened when APO is off.
    // Checked for a few seconds after a change, since the app may open its
    // device just after restoring a session that had APO on.
    if (apo.running() != apoRunning.load()) apoDeviceCheckUntil = juce::Time::getMillisecondCounter() + 3000;
    apoRunning = apo.running();
    if ((juce::int32)(apoDeviceCheckUntil - juce::Time::getMillisecondCounter()) > 0) setStandaloneAudioOpen(!apo.running());
    if (!apo.running()) return;

    // A click (bypass, a filter switch, a new target) is written straight away.
    // While a knob is dragged, writes are spaced 200 ms apart so APO doesn't
    // reload its config many times a second; the final value always follows.
    const auto text = apoConfigText();
    const auto now = juce::Time::getMillisecondCounter();
    if (text == apoLastText) return;
    if (apoLastText.isNotEmpty() && now - apoLastWrite < 200) return;
    apoLastWrite = now;
    juce::String error;
    if (apo.write(text, error)) {
        apoLastText = text;
        apoProblem.clear();
    } else {
        apoProblem = error;
        apoLastText = text; // don't retry on every tick; the next change tries again
    }
}

// The same EQ the plugin applies, in Equalizer APO's own syntax: correction
// (scaled by the correction amount), then My EQ, with one preamp for all of
// it including the output trim.
juce::String BteqProcessor::apoConfigText() const {
    const juce::ScopedLock sl(buildLock);
    std::vector<std::string> lines;
    const auto* p = selectedProfile();
    lines.push_back("# Written by Born To EQ The World. Change the EQ in BEQ; edits here are overwritten.");
    if (p != nullptr)
        lines.push_back("# " + (headphoneName + " | " + juce::String(p->source) + (p->rig.empty() ? "" : " (" + juce::String(p->rig) + ")")
                                + " | " + juce::String(p->target) + (match.active ? " | sounds like " + matchName : "")).toStdString());
    if (bypassParam->load() > 0.5f) {
        lines.push_back("# Bypassed");
        return juce::String(bteq::apo::joinLines(lines));
    }
    const double k = juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
    double preamp = trimParam->load();
    std::vector<std::string> body;
    if (p != nullptr) {
        if (graphicModeActive()) {
            // The preamp is part of the curve, exactly as BEQ's graphic filter is built.
            const double pre = preampSettingDb();
            auto gains = graphicShapeDb();
            for (auto& g : gains) g = (g + pre) * k;
            body.push_back(bteq::apo::graphicLine(graphicFrequencies(), gains));
        } else {
            const auto preset = effectiveParametric();
            preamp += preset.preampDb * k;
            for (const auto& f : preset.filters)
                if (f.enabled) body.push_back(bteq::apo::filterLine(f, k));
        }
    }
    if (myEqSettings.on) {
        bool any = false;
        for (const auto& b : myEqSettings.bands)
            if (b.enabled && std::abs(b.gainDb) > 0.01) {
                if (!any) body.push_back("# My EQ");
                any = true;
                body.push_back(bteq::apo::filterLine(b, 1.0));
            }
        if (any || std::abs(myEqSettings.gainDb) > 0.001)
            preamp += myEqSettings.autoGain ? myEqRecommended.load() : myEqSettings.gainDb;
    }
    lines.push_back("Preamp: " + bteq::apo::num(preamp, 3) + " dB");
    lines.insert(lines.end(), body.begin(), body.end());
    return juce::String(bteq::apo::joinLines(lines));
}

void BteqProcessor::rebuild(bool force) {
    const juce::ScopedLock sl(buildLock);
    syncMyEq();
    if (force || myEqVersion != builtMyEqVersion || !juce::exactlyEqual(sampleRate, builtMyEqRate)) {
        buildMyEq(sampleRate);
        builtMyEqVersion = myEqVersion;
        builtMyEqRate = sampleRate;
    }

    const auto* p = selectedProfile();
    const double k = juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
    const bool graphic = graphicModeActive();
    if (!force && p == builtProfile && cacheVersion == builtCacheVersion && std::abs(k - builtAmount) < 1e-4 && juce::exactlyEqual(sampleRate, builtRate) &&
        graphic == builtGraphic && editVersion == builtEditVersion)
        return;

    if (graphic)
        buildGraphic(p, k, sampleRate);
    else
        buildParametric(p, k, sampleRate);

    builtProfile = p;
    builtCacheVersion = cacheVersion; // a new headphone's copy can reuse the same addresses
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
    const auto& freqs = graphicFrequencies();
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
            if (b.enabled && std::abs(b.gainDb) > 0.01) preset.filters.push_back(b);
    auto engine = std::make_unique<IirEngine>();
    // Recommended gain: down by its highest boost, so it can't clip more than
    // the correction alone. Used when automatic gain is on; otherwise the knob.
    engine->eq.prepare(preset, sr, 2, 1.0, /*autoPreamp*/ true);
    const double recommended = preset.filters.empty() ? 0.0 : engine->eq.getPreampDb();
    myEqRecommended = recommended;
    preset.preampDb = myEqSettings.on ? (myEqSettings.autoGain ? recommended : myEqSettings.gainDb) : 0.0;
    engine->eq.prepare(preset, sr, 2, 1.0, /*autoPreamp*/ false);
    myEqPreampDb = preset.preampDb;
    myEqRunning = !preset.filters.empty() || std::abs(preset.preampDb) > 0.001;
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

    // While Equalizer APO applies the EQ system-wide, the standalone's own path stays clean (no double EQ).
    const bool bypass = bypassParam->load() > 0.5f || apoRunning.load();
    const int chans = juce::jmin(buffer.getNumChannels(), 2);
    // The analyser, when one is showing: the input before any correction (or the output, below).
    const bool tapping = spectrumTap.wanted() && !spectrumTap.external.load(); // the loopback feeds it instead
    if (tapping && spectrumTap.where.load() == 0) spectrumTap.write(buffer.getArrayOfReadPointers(), chans, n);
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

    trimGain.setTargetValue(apoRunning.load() ? 1.0f : juce::Decibels::decibelsToGain(trimParam->load()));
    trimGain.applyGain(buffer, n);
    if (tapping && spectrumTap.where.load() == 1) spectrumTap.write(buffer.getArrayOfReadPointers(), chans, n);
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
    state.setProperty("analyser", analyserOn, nullptr);
    state.setProperty("hiddenCurves", hiddenCurves, nullptr);
    state.setProperty("uiScale", uiScale, nullptr);
    state.setProperty("match", matchName, nullptr);
    state.setProperty("editingMyEq", editingMyEq, nullptr);
    state.setProperty("sidebarCollapsed", sidebarCollapsed, nullptr);
    state.setProperty("headphoneCollapsed", headphoneCollapsed, nullptr);
    state.setProperty("targetCollapsed", targetCollapsed, nullptr);
    if (standaloneUi) state.setProperty("apoOutput", apoWanted, nullptr);
    {
        const juce::ScopedLock sl(buildLock);
        writeMyEq(state, myEqSettings);
        state.setProperty("myEqPerTarget", myEqPerTargetFlag, nullptr);
        juce::ValueTree others("MYEQS");
        for (const auto& [key, m] : myEqByTarget) {
            if (key == myEqKey) continue;
            juce::ValueTree t("MYEQ");
            t.setProperty("key", key, nullptr);
            writeMyEq(t, m);
            others.appendChild(t, nullptr);
        }
        state.appendChild(others, nullptr);
        juce::ValueTree list("EDITS");
        for (const auto& [key, e] : edits) {
            if (!e.filtersEdited && !e.preampEdited) continue;
            juce::ValueTree t("EDIT");
            t.setProperty("key", key, nullptr);
            writeEdit(t, e);
            list.appendChild(t, nullptr);
        }
        state.appendChild(list, nullptr);
        writeCustom(state);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void BteqProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    const auto list = state.getChildWithName("EDITS");
    state.removeChild(list, nullptr);
    const auto otherMyEqs = state.getChildWithName("MYEQS");
    state.removeChild(otherMyEqs, nullptr);
    readCustom(state);
    state.removeChild(state.getChildWithName("CUSTOM"), nullptr);
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
    analyserOn = (bool)state.getProperty("analyser", false);
    hiddenCurves = (int)state.getProperty("hiddenCurves", 0);
    uiScale = (float)state.getProperty("uiScale", 0.0);
    editingMyEq = (bool)state.getProperty("editingMyEq", false);
    sidebarCollapsed = (bool)state.getProperty("sidebarCollapsed", false);
    headphoneCollapsed = (bool)state.getProperty("headphoneCollapsed", false);
    targetCollapsed = (bool)state.getProperty("targetCollapsed", false);
    readMyEq(state);
    if (standaloneUi) apoWanted = (bool)state.getProperty("apoOutput", false); // started by the timer
    profileIndex = indexForKey(headphoneName, state.getProperty("profile").toString());
    {
        const juce::ScopedLock sl(buildLock);
        matchName = state.getProperty("match").toString();
        computeMatch();
        myEqPerTargetFlag = (bool)state.getProperty("myEqPerTarget", false);
        myEqByTarget.clear();
        for (const auto& t : otherMyEqs)
            myEqByTarget[t.getProperty("key").toString()] = parseMyEq(t);
        myEqKey = editKey(); // the My EQ just read belongs to the selected target
    }
    rebuild(true);
    releaseLibraryIfUnused(); // a session loaded with no BEQ window open
}

int BteqProcessor::indexForKey(const juce::String& headphone, const juce::String& key) const {
    const auto profs = profilesFor(headphone);
    for (size_t i = 0; i < profs.size(); ++i)
        if (profileKey(*profs[i]) == key) return (int)i;
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
    writeMyEq(t, myEq());
    writeCustom(t);
    return t;
}

void BteqProcessor::loadSnapshot(const juce::ValueTree& t) {
    auto setParam = [this](const juce::String& id, float value) {
        if (auto* prm = params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(value));
    };
    setParam(kMode, (float)(int)t.getProperty("mode", 0));
    setParam(kAmount, (float)(double)t.getProperty("amount", 100.0));
    const juce::ScopedLock sl(buildLock);
    readCustom(t);
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
    syncMyEq(); // per target: put away the old target's My EQ before taking this one's
    if (myEqPerTargetFlag) readMyEq(t); // shared: My EQ carries over unchanged
    ++editVersion;
    rebuild(false);
}

// Reads My EQ from a session or saved profile; older ones without it leave it as it is.
BteqProcessor::MyEq BteqProcessor::parseMyEq(const juce::ValueTree& t) {
    MyEq m;
    m.bands = filtersFromString(t.getProperty("myEq").toString());
    const auto defaults = defaultMyEqBands();
    if (m.bands.empty() || m.bands.size() > defaults.size()) m.bands = defaults;
    // Older settings had fewer bands: keep them exactly and add the new ones, flat, after them.
    for (size_t i = m.bands.size(); i < defaults.size(); ++i) m.bands.push_back(defaults[i]);
    m.on = (bool)t.getProperty("myEqOn", true);
    if (t.hasProperty("myEqGain")) {
        m.gainDb = (double)t.getProperty("myEqGain");
        m.autoGain = (bool)t.getProperty("myEqAutoGain", false);
    } else {
        // Saved before the gain knob, when the gain was always automatic:
        // set the knob to what that was, so it sounds exactly the same.
        bteq::ParametricPreset preset;
        for (const auto& b : m.bands)
            if (b.enabled && std::abs(b.gainDb) > 0.01) preset.filters.push_back(b);
        if (!preset.filters.empty()) {
            bteq::ParametricEq eq;
            eq.prepare(preset, 48000.0, 1, 1.0, true);
            m.gainDb = eq.getPreampDb();
        }
    }
    return m;
}

void BteqProcessor::writeMyEq(juce::ValueTree& t, const MyEq& m) {
    t.setProperty("myEq", filtersToString(m.bands), nullptr);
    t.setProperty("myEqOn", m.on, nullptr);
    t.setProperty("myEqGain", m.gainDb, nullptr);
    t.setProperty("myEqAutoGain", m.autoGain, nullptr);
}

void BteqProcessor::readMyEq(const juce::ValueTree& t) {
    if (!t.hasProperty("myEq")) return;
    const juce::ScopedLock sl(buildLock);
    myEqSettings = parseMyEq(t);
    ++myEqVersion;
}

juce::AudioProcessorEditor* BteqProcessor::createEditor() { return new BteqEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BteqProcessor(); }
