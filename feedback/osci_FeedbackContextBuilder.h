#pragma once

namespace osci {

class LicenseManager;

struct FeedbackDisplayInfo {
    int width = 0;
    int height = 0;
    double scale = 0.0;
};

class FeedbackContextBuilder final {
public:
    explicit FeedbackContextBuilder(FeedbackRequest& request);

    FeedbackContextBuilder& withProduct(juce::String slug, juce::String version, juce::String variant);
    FeedbackContextBuilder& withSystemInfo();
    FeedbackContextBuilder& withPluginHost(juce::AudioProcessor::WrapperType wrapperType);
    FeedbackContextBuilder& withDisplay(FeedbackDisplayInfo display);
    FeedbackContextBuilder& withReleaseTrack();
    FeedbackContextBuilder& withContactEmailFrom(const LicenseManager& licenseManager);
    FeedbackContextBuilder& withValidLicenseTokenFrom(const LicenseManager& licenseManager,
                                                      juce::Time now = juce::Time::getCurrentTime());

    static FeedbackDisplayInfo displayInfoFor(const juce::Component& component);
    static juce::String buildFromVersion(juce::StringRef version);
    static juce::String currentOsName();
    static juce::String currentArchitecture();
    static juce::String pluginFormat(juce::AudioProcessor::WrapperType wrapperType);

private:
    FeedbackRequest& request;
};

} // namespace osci
