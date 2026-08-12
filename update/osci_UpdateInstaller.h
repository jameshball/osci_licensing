#pragma once

namespace osci {

class LinuxUpdateInstallThread final : public juce::Thread {
public:
    struct Request {
        juce::File archive;
        VersionInfo version;
        juce::String currentVersion;
        LinuxInstallManifest manifest;
        juce::MemoryBlock iconPng;
        LinuxInstaller::Progress progress;
        std::function<void (juce::Result, LinuxInstaller::Report)> completion;
    };

    explicit LinuxUpdateInstallThread (Request request);
    ~LinuxUpdateInstallThread() override;

    void run() override;

private:
    Request request;

    void finish (juce::Result result, LinuxInstaller::Report report);
};

class UpdateInstaller final {
public:
    struct LaunchRequest {
        juce::File installer;
        std::optional<VersionInfo> version;
        juce::String product;
        juce::String currentVersion;
    };

    static juce::Result launchWithPendingMarker (const LaunchRequest& request);
    static std::unique_ptr<LinuxUpdateInstallThread> installLinuxAsync (LinuxUpdateInstallThread::Request request);

private:
    UpdateInstaller() = delete;
};

} // namespace osci
