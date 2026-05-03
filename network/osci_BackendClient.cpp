namespace osci::licensing
{
#ifndef OSCI_LICENSING_ENABLE_API_LOGGING
#define OSCI_LICENSING_ENABLE_API_LOGGING 1
#endif

namespace
{
    juce::String trimTrailingSlash (juce::String text)
    {
        while (text.endsWithChar ('/'))
            text = text.dropLastCharacters (1);

        return text;
    }

    juce::String errorFromJson (const juce::var& body, int statusCode)
    {
        if (auto* object = body.getDynamicObject())
        {
            for (const auto& key : { "error", "message" })
            {
                const auto value = object->getProperty (key).toString();
                if (value.isNotEmpty())
                    return value;
            }
        }

        return "Request failed with HTTP " + juce::String (statusCode);
    }

    juce::var makeObject()
    {
        return juce::var (new juce::DynamicObject());
    }

    void setProperty (juce::var& object, const juce::Identifier& key, const juce::var& value)
    {
        if (auto* dynamicObject = object.getDynamicObject())
            dynamicObject->setProperty (key, value);
    }

    juce::String getString (juce::DynamicObject& object, const juce::Identifier& key)
    {
        return object.getProperty (key).toString();
    }

    juce::int64 getInt64 (juce::DynamicObject& object, const juce::Identifier& key)
    {
        return static_cast<juce::int64> (object.getProperty (key));
    }

    bool isVersionNewer (juce::StringRef candidateVersion, juce::StringRef currentVersion)
    {
        juce::StringArray candidateParts;
        juce::StringArray currentParts;
        candidateParts.addTokens (juce::String (candidateVersion), ".", {});
        currentParts.addTokens (juce::String (currentVersion), ".", {});

        for (int index = 0; index < 4; ++index)
        {
            const auto candidate = index < candidateParts.size() ? candidateParts[index].getIntValue() : 0;
            const auto current = index < currentParts.size() ? currentParts[index].getIntValue() : 0;
            if (candidate != current)
                return candidate > current;
        }

        return false;
    }

    void populateVersionInfoFromPayload (VersionInfo& response,
                                         const VersionQuery& query,
                                         juce::DynamicObject& versionObject)
    {
        response.product = query.product;
        response.releaseTrack = getString (versionObject, "release_track");
        response.variant = getString (versionObject, "variant");
        response.semver = getString (versionObject, "semver");
        response.isNewer = isVersionNewer (response.semver, query.currentVersion);
        response.notesMarkdown = getString (versionObject, "notes_md");
        response.minSupportedFrom = getString (versionObject, "min_supported_from");
        response.platform = getString (versionObject, "platform");
        response.artifactKind = getString (versionObject, "artifact_kind");
        response.sha256 = getString (versionObject, "sha256");
        response.ed25519Signature = getString (versionObject, "ed25519_sig");
        response.sizeBytes = getInt64 (versionObject, "size_bytes");
    }

#if OSCI_LICENSING_ENABLE_API_LOGGING
    bool isSensitiveJsonKey (juce::StringRef key)
    {
        const auto lower = juce::String (key).toLowerCase();
        return lower == "license_key"
            || lower == "license_token"
            || lower == "token"
            || lower == "url"
            || lower == "email"
            || lower == "em";
    }

    juce::var redactedJsonValue (const juce::var& value)
    {
        if (auto* object = value.getDynamicObject())
        {
            auto* redacted = new juce::DynamicObject();
            const auto& properties = object->getProperties();

            for (int index = 0; index < properties.size(); ++index)
            {
                const auto name = properties.getName (index);
                redacted->setProperty (name,
                                       isSensitiveJsonKey (name.toString())
                                           ? juce::var ("<redacted>")
                                           : redactedJsonValue (properties.getValueAt (index)));
            }

            return juce::var (redacted);
        }

        if (auto* array = value.getArray())
        {
            juce::Array<juce::var> redacted;
            for (const auto& item : *array)
                redacted.add (redactedJsonValue (item));

            return juce::var (redacted);
        }

        return value;
    }

    juce::String redactedJsonText (juce::StringRef text)
    {
        if (text.isEmpty())
            return {};

        juce::var parsed;
        if (juce::JSON::parse (juce::String (text), parsed).failed())
            return "<non-json body, " + juce::String (juce::String (text).getNumBytesAsUTF8()) + " bytes>";

        return juce::JSON::toString (redactedJsonValue (parsed), true);
    }

    juce::String redactedUrl (juce::StringRef url)
    {
        auto text = juce::String (url);
        const auto queryStart = text.indexOfChar ('?');
        if (queryStart >= 0)
            text = text.substring (0, queryStart) + "?<query>";

        return text;
    }
#endif

    void logApiRequest (juce::StringRef method, juce::StringRef url, juce::StringRef body = {})
    {
#if OSCI_LICENSING_ENABLE_API_LOGGING
        juce::String message = "[osci_licensing] API request ";
        message << method << " " << redactedUrl (url);
        if (body.isNotEmpty())
            message << " body=" << redactedJsonText (body);

        juce::Logger::writeToLog (message);
#else
        juce::ignoreUnused (method, url, body);
#endif
    }

    void logApiResponse (juce::StringRef method, juce::StringRef url, int statusCode, juce::StringRef body)
    {
#if OSCI_LICENSING_ENABLE_API_LOGGING
        juce::String message = "[osci_licensing] API response ";
        message << method << " " << redactedUrl (url) << " status=" << statusCode;
        if (body.isNotEmpty())
            message << " body=" << redactedJsonText (body);

        juce::Logger::writeToLog (message);
#else
        juce::ignoreUnused (method, url, statusCode, body);
#endif
    }
}

BackendClient::BackendClient (BackendClientConfig configToUse)
    : config (std::move (configToUse))
{
    config.apiBaseUrl = trimTrailingSlash (config.apiBaseUrl);
}

juce::Result BackendClient::activateLicense (juce::StringRef licenseKey,
                                             juce::StringRef productHint,
                                             ActivationResponse& response) const
{
    auto request = makeObject();
    setProperty (request, "license_key", juce::String (licenseKey));
    if (productHint.isNotEmpty())
        setProperty (request, "product_hint", juce::String (productHint));

    juce::var body;
    if (auto result = postJson ("/api/license/activate", request, body); result.failed())
        return result;

    auto* object = body.getDynamicObject();
    if (object == nullptr || ! static_cast<bool> (object->getProperty ("success")))
        return juce::Result::fail ("Activation response was invalid");

    response.token = getString (*object, "token");
    response.issuedAtSeconds = getInt64 (*object, "issued_at");
    response.expiresAtSeconds = getInt64 (*object, "expires_at");
    response.keyId = static_cast<int> (object->getProperty ("kid"));

    if (auto* purchase = object->getProperty ("purchase").getDynamicObject())
    {
        response.tier = getString (*purchase, "tier");
        response.email = getString (*purchase, "email");
        response.productId = getString (*purchase, "product_id");
        response.productName = getString (*purchase, "product_name");
    }

    if (response.token.isEmpty())
        return juce::Result::fail ("Activation response did not include a token");

    return juce::Result::ok();
}

juce::Result BackendClient::getLatestVersion (const VersionQuery& query, VersionInfo& response) const
{
    juce::StringPairArray params;
    params.set ("product", query.product);
    params.set ("release_track", toString (query.releaseTrack));
    params.set ("variant", query.variant);
    params.set ("platform", query.platform);
    params.set ("current", query.currentVersion);

    juce::var body;
    if (auto result = getJson ("/api/version/latest", params, body); result.failed())
        return result;

    auto* object = body.getDynamicObject();
    if (object == nullptr || ! static_cast<bool> (object->getProperty ("success")))
        return juce::Result::fail ("Version response was invalid");

    if (object->hasProperty ("version"))
    {
        auto* versionObject = object->getProperty ("version").getDynamicObject();
        if (versionObject == nullptr)
        {
            response.product = query.product;
            response.releaseTrack = toString (query.releaseTrack);
            response.variant = query.variant;
            response.isNewer = false;
            return juce::Result::ok();
        }

        populateVersionInfoFromPayload (response, query, *versionObject);
        if (response.semver.isEmpty() || response.platform.isEmpty() || response.sha256.isEmpty())
            return juce::Result::fail ("Version response was incomplete");

        return juce::Result::ok();
    }

    auto* manifest = object->getProperty ("manifest").getDynamicObject();
    if (manifest == nullptr)
        return juce::Result::fail ("Version response did not include a manifest");

    response.product = query.product;
    response.releaseTrack = toString (query.releaseTrack);
    response.variant = query.variant;
    response.semver = getString (*object, "latest");
    response.isNewer = static_cast<bool> (object->getProperty ("is_newer"));
    response.notesMarkdown = getString (*object, "notes_md");
    response.minSupportedFrom = getString (*object, "min_supported_from");
    response.platform = getString (*manifest, "platform");
    response.artifactKind = getString (*manifest, "artifact_kind");
    response.sizeBytes = getInt64 (*manifest, "size_bytes");
    response.sha256 = getString (*manifest, "sha256");
    response.ed25519Signature = getString (*manifest, "ed25519_sig");

    if (response.semver.isEmpty() || response.platform.isEmpty() || response.sha256.isEmpty())
        return juce::Result::fail ("Version response was incomplete");

    return juce::Result::ok();
}

juce::Result BackendClient::getDownloadUrl (const VersionInfo& version,
                                            juce::StringRef licenseToken,
                                            juce::String& url) const
{
    auto request = makeObject();
    setProperty (request, "product", version.product);
    setProperty (request, "semver", version.semver);
    setProperty (request, "release_track", version.releaseTrack);
    setProperty (request, "variant", version.variant);
    setProperty (request, "platform", version.platform);
    if (licenseToken.isNotEmpty())
        setProperty (request, "license_token", juce::String (licenseToken));

    juce::var body;
    if (auto result = postJson ("/api/version/download-url", request, body); result.failed())
        return result;

    auto* object = body.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("Download response was invalid");

    url = getString (*object, "url");
    if (url.isEmpty())
        return juce::Result::fail ("Download response did not include a URL");

    return juce::Result::ok();
}

juce::String BackendClient::toString (ReleaseTrack track)
{
    switch (track)
    {
        case ReleaseTrack::Alpha: return "alpha";
        case ReleaseTrack::Beta:  return "beta";
        case ReleaseTrack::Stable: return "stable";
    }

    return "stable";
}

juce::String BackendClient::endpoint (juce::StringRef path) const
{
    return trimTrailingSlash (config.apiBaseUrl) + juce::String (path);
}

juce::Result BackendClient::getJson (juce::StringRef path,
                                     const juce::StringPairArray& params,
                                     juce::var& response) const
{
    juce::URL url (endpoint (path));
    for (const auto& key : params.getAllKeys())
        if (params[key].isNotEmpty())
            url = url.withParameter (key, params[key]);

    const auto requestUrl = url.toString (true);
    logApiRequest ("GET", requestUrl);

    int statusCode = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (config.timeoutMs)
                                             .withStatusCode (&statusCode)
                                             .withExtraHeaders ("Accept: application/json\r\n"));

    if (stream == nullptr)
    {
        logApiResponse ("GET", requestUrl, statusCode, "<no response>");
        return juce::Result::fail ("Could not connect to " + endpoint (path));
    }

    const auto responseText = stream->readEntireStreamAsString();
    logApiResponse ("GET", requestUrl, statusCode, responseText);
    const auto isSuccess = statusCode >= 200 && statusCode < 300;
    auto parseResult = juce::JSON::parse (responseText, response);

    if (! isSuccess)
    {
        // Surface the actual HTTP error rather than hiding it behind a JSON
        // parse failure when the server returns e.g. a 502 HTML body.
        if (parseResult.failed())
            return juce::Result::fail ("Request failed with HTTP " + juce::String (statusCode));

        return juce::Result::fail (errorFromJson (response, statusCode));
    }

    if (parseResult.failed())
        return parseResult;

    return juce::Result::ok();
}

