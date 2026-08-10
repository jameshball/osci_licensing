#pragma once

namespace osci {

struct LinuxInstallLocations {
    juce::File standaloneDirectory;
    juce::File vst3Directory;

    bool operator== (const LinuxInstallLocations&) const = default;
};

class LinuxInstallSettings final {
public:
    explicit LinuxInstallSettings (juce::String productSlug);
    LinuxInstallSettings (juce::String productSlug, SettingsStore store);

    juce::Result load (LinuxInstallLocations& locations) const;
    juce::Result loadSaved (std::optional<LinuxInstallLocations>& locations) const;
    juce::Result save (const LinuxInstallLocations& locations);
    juce::Result clear();

    static LinuxInstallLocations defaults();
    static juce::String standaloneKey (juce::StringRef productSlug);
    static juce::String vst3Key (juce::StringRef productSlug);

private:
    juce::String product;
    SettingsStore settings;
};

} // namespace osci
