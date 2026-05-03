#pragma once

namespace osci
{

class HardwareInfo final
{
public:
    static juce::String getCurrentPlatform();
    static juce::String getMachineFingerprint();
    static juce::File getDefaultStorageDirectory (juce::StringRef productSlug);

private:
    HardwareInfo() = delete;
};

} // namespace osci
