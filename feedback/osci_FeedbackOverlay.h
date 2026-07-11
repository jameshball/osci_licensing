#pragma once

namespace osci {

class FeedbackSectionCard final : public juce::Component {
public:
    void paint(juce::Graphics& g) override;
};

class FeedbackFieldLabel final : public juce::Label {
public:
    void setField(juce::String text, bool required);
    void paint(juce::Graphics& g) override;

private:
    juce::String displayText;
    bool isRequired = false;
};

struct FeedbackOverlayConfig {
    juce::String closeButtonSvg;
    juce::String settingsButtonSvg;
    FeedbackRequest context;
    FeedbackAttachmentData automaticScreenshot;
    juce::Image automaticScreenshotPreview;
    FeedbackAttachmentData projectSnapshot;
    std::function<void(FeedbackRequest&, FeedbackAttachmentData&, bool)> submissionProvider;
    BackendClientConfig backend;
};

class FeedbackOverlay final : public OverlayComponent,
                              private juce::Thread,
                              private juce::AsyncUpdater {
public:
    explicit FeedbackOverlay(FeedbackOverlayConfig config);
    ~FeedbackOverlay() override;

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;
    juce::Point<int> getPreferredPanelSize() const override;

private:
    void run() override;
    void handleAsyncUpdate() override;
    void configureEditor(TextEditor& editor, juce::String name, bool multiline);
    void startSubmission();
    bool validateForm(bool focusFirstInvalid = true);
    bool isValidEmail(juce::StringRef email) const;
    void setEditorValid(TextEditor& editor, bool valid);
    void addUserFiles(const std::vector<juce::File>& files);
    void chooseUserFiles();
    void removeUserScreenshot(size_t index);
    void updateAttachmentSummary();
    void rebuildScreenshotPreviews();
    void setFormEnabled(bool enabled);
    void showInlineError(juce::String message);
    void showSuccess();
    void openImagePreview(const juce::Image& image, juce::String title);
    void openSettings();
    int getAttachedScreenshotCount() const;
    int getFormContentHeight() const;

    FeedbackOverlayConfig config;
    FeedbackClient client;
    FeedbackRequest pendingRequest;
    FeedbackResponse response;
    juce::Result submissionResult = juce::Result::ok();
    std::vector<FeedbackAttachmentData> userScreenshots;
    std::vector<juce::Image> userScreenshotImages;
    std::vector<std::unique_ptr<ImagePreviewComponent>> userScreenshotPreviews;
    std::unique_ptr<juce::FileChooser> chooser;

    FeedbackSectionCard feedbackCard;
    FeedbackSectionCard attachmentsCard;
    juce::Label introLabel;
    AnimatedTextButton bugKindButton { "Bug report" };
    AnimatedTextButton featureKindButton { "Feature request" };
    FeedbackFieldLabel emailLabel;
    TextEditor emailEditor { "feedbackEmail" };
    FeedbackFieldLabel titleLabel;
    TextEditor titleEditor { "feedbackTitle" };
    FeedbackFieldLabel descriptionLabel;
    TextEditor descriptionEditor { "feedbackDescription" };
    juce::Label attachmentsHeading;
    FileDropZoneComponent dropZone;
    juce::Label attachmentSummary;
    juce::Component previewContainer;
    ImagePreviewComponent screenshotPreview;

    juce::Label errorLabel;
    std::unique_ptr<SvgButton> settingsButton;
    juce::TextButton submitButton { "Send Feedback" };

    juce::SpinLock resultLock;
    std::atomic<bool> cancellationRequested { false };
    std::atomic<bool> submissionFinished { false };
    bool includeAutomaticScreenshot = false;
    bool includeDiagnosticLog = false;
    bool includeProjectSnapshot = false;
    bool submissionActive = false;
    bool success = false;
    bool validationAttempted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackOverlay)
};

} // namespace osci
