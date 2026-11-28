#include "DearVrRoom.h"

juce::AudioProcessorParameter* DearVrRoom::findParameter(juce::AudioPluginInstance& plugin, const juce::String& name) {
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(128).equalsIgnoreCase(name))
            return parameter;
    return nullptr;
}

std::unique_ptr<juce::AudioPluginInstance> DearVrRoom::openInstance(double sampleRate, int block, juce::String& error) {
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
    if (type == nullptr) { error = "An installed dearVR MIX VST3 was not found in the standard VST3 folders."; return nullptr; }

    auto plugin = format.createInstanceFromDescription(*type, sampleRate, block, error);
    if (plugin == nullptr) { if (error.isEmpty()) error = "Could not open dearVR MIX."; return nullptr; }
    const auto stereo = juce::AudioChannelSet::stereo();
    if (plugin->getMainBusNumInputChannels() != 2 || plugin->getMainBusNumOutputChannels() != 2
        || plugin->getTotalNumInputChannels() != 2 || plugin->getTotalNumOutputChannels() != 2
        || plugin->getChannelLayoutOfBus(true, 0) != stereo || plugin->getChannelLayoutOfBus(false, 0) != stereo) {
        error = "This dearVR MIX instance did not open with one stereo input and output.";
        return nullptr;
    }
    error = {};
    return plugin;
}

juce::String DearVrRoom::describeParameters(juce::String& error) {
    auto plugin = openInstance(48000.0, 512, error);
    if (plugin == nullptr) return {};
    juce::String out;
    out << "dearVR MIX " << plugin->getPluginDescription().version << ", "
        << plugin->getParameters().size() << " settings\n";
    int index = 0;
    for (auto* parameter : plugin->getParameters()) {
        const float value = parameter->getValue();
        const int steps = parameter->getNumSteps();
        const bool listed = steps >= 2 && steps <= 200;
        out << index++ << "\t" << parameter->getName(128) << "\t" << parameter->getLabel()
            << "\tsteps " << (listed || parameter->isDiscrete() ? juce::String(steps) : juce::String("continuous"))
            << "\tnow " << parameter->getText(value, 100) << " (" << juce::String(value, 4) << ")";
        if (listed) {
            juce::StringArray texts;
            for (int i = 0; i < steps; ++i)
                texts.add(parameter->getText(static_cast<float>(i) / static_cast<float>(steps - 1), 100));
            out << "\tvalues " << texts.joinIntoString(" | ");
        }
        out << "\n";
    }
    return out;
}

juce::AudioFormatWriterOptions DearVrRoom::floatWav(double sampleRate, int channels) {
    return juce::AudioFormatWriterOptions{}
        .withSampleRate(sampleRate)
        .withNumChannels(channels)
        .withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
}

juce::File DearVrRoom::folder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Born To EQ The World").getChildFile("dearVR rooms");
}

juce::File DearVrRoom::responseFile(const juce::String& scene, double sampleRate) {
    const auto safe = juce::File::createLegalFileName(scene);
    return folder().getChildFile(safe + " " + juce::String(juce::roundToInt(sampleRate)) + ".wav");
}

void DearVrRoom::rememberScenes(const juce::StringArray& names) {
    folder().createDirectory();
    folder().getChildFile("scenes.txt").replaceWithText(names.joinIntoString("\n"));
}

juce::StringArray DearVrRoom::scenes() const {
    juce::StringArray names;
    names.addLines(folder().getChildFile("scenes.txt").loadFileAsString());
    names.removeEmptyStrings();
    return names;
}

