#pragma once

namespace osci
{

struct PendingInstallMarker
{
    juce::String product;
    juce::String previousVersion;
    juce::String targetVersion;
    juce::String releaseTrack;
    juce::String variant;
    juce::String platform;
    juce::String artifactKind;
    juce::String artifactPath;
    juce::String sha256;
    juce::String ed25519Signature;
    juce::int64 sizeBytes = 0;
    juce::int64 createdAtSeconds = 0;

    VersionInfo toVersionInfo() const;
};

class PendingInstall final
{
public:
    explicit PendingInstall (juce::String productSlug);
    PendingInstall (juce::String productSlug, osci::SettingsStore store);

    std::optional<PendingInstallMarker> load() const;
    juce::Result write (const PendingInstallMarker& marker);
    void clear();

    juce::File getSettingsFile() const;

    static PendingInstallMarker makeMarker (juce::StringRef product,
                                            juce::StringRef currentVersion,
                                            const VersionInfo& version,
                                            const juce::File& artifact);

    static bool isVersionAtLeast (juce::StringRef currentVersion, juce::StringRef targetVersion);
    static bool isResolvedByRunningVersion (const PendingInstallMarker& marker, juce::StringRef currentVersion);
    static juce::Result validateArtifact (const PendingInstallMarker& marker);

    static juce::String toJson (const PendingInstallMarker& marker);
    static std::optional<PendingInstallMarker> fromJson (juce::StringRef json);

private:
    juce::String productSlug;
    mutable osci::SettingsStore settings;

    juce::String key() const;
};

} // namespace osci
