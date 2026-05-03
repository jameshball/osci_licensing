#pragma once

namespace osci {

struct DetectedDawProcess {
    juce::String processName;
    juce::String displayName;
};

class DawProcessDetector final {
public:
    using ScanCallback = std::function<void (juce::Array<DetectedDawProcess>)>;

    static juce::Array<DetectedDawProcess> scan();
    static void scanAsync (ScanCallback callback);
    static bool isKnownDawProcessName (juce::StringRef processName, juce::String* matchedDisplayName = nullptr);
    static juce::String joinDisplayNames (const juce::Array<DetectedDawProcess>& processes);

private:
    DawProcessDetector() = delete;
};

} // namespace osci
