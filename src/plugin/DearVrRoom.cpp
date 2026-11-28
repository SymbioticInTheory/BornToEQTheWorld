#include "DearVrRoom.h"

juce::AudioProcessorParameter* DearVrRoom::findParameter(juce::AudioPluginInstance& plugin, const juce::String& name) {
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(128).equalsIgnoreCase(name))
            return parameter;
    return nullptr;
}

juce::String DearVrRoom::select(const juce::String& scene) {
    if (scene.isEmpty()) {
        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock lock(owner.getCallbackLock());
            if (prepared) instance->releaseResources();
            prepared = false;
            old = std::move(instance);
            sceneParameter = nullptr;
            sceneNames.clear();
        }
        updateTiming();
        return {};
    }

    if (instance != nullptr) {
        const int index = sceneNames.indexOf(scene, true);
        if (index < 0) return "dearVR MIX does not expose the scene " + scene + ".";
        sceneParameter->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(sceneNames.size() - 1));
        return {};
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    const auto locations = format.getDefaultLocationsToSearch();
    for (int i = 0; i < locations.getNumPaths() && types.isEmpty(); ++i) {
        const auto file = locations[i].getChildFile("dearVR MIX.vst3");
        if (file.exists()) format.findAllTypesForFile(types, file.getFullPathName());
    }
    const juce::PluginDescription* type = nullptr;
    for (const auto* candidate : types)
        if (candidate->name.equalsIgnoreCase("dearVR MIX") && candidate->manufacturerName.equalsIgnoreCase("Dear Reality"))
            type = candidate;
    if (type == nullptr) return "An installed dearVR MIX VST3 was not found in the standard VST3 folders.";

    double initialRate;
    int initialBlock;
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        initialRate = rate > 0 ? rate : 48000.0;
        initialBlock = blockSize > 0 ? blockSize : 512;
    }
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*type, initialRate, initialBlock, error);
    if (plugin == nullptr) return error.isNotEmpty() ? error : "Could not open dearVR MIX.";
    const auto stereo = juce::AudioChannelSet::stereo();
    if (plugin->getMainBusNumInputChannels() != 2 || plugin->getMainBusNumOutputChannels() != 2
        || plugin->getTotalNumInputChannels() != 2 || plugin->getTotalNumOutputChannels() != 2
        || plugin->getChannelLayoutOfBus(true, 0) != stereo || plugin->getChannelLayoutOfBus(false, 0) != stereo)
        return "This dearVR MIX instance did not open with one stereo input and output.";

    auto* headphoneCompensation = findParameter(*plugin, "HPC Enable");
    auto* sceneChoice = findParameter(*plugin, "Scenes");
    if (sceneChoice == nullptr) sceneChoice = findParameter(*plugin, "Scene");
    if (headphoneCompensation == nullptr || sceneChoice == nullptr)
        return "This dearVR MIX build does not expose the headphone compensation and scene controls.";
    const int steps = sceneChoice->getNumSteps();
    if (steps < 2 || steps > 64) return "This dearVR MIX build does not expose a list of scenes.";

    juce::StringArray names;
    for (int i = 0; i < steps; ++i)
        names.add(sceneChoice->getText(static_cast<float>(i) / static_cast<float>(steps - 1), 100));
    const int selected = names.indexOf(scene, true);
    if (selected < 0) return "dearVR MIX does not expose the scene " + scene + ".";

    headphoneCompensation->setValueNotifyingHost(0.0f);
    if (auto* bypass = findParameter(*plugin, "Internal Bypass")) bypass->setValueNotifyingHost(0.0f);
    sceneChoice->setValueNotifyingHost(static_cast<float>(selected) / static_cast<float>(steps - 1));

    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        instance = std::move(plugin);
        sceneParameter = sceneChoice;
        sceneNames = std::move(names);
        prepareInstance();
    }
    updateTiming();
    return {};
}

void DearVrRoom::prepareInstance() {
    if (instance == nullptr || !useStereo || blockSize <= 0 || rate <= 0.0) return;
    instance->setNonRealtime(owner.isNonRealtime());
    instance->setPlayHead(owner.getPlayHead());
    instance->prepareToPlay(rate, blockSize);
    prepared = true;
}

void DearVrRoom::prepare(double sampleRate, int maximumBlockSize, bool stereo) {
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (prepared) instance->releaseResources();
        prepared = false;
        rate = sampleRate;
        blockSize = maximumBlockSize;
        useStereo = stereo;
        prepareInstance();
    }
    updateTiming();
}

void DearVrRoom::release() {
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (prepared) instance->releaseResources();
        prepared = false;
        useStereo = false;
        rate = 0.0;
        blockSize = 0;
    }
    updateTiming();
}

void DearVrRoom::process(juce::AudioBuffer<float>& buffer) {
    // The JUCE VST3 wrapper and the standalone player already hold the parent's callback lock.
    if (!prepared || buffer.getNumChannels() != 2 || buffer.getNumSamples() == 0) return;
    instance->setNonRealtime(owner.isNonRealtime());
    instance->setPlayHead(owner.getPlayHead());
    if (buffer.getNumSamples() <= blockSize) {
        midi.clear();
        instance->processBlock(buffer, midi);
        return;
    }
    for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize) {
        const int count = juce::jmin(blockSize, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> segment(buffer.getArrayOfWritePointers(), 2, offset, count);
        midi.clear();
        instance->processBlock(segment, midi);
    }
}

void DearVrRoom::updateTiming() {
    int latency = 0;
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        if (prepared) latency = instance->getLatencySamples();
        tail.store(prepared ? static_cast<float>(instance->getTailLengthSeconds()) : 0.0f);
    }
    if (owner.getLatencySamples() != latency) owner.setLatencySamples(latency);
}
