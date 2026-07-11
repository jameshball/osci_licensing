#include "osci_FeedbackClient.h"

namespace osci {
namespace {
constexpr size_t maxScreenshotBytesTotal = 25 * 1024 * 1024;

juce::var makeFeedbackObject() {
    return juce::var(new juce::DynamicObject());
}

void setFeedbackProperty(juce::var& object, const juce::Identifier& key, const juce::var& value) {
    auto* dynamicObject = object.getDynamicObject();
    if (dynamicObject != nullptr) {
        dynamicObject->setProperty(key, value);
    }
}

juce::String errorFromResponse(const juce::var& response, int statusCode) {
    auto* object = response.getDynamicObject();
    if (object != nullptr) {
        const auto error = object->getProperty("error").toString();
        if (error.isNotEmpty()) {
            return error;
        }
    }

    return statusCode > 0 ? "Feedback request failed with HTTP " + juce::String(statusCode)
                          : "Could not connect to the feedback service";
}

juce::String headerText(const juce::StringPairArray& headers) {
    juce::String text;
    for (const auto& key : headers.getAllKeys()) {
        text << key << ": " << headers[key] << "\r\n";
    }
    return text;
}

bool isCancelled(const std::atomic<bool>* cancellationRequested) {
    return cancellationRequested != nullptr && cancellationRequested->load(std::memory_order_relaxed);
}
} // namespace

FeedbackClient::FeedbackClient(BackendClientConfig configToUse)
    : config(std::move(configToUse)) {
    while (config.apiBaseUrl.endsWithChar('/')) {
        config.apiBaseUrl = config.apiBaseUrl.dropLastCharacters(1);
    }
}

juce::Result FeedbackClient::submit(const FeedbackRequest& request,
                                    FeedbackResponse& response,
                                    const std::atomic<bool>* cancellationRequested) const {
    if (request.productSlug.isEmpty()) {
        return juce::Result::fail("Feedback product is missing");
    }

    size_t screenshotBytes = 0;
    for (const auto& attachment : request.attachments) {
        if (attachment.kind == FeedbackAttachmentKind::screenshot) {
            screenshotBytes += attachment.data.getSize();
        }
    }
    if (screenshotBytes > maxScreenshotBytesTotal) {
        return juce::Result::fail("Attached screenshots exceed the 25 MiB total limit");
    }

    std::vector<PreparedUpload> uploads;
    if (!request.attachments.empty()) {
        const auto prepareResult = prepareUploads(request, juce::Uuid().toString(), uploads, cancellationRequested);
        if (prepareResult.failed()) {
            return prepareResult;
        }
        if (uploads.size() != request.attachments.size()) {
            return juce::Result::fail("Feedback service returned an incomplete upload batch");
        }

        for (size_t index = 0; index < uploads.size(); ++index) {
            if (isCancelled(cancellationRequested)) {
                return juce::Result::fail("Feedback submission was cancelled");
            }
            const auto uploadResult = uploadAttachment(uploads[index], request.attachments[index], cancellationRequested);
            if (uploadResult.failed()) {
                return uploadResult;
            }
        }
    }

    if (isCancelled(cancellationRequested)) {
        return juce::Result::fail("Feedback submission was cancelled");
    }
    return createFeedback(request, uploads, juce::Uuid().toString(), response, cancellationRequested);
}

juce::String FeedbackClient::kindToString(FeedbackKind kind) {
    return kind == FeedbackKind::featureRequest ? "feature_request" : "bug";
}

juce::String FeedbackClient::attachmentKindToString(FeedbackAttachmentKind kind) {
    return kind == FeedbackAttachmentKind::project ? "project" : "screenshot";
}

juce::String FeedbackClient::endpoint(juce::StringRef path) const {
    return config.apiBaseUrl + juce::String(path);
}

juce::Result FeedbackClient::prepareUploads(const FeedbackRequest& request,
                                            juce::StringRef idempotencyKey,
                                            std::vector<PreparedUpload>& uploads,
                                            const std::atomic<bool>* cancellationRequested) const {
    if (isCancelled(cancellationRequested)) {
        return juce::Result::fail("Feedback submission was cancelled");
    }

    juce::Array<juce::var> files;
    for (const auto& attachment : request.attachments) {
        auto item = makeFeedbackObject();
        setFeedbackProperty(item, "kind", attachmentKindToString(attachment.kind));
        setFeedbackProperty(item, "filename", attachment.filename);
        setFeedbackProperty(item, "content_type", attachment.contentType);
        setFeedbackProperty(item, "size_bytes", static_cast<juce::int64>(attachment.data.getSize()));
        files.add(std::move(item));
    }
    auto body = makeFeedbackObject();
    setFeedbackProperty(body, "files", juce::var(std::move(files)));

    juce::var json;
    const auto result = postJson("/api/v1/products/" + request.productSlug + "/feedback/uploads",
                                 body,
                                 idempotencyKey,
                                 json,
                                 cancellationRequested);
    if (result.failed()) {
        return result;
    }
    auto* root = json.getDynamicObject();
    auto* data = root != nullptr ? root->getProperty("data").getDynamicObject() : nullptr;
    auto* array = data != nullptr ? data->getProperty("uploads").getArray() : nullptr;
    if (array == nullptr) {
        return juce::Result::fail("Feedback upload response was invalid");
    }
    for (const auto& item : *array) {
        auto* object = item.getDynamicObject();
        if (object == nullptr) {
            return juce::Result::fail("Feedback upload response was invalid");
        }
        PreparedUpload upload;
        upload.id = object->getProperty("id").toString();
        upload.url = object->getProperty("url").toString();
        upload.method = object->getProperty("method").toString();
        auto* headers = object->getProperty("headers").getDynamicObject();
        if (headers != nullptr) {
            const auto& properties = headers->getProperties();
            for (int index = 0; index < properties.size(); ++index) {
                upload.headers.set(properties.getName(index).toString(), properties.getValueAt(index).toString());
            }
        }
        if (upload.id.isEmpty() || upload.url.isEmpty() || upload.method.isEmpty()) {
            return juce::Result::fail("Feedback upload response was incomplete");
        }
        uploads.push_back(std::move(upload));
    }
    return juce::Result::ok();
}

juce::Result FeedbackClient::uploadAttachment(const PreparedUpload& upload,
                                              const FeedbackAttachmentData& attachment,
                                              const std::atomic<bool>* cancellationRequested) const {
    if (!upload.url.startsWithIgnoreCase("https://") && !config.apiBaseUrl.startsWithIgnoreCase("http://127.0.0.1")) {
        return juce::Result::fail("Feedback upload URL was not secure");
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (isCancelled(cancellationRequested)) {
            return juce::Result::fail("Feedback submission was cancelled");
        }
        int statusCode = 0;
        auto url = juce::URL(upload.url).withPOSTData(attachment.data);
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                .withConnectionTimeoutMs(config.timeoutMs)
                                                .withStatusCode(&statusCode)
                                                .withHttpRequestCmd(upload.method)
                                                .withExtraHeaders(headerText(upload.headers))
                                                .withProgressCallback([cancellationRequested](int, int) {
                                                    return !isCancelled(cancellationRequested);
                                                }));
        if (stream != nullptr && statusCode >= 200 && statusCode < 300) {
            return juce::Result::ok();
        }
        if (statusCode > 0) {
            return juce::Result::fail("Attachment upload failed with HTTP " + juce::String(statusCode));
        }
        if (attempt == 0) {
            juce::Thread::sleep(250);
            if (isCancelled(cancellationRequested)) {
                return juce::Result::fail("Feedback submission was cancelled");
            }
        }
    }
    return juce::Result::fail("Could not upload a feedback attachment");
}

