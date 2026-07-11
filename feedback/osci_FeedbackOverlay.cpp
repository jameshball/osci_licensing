#include "osci_FeedbackOverlay.h"

namespace osci {
namespace {
constexpr size_t maxScreenshotBytes = 10 * 1024 * 1024;
constexpr int maxUserScreenshots = 4;
constexpr int maxImageDimension = 8192;
constexpr juce::int64 maxImagePixels = 24'000'000;

void configureFeedbackLabel(juce::Label& label, juce::String text, float size, bool bold = false) {
    label.setText(std::move(text), juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, Colours::text());
    label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
}

juce::String safePngFilename(const juce::File& file) {
    auto name = file.getFileNameWithoutExtension().retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ").trim();
    if (name.isEmpty()) {
        name = "screenshot";
    }
    return name.substring(0, 220) + ".png";
}
} // namespace

FeedbackOverlay::FeedbackOverlay(FeedbackOverlayConfig configToUse)
    : OverlayComponent(configToUse.closeButtonSvg),
      juce::Thread("Feedback submission"),
      config(std::move(configToUse)),
      client(config.backend) {
    setName("Feedback");
    setComponentID("feedbackOverlay");
    setOverlayTitle("Send Feedback");
    setReserveHeaderSpace(true);

    configureFeedbackLabel(introLabel,
                   "Tell us what happened or what would make " + config.productDisplayName + " better. "
                   "Technical details are prepared automatically and remain under your control.",
                   12.5f);
    introLabel.setJustificationType(juce::Justification::topLeft);

    configureFeedbackLabel(kindLabel, "TYPE", 10.5f, true);
    kindBox.setName("Feedback type");
    kindBox.setComponentID("feedbackKind");
    kindBox.addItem("Bug report", 1);
    kindBox.addItem("Feature request", 2);
    kindBox.setSelectedId(1, juce::dontSendNotification);

    configureFeedbackLabel(emailLabel, "CONTACT EMAIL", 10.5f, true);
    configureEditor(emailEditor, "Contact email", false);
    emailEditor.setInputRestrictions(254);
    emailEditor.setText(config.context.contactEmail, false);

    configureFeedbackLabel(titleLabel, "TITLE", 10.5f, true);
    configureEditor(titleEditor, "Feedback title", false);
    titleEditor.setInputRestrictions(200);
    titleEditor.setTextToShowWhenEmpty("A short summary", Colours::textSubtle());

    configureFeedbackLabel(descriptionLabel, "DETAILS", 10.5f, true);
    configureEditor(descriptionEditor, "Feedback details", true);
    descriptionEditor.setInputRestrictions(20000);
    descriptionEditor.setTextToShowWhenEmpty("What happened? What did you expect? Steps to reproduce are especially useful.", Colours::textSubtle());

    configureFeedbackLabel(attachmentsHeading, "ADDITIONAL SCREENSHOTS", 10.5f, true);
    dropZone.setTitle("Drop screenshots here");
    dropZone.setSubtitle("PNG or JPEG, up to four additional images");
    dropZone.setActionText("Choose Images...");
    dropZone.setAcceptedDescription("PNG or JPEG image");
    dropZone.setIsFileAccepted([](const juce::File& file) {
        const auto extension = file.getFileExtension().toLowerCase();
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
    });
    dropZone.onBrowseRequested = [this] { chooseUserFiles(); };
    dropZone.onFilesDropped = [this](std::vector<juce::File> files) { addUserFiles(files); };
    dropZone.onRejectedFiles = [this](const std::vector<juce::File>&) {
        showInlineError("Only PNG and JPEG screenshots can be attached.");
    };

    configureFeedbackLabel(attachmentSummary, "No additional screenshots selected", 11.0f);
    attachmentSummary.setColour(juce::Label::textColourId, Colours::textSubtle());
    clearAttachmentsButton.setName("Clear added screenshots");
    clearAttachmentsButton.setComponentID("clearFeedbackAttachments");
    clearAttachmentsButton.onClick = [this] {
        userScreenshots.clear();
        updateAttachmentSummary();
    };

    configureFeedbackLabel(diagnosticsHeading, "INCLUDE WITH THIS REPORT", 10.5f, true);
    configureToggle(screenshotToggle, screenshotLabel, "App screenshot", "Screenshot of the UI before this form opened", !config.automaticScreenshot.data.isEmpty());
    configureToggle(logToggle, logLabel, "Diagnostic log", "Recent log entries, with personal paths and device names removed", config.context.log.isNotEmpty());
    configureToggle(projectToggle, projectLabel, "Current project", "A sanitized snapshot of the current .osci or .sosci project", !config.projectSnapshot.data.isEmpty());
    configureToggle(contextToggle, contextLabel, "Technical details", "Audio, renderer, host and display details", !config.context.clientContext.isVoid());

    screenshotPreview.setImage(config.automaticScreenshotPreview);
    screenshotPreview.setImagePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    screenshotToggle.onClick = [this] { updateToggleAvailability(); };

    configureFeedbackLabel(privacyLabel,
                   "Required product, operating-system and architecture fields are always included. "
                   "Your report and private attachments are only available to the support team.",
                   10.5f);
    privacyLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    privacyLabel.setJustificationType(juce::Justification::topLeft);

    configureFeedbackLabel(errorLabel, {}, 11.5f, true);
    errorLabel.setColour(juce::Label::textColourId, Colours::danger());
    errorLabel.setJustificationType(juce::Justification::centredLeft);
    errorLabel.setVisible(false);

    progressBar.setName("Feedback upload progress");
    progressBar.setComponentID("feedbackProgress");
    progressBar.setVisible(false);
    configureFeedbackLabel(progressLabel, {}, 11.0f);
    progressLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    progressLabel.setVisible(false);

    submitButton.setName("Send Feedback");
    submitButton.setComponentID("submitFeedback");
    submitButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor());
    submitButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    submitButton.onClick = [this] {
        if (success) {
            requestDismiss();
        } else {
            startSubmission();
        }
    };

