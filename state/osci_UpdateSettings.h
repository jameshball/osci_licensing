#pragma once

namespace osci {

class UpdateSettings final
{
public:
    explicit UpdateSettings (juce::String productSlug);
    UpdateSettings (juce::String productSlug, osci::SettingsStore store);

    bool betaUpdatesEnabled() const;

    ReleaseTrack releaseTrack() const;
    void setReleaseTrack (ReleaseTrack track);
    void useStableTrack();

    bool isDismissed (juce::StringRef semver,
                      double nowSeconds,
                      double cooldownSeconds = 48.0 * 60.0 * 60.0) const;
    void dismiss (juce::StringRef semver, double nowSeconds);
    void clearDismissal();

    juce::File getSettingsFile() const;

private:
    juce::String productSlug;
    mutable osci::SettingsStore settings;

    juce::String key (juce::StringRef leaf) const;
    static ReleaseTrack releaseTrackFromString (juce::StringRef text);
};

} // namespace osci
