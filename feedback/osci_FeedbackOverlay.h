#pragma once

namespace osci {

struct FeedbackOverlayConfig {
    juce::String closeButtonSvg;
    juce::String productDisplayName;
    FeedbackRequest context;
    FeedbackAttachmentData automaticScreenshot;
    juce::Image automaticScreenshotPreview;
    FeedbackAttachmentData projectSnapshot;
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
    void configureEditor(juce::TextEditor& editor, juce::String name, bool multiline);
    void configureToggle(jux::SwitchButton& toggle, juce::Label& label, juce::String name, juce::String detail, bool enabled);
    void startSubmission();
    bool validateForm();
    void addUserFiles(const std::vector<juce::File>& files);
    void chooseUserFiles();
    void updateAttachmentSummary();
    void updateToggleAvailability();
    void setFormEnabled(bool enabled);
    void showInlineError(juce::String message);
    void showSuccess();

    FeedbackOverlayConfig config;
    FeedbackClient client;
    FeedbackRequest pendingRequest;
    FeedbackResponse response;
    juce::Result submissionResult = juce::Result::ok();
    std::vector<FeedbackAttachmentData> userScreenshots;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label introLabel;
    juce::Label kindLabel;
    juce::ComboBox kindBox;
    juce::Label emailLabel;
    TextEditor emailEditor { "feedbackEmail" };
    juce::Label titleLabel;
    TextEditor titleEditor { "feedbackTitle" };
    juce::Label descriptionLabel;
    TextEditor descriptionEditor { "feedbackDescription" };
    juce::Label attachmentsHeading;
    FileDropZoneComponent dropZone;
    juce::Label attachmentSummary;
    juce::TextButton clearAttachmentsButton { "Clear" };
    juce::Label diagnosticsHeading;

    jux::SwitchButton screenshotToggle { "Include screenshot", false };
    juce::Label screenshotLabel;
    juce::ImageComponent screenshotPreview;
    jux::SwitchButton logToggle { "Include diagnostic log", false };
    juce::Label logLabel;
    jux::SwitchButton projectToggle { "Include current project", false };
    juce::Label projectLabel;
    jux::SwitchButton contextToggle { "Include technical details", false };
    juce::Label contextLabel;
    juce::Label privacyLabel;

    juce::Label errorLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::TextButton submitButton { "Send Feedback" };

    juce::SpinLock resultLock;
    juce::String backgroundStatus;
    std::atomic<float> backgroundProgress { 0.0f };
    std::atomic<bool> cancellationRequested { false };
    std::atomic<bool> submissionFinished { false };
    bool submissionActive = false;
    bool success = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackOverlay)
};

} // namespace osci
