namespace osci
{
namespace
{
    juce::var makePendingObject()
    {
        return juce::var (new juce::DynamicObject());
    }

    void setPendingProperty (juce::var& object, const juce::Identifier& key, const juce::var& value)
    {
        if (auto* dynamicObject = object.getDynamicObject())
            dynamicObject->setProperty (key, value);
    }

    juce::String getPendingString (juce::DynamicObject& object, const juce::Identifier& key)
    {
        return object.getProperty (key).toString();
    }

    juce::int64 getPendingInt64 (juce::DynamicObject& object, const juce::Identifier& key)
    {
        return static_cast<juce::int64> (object.getProperty (key));
    }
}

VersionInfo PendingInstallMarker::toVersionInfo() const
{
    VersionInfo version;
    version.product = product;
    version.semver = targetVersion;
    version.releaseTrack = releaseTrack;
    version.variant = variant;
    version.platform = platform;
    version.artifactKind = artifactKind;
    version.sizeBytes = sizeBytes;
    version.sha256 = sha256;
    version.ed25519Signature = ed25519Signature;
    version.isNewer = true;
    return version;
}

PendingInstall::PendingInstall (juce::String productSlugToUse)
    : PendingInstall (std::move (productSlugToUse), osci::SettingsStore::forSharedLicensing())
{
}

PendingInstall::PendingInstall (juce::String productSlugToUse, osci::SettingsStore store)
    : productSlug (std::move (productSlugToUse)), settings (std::move (store))
{
}

std::optional<PendingInstallMarker> PendingInstall::load() const
{
    settings.reload();
    const auto json = settings.getString (key()).trim();
    if (json.isEmpty())
        return std::nullopt;

    auto marker = fromJson (json);
    if (! marker.has_value() || marker->product != productSlug)
        return std::nullopt;

    return marker;
}

juce::Result PendingInstall::write (const PendingInstallMarker& marker)
{
    settings.set (key(), toJson (marker));
    return settings.save() ? juce::Result::ok()
                           : juce::Result::fail ("Could not write pending install marker");
}

void PendingInstall::clear()
{
    settings.remove (key());
    settings.save();
}

juce::File PendingInstall::getSettingsFile() const
{
    return settings.getFile();
}

PendingInstallMarker PendingInstall::makeMarker (juce::StringRef product,
                                                 juce::StringRef currentVersion,
                                                 const VersionInfo& version,
                                                 const juce::File& artifact)
{
    PendingInstallMarker marker;
    marker.product = product;
    marker.previousVersion = currentVersion;
    marker.targetVersion = version.semver;
    marker.releaseTrack = version.releaseTrack;
    marker.variant = version.variant;
    marker.platform = version.platform;
    marker.artifactKind = version.artifactKind;
    marker.artifactPath = artifact.getFullPathName();
    marker.sha256 = version.sha256;
    marker.ed25519Signature = version.ed25519Signature;
    marker.sizeBytes = version.sizeBytes;
    marker.createdAtSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    return marker;
}

bool PendingInstall::isVersionAtLeast (juce::StringRef currentVersion, juce::StringRef targetVersion)
{
    juce::StringArray currentParts;
    juce::StringArray targetParts;
    currentParts.addTokens (juce::String (currentVersion), ".", {});
    targetParts.addTokens (juce::String (targetVersion), ".", {});

    for (int index = 0; index < 4; ++index)
    {
        const auto current = index < currentParts.size() ? currentParts[index].getIntValue() : 0;
        const auto target = index < targetParts.size() ? targetParts[index].getIntValue() : 0;
        if (current != target)
            return current > target;
    }

    return true;
}

bool PendingInstall::isResolvedByRunningVersion (const PendingInstallMarker& marker, juce::StringRef currentVersion)
{
    return isVersionAtLeast (currentVersion, marker.targetVersion);
}

juce::Result PendingInstall::validateArtifact (const PendingInstallMarker& marker)
{
    if (marker.artifactPath.isEmpty())
        return juce::Result::fail ("Pending installer path is empty");

    const juce::File artifact (marker.artifactPath);
    if (! artifact.existsAsFile())
        return juce::Result::fail ("Pending installer file is missing");

    if (marker.sha256.isEmpty())
        return juce::Result::fail ("Pending installer SHA-256 is missing");

    const auto actualSha256 = juce::SHA256 (artifact).toHexString().toLowerCase();
    if (actualSha256 != marker.sha256.toLowerCase())
        return juce::Result::fail ("Pending installer SHA-256 does not match");

    return juce::Result::ok();
}

juce::String PendingInstall::toJson (const PendingInstallMarker& marker)
{
    auto object = makePendingObject();
    setPendingProperty (object, "product", marker.product);
    setPendingProperty (object, "previous_version", marker.previousVersion);
    setPendingProperty (object, "target_version", marker.targetVersion);
    setPendingProperty (object, "release_track", marker.releaseTrack);
    setPendingProperty (object, "variant", marker.variant);
    setPendingProperty (object, "platform", marker.platform);
    setPendingProperty (object, "artifact_kind", marker.artifactKind);
    setPendingProperty (object, "artifact_path", marker.artifactPath);
    setPendingProperty (object, "sha256", marker.sha256);
    setPendingProperty (object, "ed25519_sig", marker.ed25519Signature);
    setPendingProperty (object, "size_bytes", marker.sizeBytes);
    setPendingProperty (object, "created_at", marker.createdAtSeconds);
    return juce::JSON::toString (object, true);
}

std::optional<PendingInstallMarker> PendingInstall::fromJson (juce::StringRef json)
{
    juce::var parsed;
    if (juce::JSON::parse (juce::String (json), parsed).failed())
        return std::nullopt;

    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;

    PendingInstallMarker marker;
    marker.product = getPendingString (*object, "product");
    marker.previousVersion = getPendingString (*object, "previous_version");
    marker.targetVersion = getPendingString (*object, "target_version");
    marker.releaseTrack = getPendingString (*object, "release_track");
    marker.variant = getPendingString (*object, "variant");
    marker.platform = getPendingString (*object, "platform");
    marker.artifactKind = getPendingString (*object, "artifact_kind");
    marker.artifactPath = getPendingString (*object, "artifact_path");
    marker.sha256 = getPendingString (*object, "sha256");
    marker.ed25519Signature = getPendingString (*object, "ed25519_sig");
    marker.sizeBytes = getPendingInt64 (*object, "size_bytes");
    marker.createdAtSeconds = getPendingInt64 (*object, "created_at");

    if (marker.product.isEmpty() || marker.targetVersion.isEmpty())
        return std::nullopt;

    return marker;
}

juce::String PendingInstall::key() const
{
    return "install." + productSlug + ".pending";
}

} // namespace osci