juce::Result FeedbackClient::createFeedback(const FeedbackRequest& request,
                                            const std::vector<PreparedUpload>& uploads,
                                            juce::StringRef idempotencyKey,
                                            FeedbackResponse& response,
                                            const std::atomic<bool>* cancellationRequested) const {
    if (isCancelled(cancellationRequested)) {
        return juce::Result::fail("Feedback submission was cancelled");
    }

    auto body = makeFeedbackObject();
    setFeedbackProperty(body, "kind", kindToString(request.kind));
    setFeedbackProperty(body, "title", request.title);
    setFeedbackProperty(body, "description", request.description);
    setFeedbackProperty(body, "contact_email", request.contactEmail);
    setFeedbackProperty(body, "product_version", request.productVersion);
    setFeedbackProperty(body, "product_build", request.productBuild);
    setFeedbackProperty(body, "release_track", request.releaseTrack);
    setFeedbackProperty(body, "product_variant", request.productVariant);
    setFeedbackProperty(body, "platform", request.platform);
    setFeedbackProperty(body, "os_name", request.osName);
    setFeedbackProperty(body, "os_version", request.osVersion);
    setFeedbackProperty(body, "os_build", request.osBuild);
    setFeedbackProperty(body, "architecture", request.architecture);
    setFeedbackProperty(body, "locale", request.locale);
    setFeedbackProperty(body, "host_application", request.hostApplication);
    setFeedbackProperty(body, "host_application_version", request.hostApplicationVersion);
    setFeedbackProperty(body, "plugin_format", request.pluginFormat);
    if (request.displayWidth > 0) {
        setFeedbackProperty(body, "display_width", request.displayWidth);
    }
    if (request.displayHeight > 0) {
        setFeedbackProperty(body, "display_height", request.displayHeight);
    }
    if (request.displayScale > 0.0) {
        setFeedbackProperty(body, "display_scale", request.displayScale);
    }
    if (request.clientContextSchemaVersion > 0 && !request.clientContext.isVoid()) {
        setFeedbackProperty(body, "client_context_schema_version", request.clientContextSchemaVersion);
        setFeedbackProperty(body, "client_context", request.clientContext);
    }
    if (request.log.isNotEmpty()) {
        setFeedbackProperty(body, "osci_render_log", request.log);
        setFeedbackProperty(body, "log_truncated", request.logTruncated);
    }
    if (request.licenseToken.isNotEmpty()) {
        setFeedbackProperty(body, "license_token", request.licenseToken);
    }
    juce::Array<juce::var> uploadIds;
    for (const auto& upload : uploads) {
        uploadIds.add(upload.id);
    }
    setFeedbackProperty(body, "upload_ids", juce::var(std::move(uploadIds)));

    juce::var json;
    const auto result = postJson("/api/v1/products/" + request.productSlug + "/feedback",
                                 body,
                                 idempotencyKey,
                                 json,
                                 cancellationRequested);
    if (result.failed()) {
        return result;
    }
    auto* root = json.getDynamicObject();
    auto* data = root != nullptr ? root->getProperty("data").getDynamicObject() : nullptr;
    if (data == nullptr) {
        return juce::Result::fail("Feedback response was invalid");
    }
    response.reference = data->getProperty("reference").toString();
    response.createdAt = data->getProperty("created_at").toString();
    response.verifiedCustomer = static_cast<bool>(data->getProperty("verified_customer"));
    if (response.reference.isEmpty()) {
        return juce::Result::fail("Feedback response did not include a reference");
    }
    return juce::Result::ok();
}

