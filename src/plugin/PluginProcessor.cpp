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
// Engine building (message thread, or prepareToPlay)
// ---------------------------------------------------------------------------
void BteqProcessor::timerCallback() { rebuild(false); }

void BteqProcessor::rebuild(bool force) {
    const juce::ScopedLock sl(buildLock);
    const auto* p = selectedProfile();
    const double k = juce::jlimit(0.0, 1.0, (double)amountParam->load() / 100.0);
    const bool graphic = graphicModeActive();
    if (!force && p == builtProfile && std::abs(k - builtAmount) < 1e-4 && juce::exactlyEqual(sampleRate, builtRate) &&
        graphic == builtGraphic)
        return;

    if (graphic)
        buildGraphic(p, k, sampleRate);
    else
        buildParametric(p, k, sampleRate);

    builtProfile = p;
    builtAmount = k;
    builtRate = sampleRate;
    builtGraphic = graphic;
}

void BteqProcessor::buildParametric(const bteq::HeadphoneProfile* p, double k, double sr) {
    auto eq = std::make_unique<bteq::ParametricEq>();
    bteq::ParametricPreset preset; // flat when nothing is selected
    if (p != nullptr && p->hasParametric) preset = p->parametric;
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

void BteqProcessor::buildGraphic(const bteq::HeadphoneProfile* p, double k, double sr) {
    const auto& freqs = holder->library.graphicFrequencies();
    std::vector<double> gains(p->graphicGainsDb.begin(), p->graphicGainsDb.end());
    const auto taps = bteq::designGraphicEqFir(freqs, bteq::scaleGraphicGains(gains, k), sr);
    juce::AudioBuffer<float> ir(1, (int)taps.size());
    std::copy(taps.begin(), taps.end(), ir.getWritePointer(0));
    convolution.loadImpulseResponse(std::move(ir), sr, juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    preampDb = 0; // AutoEQ's GraphicEQ curves already include their headroom
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
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void BteqProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    headphoneName = state.getProperty("headphone").toString();
    accentIndex = (int)state.getProperty("accent", 0);
    profileIndex = 0;
    const juce::String key = state.getProperty("profile").toString();
    if (const auto idx = holder->library.headphoneIndex(headphoneName.toStdString())) {
        const auto profs = holder->library.profilesFor(*idx);
        for (size_t i = 0; i < profs.size(); ++i)
            if (profileKey(*profs[i]) == key) profileIndex = (int)i;
    }
    rebuild(true);
}

juce::AudioProcessorEditor* BteqProcessor::createEditor() { return new BteqEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BteqProcessor(); }