// Records the room: an impulse into the left input, then one into the right,
// with dearVR's own latency taken off so the room starts where the sound does.
bool DearVrRoom::capture(const juce::String& scene, double sampleRate, juce::AudioBuffer<float>& ir, juce::String& error) {
    constexpr int block = 1024;
    auto plugin = openInstance(sampleRate, block, error);
    if (plugin == nullptr) return false;

    auto* headphoneCompensation = findParameter(*plugin, "HPC Enable");
    auto* sceneChoice = findParameter(*plugin, "Scenes");
    if (sceneChoice == nullptr) sceneChoice = findParameter(*plugin, "Scene");
    if (headphoneCompensation == nullptr || sceneChoice == nullptr) {
        error = "This dearVR MIX build does not expose the headphone compensation and scene controls.";
        return false;
    }
    const int steps = sceneChoice->getNumSteps();
    if (steps < 2 || steps > 64) { error = "This dearVR MIX build does not expose a list of scenes."; return false; }
    juce::StringArray names;
    for (int i = 0; i < steps; ++i)
        names.add(sceneChoice->getText(static_cast<float>(i) / static_cast<float>(steps - 1), 100));
    rememberScenes(names);
    const int selected = names.indexOf(scene, true);
    if (selected < 0) { error = "dearVR MIX does not have the scene " + scene + "."; return false; }

    // BEQ does the headphone correction, so dearVR's own is off.
    headphoneCompensation->setValueNotifyingHost(0.0f);
    if (auto* bypass = findParameter(*plugin, "Internal Bypass")) bypass->setValueNotifyingHost(0.0f);
    if (auto* bypass = findParameter(*plugin, "Bypass")) bypass->setValueNotifyingHost(0.0f);
    sceneChoice->setValueNotifyingHost(static_cast<float>(selected) / static_cast<float>(steps - 1));

    plugin->setNonRealtime(true);
    plugin->prepareToPlay(sampleRate, block);
    const int latency = juce::jmax(0, plugin->getLatencySamples());
    const int length = juce::roundToInt(sampleRate * 3.0); // the longest room we keep
    const int settle = juce::roundToInt(sampleRate * 1.0);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);

    // Runs `samples` of input through dearVR; `impulse` < 0 is silence.
    auto run = [&](int samples, int impulseChannel, float amplitude, juce::AudioBuffer<float>* out) {
        if (out != nullptr) out->setSize(2, samples);
        for (int pos = 0; pos < samples; pos += block) {
            const int n = juce::jmin(block, samples - pos);
            juce::AudioBuffer<float> part(buf.getArrayOfWritePointers(), 2, 0, n);
            part.clear();
            if (pos == 0 && impulseChannel >= 0) part.setSample(impulseChannel, 0, amplitude);
            midi.clear();
            plugin->processBlock(part, midi);
            if (out != nullptr)
                for (int c = 0; c < 2; ++c) out->copyFrom(c, pos, part, c, 0, n);
        }
    };

    constexpr float amplitude = 0.25f;
    juce::AudioBuffer<float> fromLeft, fromRight, check;
    run(settle, -1, 0.0f, nullptr); // scene changes glide; let them finish
    run(latency + length, 0, amplitude, &fromLeft);
    run(settle, -1, 0.0f, nullptr);
    run(latency + length, 1, amplitude, &fromRight);
    run(settle, -1, 0.0f, nullptr);
    run(latency + length, 0, amplitude * 0.25f, &check); // the same again, quieter
    plugin->releaseResources();

    // A room that changes over time, or with level, can't be captured this way.
    double energy = 0.0, difference = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < fromLeft.getNumSamples(); ++i) {
            const double a = fromLeft.getSample(c, i), b = 4.0 * check.getSample(c, i);
            energy += a * a;
            difference += (a - b) * (a - b);
        }
    if (energy < 1e-12) { error = "dearVR MIX gave no sound back for " + scene + "."; return false; }
    if (difference > energy * 1e-4) { // -40 dB
        error = "dearVR's " + scene + " room changes over time or with level, so it can't be recorded as a fixed room.";
        return false;
    }

    // Trim the silent end: last point above -100 dB of the peak, then a 10 ms fade.
    float peak = 0.0f;
    for (auto* b : { &fromLeft, &fromRight })
        for (int c = 0; c < 2; ++c) peak = juce::jmax(peak, b->getMagnitude(c, latency, length));
    int end = latency + 1;
    for (auto* b : { &fromLeft, &fromRight })
        for (int c = 0; c < 2; ++c) {
            const float* d = b->getReadPointer(c);
            for (int i = latency + length - 1; i >= end; --i)
                if (std::abs(d[i]) > peak * 1e-5f) { end = i + 1; break; }
        }
    const int fade = juce::roundToInt(sampleRate * 0.01);
    const int count = juce::jmin(latency + length, end + fade) - latency;
    ir.setSize(4, count);
    const juce::AudioBuffer<float>* sources[] = { &fromLeft, &fromLeft, &fromRight, &fromRight };
    for (int c = 0; c < 4; ++c) {
        ir.copyFrom(c, 0, *sources[c], c % 2, latency, count);
        ir.applyGain(c, 0, count, 1.0f / amplitude);
        const int fadeStart = juce::jmax(0, count - fade);
        ir.applyGainRamp(c, fadeStart, count - fadeStart, 1.0f, 0.0f);
    }
    return true;
}