    for (auto* component : std::initializer_list<juce::Component*> {
             &introLabel, &kindLabel, &kindBox, &emailLabel, &emailEditor, &titleLabel, &titleEditor,
             &descriptionLabel, &descriptionEditor, &attachmentsHeading, &dropZone, &attachmentSummary,
             &clearAttachmentsButton, &diagnosticsHeading, &screenshotToggle, &screenshotLabel,
             &screenshotPreview, &logToggle, &logLabel, &projectToggle, &projectLabel, &contextToggle,
             &contextLabel, &privacyLabel, &errorLabel, &progressBar, &progressLabel, &submitButton }) {
        addPanelContentAndMakeVisible(*component);
    }

    errorLabel.setVisible(false);
    progressBar.setVisible(false);
    progressLabel.setVisible(false);

    updateAttachmentSummary();
    updateToggleAvailability();
}

FeedbackOverlay::~FeedbackOverlay() {
    cancellationRequested.store(true, std::memory_order_relaxed);
    stopThread(config.backend.timeoutMs + 1000);
    cancelPendingUpdate();
}

void FeedbackOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    if (success) {
        submitButton.setBounds(contentArea.removeFromBottom(44).withSizeKeepingCentre(170, 40));
        contentArea.removeFromBottom(16);
        introLabel.setBounds(contentArea);
        return;
    }

    const int gap = 8;
    introLabel.setBounds(contentArea.removeFromTop(48));
    contentArea.removeFromTop(gap);

    auto firstRow = contentArea.removeFromTop(62);
    auto kindArea = firstRow.removeFromLeft(210);
    kindLabel.setBounds(kindArea.removeFromTop(18));
    kindBox.setBounds(kindArea.removeFromTop(36));
    firstRow.removeFromLeft(12);
    emailLabel.setBounds(firstRow.removeFromTop(18));
    emailEditor.setBounds(firstRow.removeFromTop(36));
    contentArea.removeFromTop(gap);

    titleLabel.setBounds(contentArea.removeFromTop(18));
    titleEditor.setBounds(contentArea.removeFromTop(38));
    contentArea.removeFromTop(gap);
    descriptionLabel.setBounds(contentArea.removeFromTop(18));
    descriptionEditor.setBounds(contentArea.removeFromTop(142));
    contentArea.removeFromTop(12);

    attachmentsHeading.setBounds(contentArea.removeFromTop(18));
    dropZone.setBounds(contentArea.removeFromTop(186));
    auto attachmentRow = contentArea.removeFromTop(32);
    clearAttachmentsButton.setBounds(attachmentRow.removeFromRight(86).reduced(0, 2));
    attachmentSummary.setBounds(attachmentRow);
    contentArea.removeFromTop(12);

    diagnosticsHeading.setBounds(contentArea.removeFromTop(18));
    auto layoutToggle = [&contentArea](jux::SwitchButton& toggle, juce::Label& label, juce::ImageComponent* preview = nullptr) {
        auto row = contentArea.removeFromTop(preview != nullptr ? 62 : 44);
        toggle.setBounds(row.removeFromLeft(44).withHeight(32));
        if (preview != nullptr) {
            preview->setBounds(row.removeFromRight(88).reduced(4, 2));
        }
        label.setBounds(row);
    };
    layoutToggle(screenshotToggle, screenshotLabel, &screenshotPreview);
    layoutToggle(logToggle, logLabel);
    layoutToggle(projectToggle, projectLabel);
    layoutToggle(contextToggle, contextLabel);
    contentArea.removeFromTop(4);
    privacyLabel.setBounds(contentArea.removeFromTop(40));

    errorLabel.setBounds(contentArea.removeFromTop(34));
    progressLabel.setBounds(contentArea.removeFromTop(24));
    progressBar.setBounds(contentArea.removeFromTop(12));
    contentArea.removeFromTop(10);
    submitButton.setBounds(contentArea.removeFromTop(40).removeFromRight(170));
}

