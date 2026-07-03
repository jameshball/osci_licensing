#pragma once

namespace osci
{

class LicenseManager
{
public:
    enum class Status
    {
        Free,
        PremiumValid,
        PremiumCachedToken,
        ExpiredOffline,
    };

    struct Config
    {
        juce::String productSlug = "osci-render";
        BackendClientConfig backend;
        juce::PropertiesFile::Options settingsOptions = osci::SettingsStore::optionsForSharedLicensing();
        juce::RelativeTime offlineGrace = juce::RelativeTime::days (14.0);
    };

    LicenseManager();
    explicit LicenseManager (Config config);

    static LicenseManager& instance();

    Status status() const noexcept;
    bool hasPremium (const juce::String& featureGroup = {}) const noexcept;

    juce::Result loadCachedToken();
    juce::Result activate (juce::StringRef licenseKey);
    juce::Result refreshNow();
    void scheduleBackgroundRefresh();
    void deactivate();

    juce::ValueTree getStateForUi() const;
    std::optional<LicenseTokenPayload> getPayload() const;
    juce::String getCachedToken() const;

    juce::String getTokenSettingsKey() const;
    juce::File getSettingsFile() const;

private:
    Config config;
    BackendClient backend;
    mutable osci::SettingsStore settings;

    mutable juce::SpinLock stateLock;
    Status currentStatus = Status::Free;
    juce::String cachedToken;
    std::optional<LicenseTokenPayload> cachedPayload;

    void setStateFromValidation (const LicenseTokenValidation& validation, juce::String token);
    void clearCachedState();
    static juce::String statusToString (Status status);
};

} // namespace osci
