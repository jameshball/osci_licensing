namespace osci {

UpdateSettings::UpdateSettings (juce::String productSlugToUse)
    : UpdateSettings (std::move (productSlugToUse), osci::SettingsStore::forSharedLicensing())
{
}

UpdateSettings::UpdateSettings (juce::String productSlugToUse, osci::SettingsStore store)
    : productSlug (std::move (productSlugToUse)), settings (std::move (store))
{
}

bool UpdateSettings::betaUpdatesEnabled() const
{
    return releaseTrack() != ReleaseTrack::Stable;
}

ReleaseTrack UpdateSettings::releaseTrack() const
{
    settings.reload();
    return releaseTrackFromString (settings.getString (key ("releaseTrack"), "stable"));
}

void UpdateSettings::setReleaseTrack (ReleaseTrack track)
{
    settings.set (key ("releaseTrack"), BackendClient::toString (track));
    settings.save();
}

void UpdateSettings::useStableTrack()
{
    settings.set (key ("releaseTrack"), "stable");
    settings.save();
}

bool UpdateSettings::isDismissed (juce::StringRef semver, double nowSeconds, double cooldownSeconds) const
{
    settings.reload();

    if (settings.getString (key ("dismissedSemver")) != juce::String (semver))
        return false;

    const auto dismissedAt = settings.getDouble (key ("dismissedAt"), 0.0);
    return dismissedAt > 0.0 && (nowSeconds - dismissedAt) < cooldownSeconds;
}

void UpdateSettings::dismiss (juce::StringRef semver, double nowSeconds)
{
    settings.set (key ("dismissedSemver"), juce::String (semver));
    settings.set (key ("dismissedAt"), nowSeconds);
    settings.save();
}

void UpdateSettings::clearDismissal()
{
    settings.remove (key ("dismissedSemver"));
    settings.remove (key ("dismissedAt"));
    settings.save();
}

juce::File UpdateSettings::getSettingsFile() const
{
    return settings.getFile();
}

juce::String UpdateSettings::key (juce::StringRef leaf) const
{
    return "updates." + productSlug + "." + juce::String (leaf);
}

ReleaseTrack UpdateSettings::releaseTrackFromString (juce::StringRef text)
{
    const auto lower = juce::String (text).toLowerCase();
    if (lower == "alpha") {
        return ReleaseTrack::Alpha;
    }

    if (lower == "beta") {
        return ReleaseTrack::Beta;
    }

    return ReleaseTrack::Stable;
}

} // namespace osci