juce::Point<int> FeedbackOverlay::getPreferredPanelSize() const {
    return getPanelSizeForContentSize(success ? juce::Point<int> { 600, 330 } : juce::Point<int> { 740, 1000 });
}

void FeedbackOverlay::run() {
    FeedbackResponse newResponse;
    const auto result = client.submit(
        pendingRequest,
        newResponse,
        [this](float progress, juce::String status) {
            backgroundProgress.store(progress, std::memory_order_relaxed);
            {
                const juce::SpinLock::ScopedLockType lock(resultLock);
                backgroundStatus = std::move(status);
            }
            triggerAsyncUpdate();
        },
        &cancellationRequested);
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        submissionResult = result;
        response = std::move(newResponse);
    }
    submissionFinished.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}

void FeedbackOverlay::handleAsyncUpdate() {
    progressValue = backgroundProgress.load(std::memory_order_relaxed);
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        progressLabel.setText(backgroundStatus, juce::dontSendNotification);
    }
    if (!submissionFinished.load(std::memory_order_acquire)) {
        return;
    }

    submissionActive = false;
    setDismissible(true);
    setFormEnabled(true);
    progressBar.setVisible(false);
    progressLabel.setVisible(false);
    juce::Result result = juce::Result::ok();
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        result = submissionResult;
    }
    if (result.failed()) {
        showInlineError(result.getErrorMessage());
        submitButton.setButtonText("Try Again");
        return;
    }
    showSuccess();
}

void FeedbackOverlay::configureEditor(juce::TextEditor& editor, juce::String name, bool multiline) {
    editor.setName(name);
    editor.setComponentID(name.replaceCharacter(' ', '_').toLowerCase());
    editor.setMultiLine(multiline, multiline);
    editor.setReturnKeyStartsNewLine(multiline);
    editor.setScrollbarsShown(multiline);
    editor.setColour(juce::TextEditor::backgroundColourId, Colours::surfaceSunken());
    editor.setColour(juce::TextEditor::outlineColourId, Colours::neutralStroke(0.18f));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor());
}

