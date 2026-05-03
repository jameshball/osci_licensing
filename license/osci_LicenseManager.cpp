namespace osci
{

LicenseManager::LicenseManager()
    : LicenseManager (Config {})
{
}

LicenseManager::LicenseManager (Config configToUse)
    : config (std::move (configToUse)),
      backend (config.backend),
      settings (config.settingsOptions)
{
}

LicenseManager& LicenseManager::instance()
{
    static LicenseManager manager;
    return manager;
}

LicenseManager::Status LicenseManager::status() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return currentStatus;
}

bool LicenseManager::hasPremium (const juce::String& featureGroup) const noexcept
{
    juce::ignoreUnused (featureGroup);
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return (currentStatus == Status::PremiumValid || currentStatus == Status::PremiumCachedToken)
           && cachedPayload.has_value() && cachedPayload->isPremium();
}

juce::Result LicenseManager::loadCachedToken()
{
    settings.reload();

    const auto token = settings.getString (getTokenSettingsKey()).trim();
    if (token.isEmpty())
    {
        clearCachedState();
        return juce::Result::ok();
    }

    const auto validation = LicenseToken::validate (token, juce::Time::getCurrentTime(), config.offlineGrace);
    if (validation.result.failed())
    {
        if (auto payload = LicenseToken::inspectUnverified (token))
        {
            const juce::SpinLock::ScopedLockType lock (stateLock);
            cachedToken = token;
            cachedPayload = *payload;
            currentStatus = Status::ExpiredOffline;
        }

        return validation.result;
    }

    setStateFromValidation (validation, token);
    return juce::Result::ok();
}

juce::Result LicenseManager::activate (juce::StringRef licenseKey)
{
    ActivationResponse activation;
    const auto activationResult = backend.activateLicense (licenseKey, config.productSlug, activation);
    if (activationResult.failed()) {
        return activationResult;
    }

    const auto validation = LicenseToken::validate (activation.token, juce::Time::getCurrentTime(), config.offlineGrace);
    if (validation.result.failed()) {
        return validation.result;
    }

    settings.set (getTokenSettingsKey(), activation.token);
    if (! settings.save()) {
        return juce::Result::fail ("Could not write license token");
    }

    setStateFromValidation (validation, activation.token);
    return juce::Result::ok();
}

juce::Result LicenseManager::refreshNow()
{
    juce::String licenseKey;
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        if (cachedPayload.has_value())
            licenseKey = cachedPayload->licenseKey;
    }

    if (licenseKey.isEmpty())
    {
        settings.reload();
        const auto token = settings.getString (getTokenSettingsKey()).trim();
        if (auto payload = LicenseToken::inspectUnverified (token))
            licenseKey = payload->licenseKey;
    }

    if (licenseKey.isEmpty())
        return juce::Result::fail ("No cached license key is available to refresh");

    return activate (licenseKey);
}

void LicenseManager::scheduleBackgroundRefresh()
{
    juce::Thread::launch ([this]
    {
        juce::ignoreUnused (refreshNow());
    });
}

void LicenseManager::deactivate()
{
    settings.remove (getTokenSettingsKey());
    settings.save();
    clearCachedState();
}

juce::ValueTree LicenseManager::getStateForUi() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    juce::ValueTree state ("license");
    state.setProperty ("status", statusToString (currentStatus), nullptr);
    state.setProperty ("premium", currentStatus == Status::PremiumValid || currentStatus == Status::PremiumCachedToken, nullptr);

    if (cachedPayload.has_value())
    {
        state.setProperty ("email", cachedPayload->email, nullptr);
        state.setProperty ("tier", cachedPayload->tier, nullptr);
        state.setProperty ("product_id", cachedPayload->productId, nullptr);
        state.setProperty ("expires_at", cachedPayload->expiresAt.toISO8601 (true), nullptr);
    }

    return state;
}

std::optional<LicenseTokenPayload> LicenseManager::getPayload() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return cachedPayload;
}

juce::String LicenseManager::getCachedToken() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return cachedToken;
}

juce::String LicenseManager::getTokenSettingsKey() const
{
    return "license." + config.productSlug + ".token";
}

juce::File LicenseManager::getSettingsFile() const
{
    return settings.getFile();
}

void LicenseManager::setStateFromValidation (const LicenseTokenValidation& validation, juce::String token)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    cachedToken = std::move (token);
    cachedPayload = validation.payload;
    currentStatus = validation.payload.isPremium()
        ? (validation.withinOfflineGrace ? Status::PremiumCachedToken : Status::PremiumValid)
        : Status::Free;
}

void LicenseManager::clearCachedState()
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    currentStatus = Status::Free;
    cachedToken.clear();
    cachedPayload.reset();
}

juce::String LicenseManager::statusToString (Status status)
{
    switch (status)
    {
        case Status::Free: return "free";
        case Status::PremiumValid: return "premium_valid";
        case Status::PremiumCachedToken: return "premium_cached_token";
        case Status::ExpiredOffline: return "expired_offline";
    }

    return "free";
}

} // namespace osci
