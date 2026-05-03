namespace osci::licensing
{

Downloader::Downloader()
    : Downloader (Config {})
{
}

Downloader::Downloader (Config configToUse)
    : config (std::move (configToUse)), backend (config.backend)
{
    if (config.downloadDirectory == juce::File())
        config.downloadDirectory = HardwareInfo::getDefaultStorageDirectory ("osci-render").getChildFile ("downloads");
}

juce::Result Downloader::downloadAndVerify (const VersionInfo& version,
                                            juce::StringRef licenseToken,
                                            Progress progress)
{
    if (! LicenseToken::canVerifyReleaseSignatures())
        return juce::Result::fail ("Release signature verifier is not configured");

    if (! LicenseToken::verifyReleaseSignature (version.semver, version.platform, version.sha256, version.ed25519Signature))
        return juce::Result::fail ("Release signature does not verify");

    juce::String downloadUrl;
    if (auto result = backend.getDownloadUrl (version, licenseToken, downloadUrl); result.failed())
        return result;

    // Defense in depth: only follow https URLs. Integrity is already enforced
    // by SHA-256 + ed25519 verification, but reject obviously wrong schemes
    // (file://, http://) up front so a misconfigured or compromised backend
    // cannot redirect the client to internal/local resources.
    if (! downloadUrl.startsWithIgnoreCase ("https://"))
        return juce::Result::fail ("Download URL must use https");

    if (! config.downloadDirectory.createDirectory())
        return juce::Result::fail ("Could not create download directory");

    auto target = targetFileFor (version);
    target.deleteFile();

    int statusCode = 0;
    auto stream = juce::URL (downloadUrl).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (30000)
            .withStatusCode (&statusCode));

    if (stream == nullptr || statusCode < 200 || statusCode >= 300)
        return juce::Result::fail ("Could not open download URL");

    juce::FileOutputStream output (target);
    if (! output.openedOk())
        return juce::Result::fail ("Could not write download file");

    constexpr int bufferSize = 64 * 1024;
    // Hard cap: refuse downloads larger than 1 GB so a buggy or hostile
    // backend can't fill the user's disk before SHA-256 verification catches
    // the mismatch. If a server-supplied size is available, additionally
    // refuse anything more than 1.5x that hint.
    constexpr juce::int64 absoluteSizeCap = 1024LL * 1024LL * 1024LL;
    const juce::int64 reportedSize = juce::jmax<juce::int64> (version.sizeBytes, 0);
    const juce::int64 sizeCap = reportedSize > 0
        ? juce::jmin (absoluteSizeCap, reportedSize + reportedSize / 2)
        : absoluteSizeCap;

    juce::HeapBlock<char> buffer (bufferSize);
    juce::int64 downloadedBytes = 0;
    const auto totalBytes = stream->getTotalLength();

    for (;;)
    {
        const auto bytesRead = stream->read (buffer.get(), bufferSize);
        if (bytesRead <= 0)
            break;

        if (! output.write (buffer.get(), static_cast<size_t> (bytesRead)))
            return juce::Result::fail ("Could not write download file");

        downloadedBytes += bytesRead;
        if (downloadedBytes > sizeCap)
        {
            output.flush();
            target.deleteFile();
            return juce::Result::fail ("Download exceeded the maximum allowed size");
        }

        if (progress != nullptr)
            progress (totalBytes > 0 ? static_cast<double> (downloadedBytes) / static_cast<double> (totalBytes) : -1.0,
                      downloadedBytes);
    }

    output.flush();

    const auto actualSha256 = juce::SHA256 (target).toHexString().toLowerCase();
    if (actualSha256 != version.sha256.toLowerCase())
    {
        target.deleteFile();
        return juce::Result::fail ("Downloaded file SHA-256 does not match manifest");
    }

    downloadedFile = target;
    return juce::Result::ok();
}

juce::File Downloader::getDownloadedFile() const
{
    return downloadedFile;
}

juce::File Downloader::targetFileFor (const VersionInfo& version) const
{
    const auto extension = version.artifactKind.isNotEmpty() ? version.artifactKind : "bin";
    const auto filename = version.product + "-" + version.semver + "-" + version.platform + "." + extension;
    return config.downloadDirectory.getChildFile (filename.replaceCharacter ('/', '-'));
}

} // namespace osci::licensing