void FeedbackOverlay::configureToggle(jux::SwitchButton& toggle,
                                      juce::Label& label,
                                      juce::String name,
                                      juce::String detail,
                                      bool enabled) {
    toggle.setName(name);
    toggle.setComponentID(name.replaceCharacter(' ', '_').toLowerCase());
    toggle.setToggleState(enabled, juce::dontSendNotification);
    toggle.setEnabled(enabled);
    configureFeedbackLabel(label, name + "\n" + detail, 11.5f, true);
    label.setColour(juce::Label::textColourId, enabled ? Colours::text() : Colours::textSubtle());
    label.setJustificationType(juce::Justification::centredLeft);
}

void FeedbackOverlay::startSubmission() {
    if (submissionActive || !validateForm()) {
        return;
    }
    pendingRequest = config.context;
    pendingRequest.kind = kindBox.getSelectedId() == 2 ? FeedbackKind::featureRequest : FeedbackKind::bug;
    pendingRequest.title = titleEditor.getText().trim();
    pendingRequest.description = descriptionEditor.getText().trim();
    pendingRequest.contactEmail = emailEditor.getText().trim();
    pendingRequest.attachments = userScreenshots;
    if (screenshotToggle.getToggleState() && !config.automaticScreenshot.data.isEmpty()) {
        pendingRequest.attachments.insert(pendingRequest.attachments.begin(), config.automaticScreenshot);
    }
    if (projectToggle.getToggleState() && !config.projectSnapshot.data.isEmpty()) {
        pendingRequest.attachments.push_back(config.projectSnapshot);
    }
    if (!logToggle.getToggleState()) {
        pendingRequest.log.clear();
        pendingRequest.logTruncated = false;
    }
    if (!contextToggle.getToggleState()) {
        pendingRequest.clientContext = juce::var();
        pendingRequest.clientContextSchemaVersion = 0;
    }

    submissionActive = true;
    submissionFinished.store(false, std::memory_order_release);
    cancellationRequested.store(false, std::memory_order_relaxed);
    backgroundProgress.store(0.0f, std::memory_order_relaxed);
    errorLabel.setVisible(false);
    progressValue = 0.0;
    progressLabel.setText("Preparing feedback...", juce::dontSendNotification);
    progressLabel.setVisible(true);
    progressBar.setVisible(true);
    submitButton.setButtonText("Sending...");
    setDismissible(false);
    setFormEnabled(false);
    startThread();
}

bool FeedbackOverlay::validateForm() {
    const auto email = emailEditor.getText().trim();
    if (!email.containsChar('@') || email.startsWithChar('@') || email.endsWithChar('@')) {
        showInlineError("Enter a valid contact email address.");
        emailEditor.grabKeyboardFocus();
        return false;
    }
    if (titleEditor.getText().trim().isEmpty()) {
        showInlineError("Add a short title for your feedback.");
        titleEditor.grabKeyboardFocus();
        return false;
    }
    if (descriptionEditor.getText().trim().isEmpty()) {
        showInlineError("Tell us a little more about your feedback.");
        descriptionEditor.grabKeyboardFocus();
        return false;
    }
    errorLabel.setVisible(false);
    return true;
}

void FeedbackOverlay::addUserFiles(const std::vector<juce::File>& files) {
    int rejected = 0;
    for (const auto& file : files) {
        if (static_cast<int>(userScreenshots.size()) >= maxUserScreenshots) {
            ++rejected;
            continue;
        }
        if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > static_cast<juce::int64>(maxScreenshotBytes)) {
            ++rejected;
            continue;
        }
        const auto image = juce::ImageFileFormat::loadFrom(file);
        if (!image.isValid() || image.getWidth() > maxImageDimension || image.getHeight() > maxImageDimension
            || static_cast<juce::int64>(image.getWidth()) * image.getHeight() > maxImagePixels) {
            ++rejected;
            continue;
        }
        juce::MemoryBlock pngData;
        juce::MemoryOutputStream stream(pngData, false);
        juce::PNGImageFormat png;
        if (!png.writeImageToStream(image, stream) || pngData.getSize() > maxScreenshotBytes) {
            ++rejected;
            continue;
        }
        userScreenshots.push_back({ FeedbackAttachmentKind::screenshot, safePngFilename(file), "image/png", std::move(pngData) });
    }
    updateAttachmentSummary();
    if (rejected > 0) {
        showInlineError(juce::String(rejected) + (rejected == 1 ? " image was not added. " : " images were not added. ")
                        + "Use PNG or JPEG images under 10 MiB; up to four can be added.");
    }
}

