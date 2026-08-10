#pragma once

namespace osci {

struct LinuxInstallManifest {
    juce::String productSlug;
    juce::String displayName;
    juce::String standaloneFile;
    juce::StringArray vst3Bundles;
    juce::StringArray optionalVst3Bundles;
    juce::String desktopCategories;
};

class LinuxInstaller final {
public:
    using Progress = std::function<void (double fraction, juce::StringRef stage)>;

    enum class MissingDirectoryPolicy {
        Create,
        Allow,
        Reject
    };

    struct Request {
        LinuxInstallManifest manifest;
        juce::File archive;
        LinuxInstallLocations locations;
        juce::MemoryBlock iconPng;
        Progress progress;
        MissingDirectoryPolicy missingDirectoryPolicy = MissingDirectoryPolicy::Create;
    };

    struct Report {
        juce::StringArray warnings;
        juce::File standalonePath;
        juce::Array<juce::File> vst3Paths;
        juce::File desktopEntryPath;
        juce::File iconPath;
    };

    struct Config {
        juce::File dataHome;
        std::optional<juce::PropertiesFile::Options> settingsOptions;
        bool refreshDesktopCaches = true;
    };

    LinuxInstaller();
    explicit LinuxInstaller (Config config);

    juce::Result install (const Request& request, Report& report) const;

    static juce::Result validateLocations (const LinuxInstallLocations& locations, MissingDirectoryPolicy policy);

private:
    Config config;
};

} // namespace osci