bool DearVrRoom::responses(const juce::String& scene, double sampleRate, juce::AudioBuffer<float>& ir, juce::String& error) {
    const auto file = responseFile(scene, sampleRate);
    juce::WavAudioFormat wav;
    if (auto stream = file.createInputStream()) {
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
        if (reader != nullptr && reader->numChannels == 4 && reader->lengthInSamples > 0
            && std::abs(reader->sampleRate - sampleRate) < 1.0) {
            ir.setSize(4, (int)reader->lengthInSamples);
            if (reader->read(&ir, 0, (int)reader->lengthInSamples, 0, true, true)) return true;
        }
    }
    if (!capture(scene, sampleRate, ir, error)) return false;
    folder().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    if (out != nullptr)
        if (auto writer = wav.createWriterFor(out, floatWav(sampleRate, 4)))
            writer->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
    return true; // a failed save only means it is recorded again next time
}

void DearVrRoom::Engine::prepare(double sampleRate, int block) {
    blockSize = juce::jmax(1, block);
    for (auto* c : { &fromLeft, &fromRight }) c->reset();
    left.setSize(2, blockSize);
    right.setSize(2, blockSize);
    fromLeft.prepare({ sampleRate, (juce::uint32)blockSize, 2 });
    fromRight.prepare({ sampleRate, (juce::uint32)blockSize, 2 });
}

void DearVrRoom::Engine::process(juce::AudioBuffer<float>& buffer) {
    for (int pos = 0; pos < buffer.getNumSamples(); pos += blockSize) {
        const int n = juce::jmin(blockSize, buffer.getNumSamples() - pos);
        for (int c = 0; c < 2; ++c) {
            left.copyFrom(c, 0, buffer, 0, pos, n);
            right.copyFrom(c, 0, buffer, 1, pos, n);
        }
        juce::dsp::AudioBlock<float> l(left.getArrayOfWritePointers(), 2, (size_t)n), r(right.getArrayOfWritePointers(), 2, (size_t)n);
        fromLeft.process(juce::dsp::ProcessContextReplacing<float>(l));
        fromRight.process(juce::dsp::ProcessContextReplacing<float>(r));
        for (int c = 0; c < 2; ++c) {
            buffer.copyFrom(c, pos, left, c, 0, n);
            buffer.addFrom(c, pos, right, c, 0, n);
        }
    }
}

juce::String DearVrRoom::select(const juce::String& scene) {
    std::unique_ptr<Engine> next;
    if (scene.isNotEmpty()) {
        double sampleRate;
        int block;
        {
            const juce::ScopedLock lock(owner.getCallbackLock());
            sampleRate = rate > 0 ? rate : 48000.0;
            block = blockSize > 0 ? blockSize : 512;
        }
        juce::AudioBuffer<float> ir;
        juce::String error;
        if (!responses(scene, sampleRate, ir, error)) return error;
        next = std::make_unique<Engine>();
        next->irRate = sampleRate;
        next->left.setSize(2, ir.getNumSamples());
        next->right.setSize(2, ir.getNumSamples());
        for (int c = 0; c < 2; ++c) {
            next->left.copyFrom(c, 0, ir, c, 0, ir.getNumSamples());      // LL, LR
            next->right.copyFrom(c, 0, ir, 2 + c, 0, ir.getNumSamples()); // RL, RR
        }
        using C = juce::dsp::Convolution;
        // Load first, then prepare: prepare runs the queued load right here.
        next->fromLeft.loadImpulseResponse(juce::AudioBuffer<float>(next->left), sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        next->fromRight.loadImpulseResponse(juce::AudioBuffer<float>(next->right), sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        next->prepare(sampleRate, block);
        tail.store((float)ir.getNumSamples() / (float)sampleRate);
    } else {
        tail.store(0.0f);
    }
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        std::swap(engine, next);
    }
    return {}; // the old engine is freed here, off the audio thread
}

bool DearVrRoom::needsRecapture() const {
    const juce::ScopedLock lock(owner.getCallbackLock());
    return engine != nullptr && rate > 0 && std::abs(engine->irRate - rate) > 1.0;
}

void DearVrRoom::prepare(double sampleRate, int maximumBlockSize, bool stereo) {
    const juce::ScopedLock lock(owner.getCallbackLock());
    rate = sampleRate;
    blockSize = maximumBlockSize;
    useStereo = stereo;
    // A new sample rate plays the old recording resampled until it is recorded again.
    if (engine != nullptr) engine->prepare(sampleRate, maximumBlockSize);
}

void DearVrRoom::release() {
    const juce::ScopedLock lock(owner.getCallbackLock());
    useStereo = false;
}

void DearVrRoom::process(juce::AudioBuffer<float>& buffer) {
    // The JUCE VST3 wrapper and the standalone player already hold the parent's callback lock.
    if (engine == nullptr || !useStereo || buffer.getNumChannels() != 2 || buffer.getNumSamples() == 0) return;
    engine->process(buffer);
}

void DearVrRoom::updateTiming() {
    // Convolution adds no latency.
}