juce::Result FeedbackClient::postJson(juce::StringRef path,
                                      const juce::var& body,
                                      juce::StringRef idempotencyKey,
                                      juce::var& response,
                                      const std::atomic<bool>* cancellationRequested) const {
    const auto requestText = juce::JSON::toString(body, true);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (isCancelled(cancellationRequested)) {
            return juce::Result::fail("Feedback submission was cancelled");
        }

        int statusCode = 0;
        auto url = juce::URL(endpoint(path)).withPOSTData(requestText);
        juce::String headers;
        headers << "Content-Type: application/json\r\nAccept: application/json\r\nIdempotency-Key: "
                << idempotencyKey << "\r\n";
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                                .withConnectionTimeoutMs(config.timeoutMs)
                                                .withStatusCode(&statusCode)
                                                .withHttpRequestCmd("POST")
                                                .withExtraHeaders(headers)
                                                .withProgressCallback([cancellationRequested](int, int) {
                                                    return !isCancelled(cancellationRequested);
                                                }));
        if (stream == nullptr) {
            if (attempt == 0 && statusCode == 0) {
                juce::Thread::sleep(250);
                if (isCancelled(cancellationRequested)) {
                    return juce::Result::fail("Feedback submission was cancelled");
                }
                continue;
            }
            return juce::Result::fail(errorFromResponse({}, statusCode));
        }
        const auto responseText = stream->readEntireStreamAsString();
        const auto parseResult = juce::JSON::parse(responseText, response);
        if (statusCode < 200 || statusCode >= 300) {
            return juce::Result::fail(errorFromResponse(response, statusCode));
        }
        if (parseResult.failed()) {
            return juce::Result::fail("Feedback service returned an invalid response");
        }
        return juce::Result::ok();
    }
    return juce::Result::fail("Could not connect to the feedback service");
}

} // namespace osci
