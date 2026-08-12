#include "osci_UpdateInstaller.h"

namespace osci {

LinuxUpdateInstallThread::LinuxUpdateInstallThread (Request requestToUse)
    : juce::Thread ("Linux update installer"), request (std::move (requestToUse)) {
}

LinuxUpdateInstallThread::~LinuxUpdateInstallThread() {
    stopThread (-1);
}

void LinuxUpdateInstallThread::run() {
    const auto product = request.manifest.productSlug;
    juce::InterProcessLock updateLock ("osci-linux-update-" + product);
    if (!updateLock.enter (0)) {
        finish (juce::Result::fail ("Another update of " + product + " is already in progress"), {});
        return;
    }

    PendingInstall pending (product);
    const auto marker = PendingInstall::makeMarker (product, request.currentVersion, request.version, request.archive);
    auto result = pending.write (marker);
    LinuxInstaller::Report report;

    if (result.wasOk()) {
        result = PendingInstall::validateArtifact (marker);
    }

    LinuxInstallLocations locations;
    std::optional<LinuxInstallLocations> savedLocations;
    if (result.wasOk()) {
        result = LinuxInstallSettings (product).loadSaved (savedLocations);
        if (result.wasOk()) {
            locations = savedLocations.value_or (LinuxInstallSettings::defaults());
        }
    }

    if (result.wasOk()) {
        const auto policy = savedLocations.has_value() ? LinuxInstaller::MissingDirectoryPolicy::Reject
                                                        : LinuxInstaller::MissingDirectoryPolicy::Create;
        result = LinuxInstaller::validateLocations (locations, policy);
        if (result.failed()) {
            result = juce::Result::fail (result.getErrorMessage() + " Run osci-installer to choose installation locations again.");
        }
    }

    if (result.wasOk()) {
        LinuxInstaller::Request installRequest;
        installRequest.manifest = std::move (request.manifest);
        installRequest.archive = request.archive;
        installRequest.locations = locations;
        installRequest.iconPng = std::move (request.iconPng);
        installRequest.progress = std::move (request.progress);
        installRequest.missingDirectoryPolicy = savedLocations.has_value() ? LinuxInstaller::MissingDirectoryPolicy::Reject
                                                                            : LinuxInstaller::MissingDirectoryPolicy::Create;
        result = LinuxInstaller().install (installRequest, report);
    }

    pending.clear();
    finish (result, std::move (report));
}

void LinuxUpdateInstallThread::finish (juce::Result result, LinuxInstaller::Report report) {
    juce::MessageManager::callAsync ([completion = std::move (request.completion), result, report = std::move (report)]() mutable {
        if (completion != nullptr) {
            completion (result, std::move (report));
        }
    });
}

juce::Result UpdateInstaller::launchWithPendingMarker (const LaunchRequest& request) {
    bool markerWritten = false;
    if (request.version.has_value()) {
        PendingInstall pending (request.product);
        const auto marker = PendingInstall::makeMarker (request.product, request.currentVersion, *request.version, request.installer);
        const auto markerResult = pending.write (marker);
        if (markerResult.failed()) {
            juce::Logger::writeToLog ("Pending install marker write failed: " + markerResult.getErrorMessage());
        } else {
            markerWritten = true;
        }
    }

    if (InstallerLauncher::launchAndExitHost (request.installer)) {
        return juce::Result::ok();
    }

    if (markerWritten) {
        PendingInstall (request.product).clear();
    }

    return juce::Result::fail ("Could not launch downloaded installer at " + request.installer.getFullPathName() + ".");
}

std::unique_ptr<LinuxUpdateInstallThread> UpdateInstaller::installLinuxAsync (LinuxUpdateInstallThread::Request request) {
    auto worker = std::make_unique<LinuxUpdateInstallThread> (std::move (request));
    if (!worker->startThread()) {
        return nullptr;
    }
    return worker;
}

} // namespace osci