juce::Result BackendClient::postJson (juce::StringRef path,
                                      const juce::var& body,
                                      juce::var& response) const
{
    const auto requestText = juce::JSON::toString (body, true);
    auto url = juce::URL (endpoint (path)).withPOSTData (requestText);
    const auto requestUrl = endpoint (path);
    logApiRequest ("POST", requestUrl, requestText);

    int statusCode = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                             .withConnectionTimeoutMs (config.timeoutMs)
                                             .withStatusCode (&statusCode)
                                             .withHttpRequestCmd ("POST")
                                             .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n"));

    if (stream == nullptr)
    {
        logApiResponse ("POST", requestUrl, statusCode, "<no response>");
        return juce::Result::fail ("Could not connect to " + endpoint (path));
    }

    const auto responseText = stream->readEntireStreamAsString();
    logApiResponse ("POST", requestUrl, statusCode, responseText);
    const auto isSuccess = statusCode >= 200 && statusCode < 300;
    auto parseResult = juce::JSON::parse (responseText, response);

    if (! isSuccess)
    {
        if (parseResult.failed())
            return juce::Result::fail ("Request failed with HTTP " + juce::String (statusCode));

        return juce::Result::fail (errorFromJson (response, statusCode));
    }

    if (parseResult.failed())
        return parseResult;

    return juce::Result::ok();
}

} // namespace osci::licensing
