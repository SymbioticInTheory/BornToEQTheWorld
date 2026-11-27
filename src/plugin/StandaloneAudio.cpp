// Lets the standalone app close or reopen its own audio device. In the plugin
// formats there is no standalone holder, so this does nothing.
#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

void setStandaloneAudioOpen(bool open) {
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
        auto& dm = holder->deviceManager;
        const bool isOpen = dm.getCurrentAudioDevice() != nullptr;
        if (!open && isOpen) {
            dm.closeAudioDevice(); // the chosen device stays saved for when it reopens
        } else if (open && !isOpen) {
            const auto setup = dm.getAudioDeviceSetup();
            if (setup.outputDeviceName.isNotEmpty() || setup.inputDeviceName.isNotEmpty()) dm.restartLastAudioDevice();
        }
    }
#else
    juce::ignoreUnused(open);
#endif
}

// Whether the standalone's own audio device is open with an input switched on.
bool standaloneAudioHasInput() {
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            return device->getActiveInputChannels().countNumberOfSetBits() > 0;
#endif
    return false;
}
