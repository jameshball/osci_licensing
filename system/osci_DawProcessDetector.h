#pragma once

namespace osci {

struct DetectedDawProcess {
    juce::String processName;
    juce::String displayName;
};

class DawProcessDetector final {
public:
    static juce::Array<DetectedDawProcess> scan();
    static bool isKnownDawProcessName (juce::StringRef processName, juce::String* matchedDisplayName = nullptr);
    static juce::String joinDisplayNames (const juce::Array<DetectedDawProcess>& processes);

private:
    DawProcessDetector() = delete;
};

} // namespace osci