void FeedbackOverlay::chooseUserFiles() {
    chooser = std::make_unique<juce::FileChooser>("Add screenshots", juce::File(), "*.png;*.jpg;*.jpeg");
    const auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::canSelectMultipleItems;
    const juce::Component::SafePointer<FeedbackOverlay> safeThis(this);
    chooser->launchAsync(flags, [safeThis](const juce::FileChooser& selected) {
        if (safeThis == nullptr) {
            return;
        }
        std::vector<juce::File> files;
        const auto results = selected.getResults();
        files.reserve(static_cast<size_t>(results.size()));
        for (const auto& file : results) {
            files.push_back(file);
        }
        safeThis->addUserFiles(files);
    });
}

void FeedbackOverlay::updateAttachmentSummary() {
    const auto count = static_cast<int>(userScreenshots.size());
    if (count == 0) {
        attachmentSummary.setText("No additional screenshots selected", juce::dontSendNotification);
        dropZone.setStatus(FileDropZoneComponent::Status::empty);
    } else {
        juce::String names;
        for (size_t index = 0; index < userScreenshots.size(); ++index) {
            if (index > 0) {
                names << ", ";
            }
            names << userScreenshots[index].filename;
        }
        attachmentSummary.setText(juce::String(count) + (count == 1 ? " screenshot: " : " screenshots: ") + names, juce::dontSendNotification);
        dropZone.setStatus(FileDropZoneComponent::Status::success, juce::String(count) + " ready");
    }
    clearAttachmentsButton.setVisible(count > 0);
    resized();
}

void FeedbackOverlay::updateToggleAvailability() {
    screenshotPreview.setAlpha(screenshotToggle.getToggleState() ? 1.0f : 0.35f);
}

void FeedbackOverlay::setFormEnabled(bool enabled) {
    for (auto* component : std::initializer_list<juce::Component*> {
             &kindBox, &emailEditor, &titleEditor, &descriptionEditor, &dropZone, &clearAttachmentsButton,
             &screenshotToggle, &logToggle, &projectToggle, &contextToggle, &submitButton }) {
        component->setEnabled(enabled);
    }
}

void FeedbackOverlay::showInlineError(juce::String message) {
    errorLabel.setText(std::move(message), juce::dontSendNotification);
    errorLabel.setVisible(true);
}

void FeedbackOverlay::showSuccess() {
    success = true;
    for (auto* component : std::initializer_list<juce::Component*> {
             &kindLabel, &kindBox, &emailLabel, &emailEditor, &titleLabel, &titleEditor, &descriptionLabel,
             &descriptionEditor, &attachmentsHeading, &dropZone, &attachmentSummary, &clearAttachmentsButton,
             &diagnosticsHeading, &screenshotToggle, &screenshotLabel, &screenshotPreview, &logToggle, &logLabel,
             &projectToggle, &projectLabel, &contextToggle, &contextLabel, &privacyLabel }) {
        component->setVisible(false);
    }
    introLabel.setText("Thank you. Your feedback has been sent.\n\nReference " + response.reference
                       + "\n\nKeep this reference if you contact support about the report.",
                       juce::dontSendNotification);
    introLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    introLabel.setJustificationType(juce::Justification::centred);
    errorLabel.setVisible(false);
    submitButton.setButtonText("Done");
    submitButton.setEnabled(true);
    requestOverlayLayout();
}

} // namespace osci
