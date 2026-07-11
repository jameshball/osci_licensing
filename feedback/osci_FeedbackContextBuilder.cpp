#include "osci_FeedbackContextBuilder.h"

#include "../license/osci_LicenseManager.h"
#include "../state/osci_UpdateSettings.h"
#include "../system/osci_HardwareInfo.h"

namespace osci {

FeedbackContextBuilder::FeedbackContextBuilder(FeedbackRequest& requestToPopulate)
    : request(requestToPopulate) {}

FeedbackContextBuilder& FeedbackContextBuilder::withProduct(juce::String slug, juce::String version, juce::String variant) {
    request.productSlug = std::move(slug);
    request.productVersion = std::move(version);
    request.productBuild = buildFromVersion(request.productVersion);
    request.productVariant = std::move(variant);
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withSystemInfo() {
    request.platform = HardwareInfo::getCurrentPlatform();
    request.osName = currentOsName();
    request.osVersion = juce::SystemStats::getOperatingSystemName();
    request.architecture = currentArchitecture();

    const auto language = juce::SystemStats::getUserLanguage();
    const auto region = juce::SystemStats::getUserRegion();
    request.locale = region.isEmpty() ? language : language + "-" + region;
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withPluginHost(juce::AudioProcessor::WrapperType wrapperType) {
    const juce::PluginHostType host;
    request.hostApplication = wrapperType == juce::AudioProcessor::wrapperType_Standalone
        ? "Standalone"
        : juce::String(host.getHostDescription());
    request.pluginFormat = pluginFormat(wrapperType);
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withDisplay(FeedbackDisplayInfo display) {
    request.displayWidth = display.width;
    request.displayHeight = display.height;
    request.displayScale = display.scale;
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withReleaseTrack() {
    jassert(request.productSlug.isNotEmpty());
    if (request.productSlug.isNotEmpty()) {
        request.releaseTrack = BackendClient::toString(UpdateSettings(request.productSlug).releaseTrack());
    }
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withContactEmailFrom(const LicenseManager& licenseManager) {
    const auto payload = licenseManager.getPayload();
    if (payload.has_value()) {
        request.contactEmail = payload->email;
    }
    return *this;
}

FeedbackContextBuilder& FeedbackContextBuilder::withValidLicenseTokenFrom(const LicenseManager& licenseManager, juce::Time now) {
    request.licenseToken.clear();
    const auto payload = licenseManager.getPayload();
    if (payload.has_value() && payload->expiresAt > now) {
        request.licenseToken = licenseManager.getCachedToken();
    }
    return *this;
}

FeedbackDisplayInfo FeedbackContextBuilder::displayInfoFor(const juce::Component& component) {
    FeedbackDisplayInfo result;
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(component.getScreenBounds());
    if (display != nullptr) {
        result.scale = display->scale;
        result.width = juce::roundToInt(display->totalArea.getWidth() * display->scale);
        result.height = juce::roundToInt(display->totalArea.getHeight() * display->scale);
    }
    return result;
}

juce::String FeedbackContextBuilder::buildFromVersion(juce::StringRef version) {
    juce::StringArray parts;
    parts.addTokens(juce::String(version), ".", {});
    return parts.isEmpty() ? juce::String() : parts[parts.size() - 1];
}

juce::String FeedbackContextBuilder::currentOsName() {
#if JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows";
#elif JUCE_LINUX
    return "linux";
#else
    return "other";
#endif
}

juce::String FeedbackContextBuilder::currentArchitecture() {
#if JUCE_ARM && JUCE_64BIT
    return "arm64";
#elif JUCE_ARM
    return "arm";
#elif JUCE_64BIT
    return "x86_64";
#elif JUCE_INTEL
    return "x86";
#else
    return "other";
#endif
}

juce::String FeedbackContextBuilder::pluginFormat(juce::AudioProcessor::WrapperType wrapperType) {
    switch (wrapperType) {
        case juce::AudioProcessor::wrapperType_Standalone: return "Standalone";
        case juce::AudioProcessor::wrapperType_VST: return "VST";
        case juce::AudioProcessor::wrapperType_VST3: return "VST3";
        case juce::AudioProcessor::wrapperType_AudioUnit: return "AU";
        case juce::AudioProcessor::wrapperType_AudioUnitv3: return "AUv3";
        case juce::AudioProcessor::wrapperType_AAX: return "AAX";
        case juce::AudioProcessor::wrapperType_LV2: return "LV2";
        default: return "Other";
    }
}

} // namespace osci
