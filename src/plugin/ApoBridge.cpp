#include "ApoBridge.h"
#include "ApoConfig.h"

juce::File ApoBridge::findConfigDir() {
    if (const auto env = juce::SystemStats::getEnvironmentVariable("BTEQ_APO_CONFIG_DIR", {}); env.isNotEmpty()) {
        const juce::File d(env);
        return d.getChildFile("config.txt").existsAsFile() ? d : juce::File();
    }
#if JUCE_WINDOWS
    for (const auto mode : { juce::WindowsRegistry::WoW64_64bit, juce::WindowsRegistry::WoW64_Default }) {
        const auto path = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO\\ConfigPath", {}, mode);
        if (path.isNotEmpty() && juce::File::isAbsolutePath(path) && juce::File(path).getChildFile("config.txt").existsAsFile())
            return juce::File(path);
    }
    const auto programFiles = juce::SystemStats::getEnvironmentVariable("ProgramW6432", "C:\\Program Files");
    const auto fallback = juce::File(programFiles).getChildFile("EqualizerAPO").getChildFile("config");
    if (fallback.getChildFile("config.txt").existsAsFile()) return fallback;
#endif
    return {};
}

bool ApoBridge::replaceFile(const juce::File& target, const juce::String& text, juce::String& error) {
    // Written next to the target and swapped in, so APO never reads half a file.
    juce::TemporaryFile temp(target);
    if (!temp.getFile().replaceWithText(text, false, false, nullptr) || !temp.overwriteTargetFileWithTemporary()) {
        error = "Could not write " + target.getFullPathName() + ". Check that BEQ may change files in Equalizer APO's config folder.";
        return false;
    }
    return true;
}

bool ApoBridge::start(juce::String& error) {
    if (lock == nullptr) {
        auto l = std::make_unique<juce::InterProcessLock>("BornToEQTheWorld-EqualizerAPO");
        if (!l->enter(0)) {
            error = "Another BEQ window is already sending its EQ to Equalizer APO. Turn it off there (or close that BEQ) first.";
            return false;
        }
        lock = std::move(l);
    }
    if (startLocked(error)) return true;
    lock->exit(); // couldn't start: let another BEQ have it
    lock.reset();
    return false;
}

bool ApoBridge::startLocked(juce::String& error) {
    dir = findConfigDir();
    if (dir == juce::File()) {
        error = "Equalizer APO was not found. Install it (and pick your headphone output in its Device Selector), then try again.";
        return false;
    }
    const auto config = dir.getChildFile("config.txt");
    const auto old = config.loadFileAsString().toStdString();
    // Our file first, so the include never points at a missing file.
    if (!dir.getChildFile(bteq::apo::kFileName).existsAsFile()
        && !replaceFile(dir.getChildFile(bteq::apo::kFileName), "# Born To EQ The World\r\n", error))
        return false;
    if (!bteq::apo::isTakenOver(old)) {
        config.copyFileTo(dir.getChildFile("config.txt.beq-backup"));
        if (!replaceFile(config, bteq::apo::takeOver(old), error)) return false;
    } else if (const auto updated = bteq::apo::takeOver(old); updated != old) {
        // Lines added since (by Peace, say) are switched off too.
        if (!replaceFile(config, updated, error)) return false;
    }
    on = true;
    return true;
}

void ApoBridge::stop() {
    if (!on) return;
    on = false;
    struct Release { std::unique_ptr<juce::InterProcessLock>& l; ~Release() { if (l) { l->exit(); l.reset(); } } } release { lock };
    juce::String ignored;
    const auto config = dir.getChildFile("config.txt");
    replaceFile(config, bteq::apo::restore(config.loadFileAsString().toStdString()), ignored);
    replaceFile(dir.getChildFile(bteq::apo::kFileName), "# Born To EQ The World is off\r\n", ignored);
}

bool ApoBridge::write(const juce::String& text, juce::String& error) {
    if (!on) return false;
    return replaceFile(dir.getChildFile(bteq::apo::kFileName), text, error);
}
