#pragma once

namespace osci {

enum class FeedbackKind {
    bug,
    featureRequest,
};

enum class FeedbackAttachmentKind {
    screenshot,
    project,
};

struct FeedbackAttachmentData {
    FeedbackAttachmentKind kind = FeedbackAttachmentKind::screenshot;
    juce::String filename;
    juce::String contentType;
    juce::MemoryBlock data;
};

struct FeedbackRequest {
    FeedbackKind kind = FeedbackKind::bug;
    juce::String title;
    juce::String description;
    juce::String contactEmail;
    juce::String productSlug;
    juce::String productVersion;
    juce::String productBuild;
    juce::String releaseTrack;
    juce::String productVariant;
    juce::String platform;
    juce::String osName;
    juce::String osVersion;
    juce::String osBuild;
    juce::String architecture;
    juce::String locale;
    juce::String hostApplication;
    juce::String hostApplicationVersion;
    juce::String pluginFormat;
    int displayWidth = 0;
    int displayHeight = 0;
    double displayScale = 0.0;
    int clientContextSchemaVersion = 0;
    juce::var clientContext;
    juce::String log;
    bool logTruncated = false;
    juce::String licenseToken;
    std::vector<FeedbackAttachmentData> attachments;
};

struct FeedbackResponse {
    juce::String reference;
    juce::String createdAt;
    bool verifiedCustomer = false;
};

class FeedbackClient final {
public:
    using ProgressCallback = std::function<void(float progress, juce::String status)>;

    explicit FeedbackClient(BackendClientConfig config = {});

    juce::Result submit(const FeedbackRequest& request,
                        FeedbackResponse& response,
                        ProgressCallback progress = {},
                        const std::atomic<bool>* cancellationRequested = nullptr) const;

    static juce::String kindToString(FeedbackKind kind);
    static juce::String attachmentKindToString(FeedbackAttachmentKind kind);

private:
    struct PreparedUpload {
        juce::String id;
        juce::String url;
        juce::String method;
        juce::StringPairArray headers;
    };

    BackendClientConfig config;

    juce::String endpoint(juce::StringRef path) const;
    juce::Result prepareUploads(const FeedbackRequest& request,
                                juce::StringRef idempotencyKey,
                                std::vector<PreparedUpload>& uploads) const;
    juce::Result uploadAttachment(const PreparedUpload& upload,
                                  const FeedbackAttachmentData& attachment,
                                  const std::atomic<bool>* cancellationRequested) const;
    juce::Result createFeedback(const FeedbackRequest& request,
                                const std::vector<PreparedUpload>& uploads,
                                juce::StringRef idempotencyKey,
                                FeedbackResponse& response) const;
    juce::Result postJson(juce::StringRef path,
                          const juce::var& body,
                          juce::StringRef idempotencyKey,
                          juce::var& response) const;
};

} // namespace osci
