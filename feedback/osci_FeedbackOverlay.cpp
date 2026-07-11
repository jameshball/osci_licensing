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

void FeedbackSectionCard::paint(juce::Graphics& g) {
    constexpr auto radius = 14.0f;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours::surfaceRaised().withAlpha(0.42f));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(Colours::neutralStroke(0.16f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

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
                   "Tell us what happened or what would make " + config.productDisplayName + " better.\n"
                   "Technical details are prepared automatically and remain under your control.",
                   14.0f);
    introLabel.setJustificationType(juce::Justification::topLeft);

    configureFeedbackLabel(feedbackHeading, "Your feedback", 17.0f, true);
    configureFeedbackLabel(kindLabel, "Type", 13.0f, true);
    kindBox.setName("Feedback type");
    kindBox.setComponentID("feedbackKind");
    kindBox.addItem("Bug report", 1);
    kindBox.addItem("Feature request", 2);
    kindBox.setSelectedId(1, juce::dontSendNotification);

    configureFeedbackLabel(emailLabel, "Contact email", 13.0f, true);
    configureEditor(emailEditor, "Contact email", false);
    emailEditor.setInputRestrictions(254);
    emailEditor.setText(config.context.contactEmail, false);

    configureFeedbackLabel(titleLabel, "Title", 13.0f, true);
    configureEditor(titleEditor, "Feedback title", false);
    titleEditor.setInputRestrictions(200);
    titleEditor.setTextToShowWhenEmpty("A short summary", Colours::textSubtle());

    configureFeedbackLabel(descriptionLabel, "Details", 13.0f, true);
    configureEditor(descriptionEditor, "Feedback details", true);
    descriptionEditor.setInputRestrictions(20000);
    descriptionEditor.setTextToShowWhenEmpty("What happened? What did you expect? Steps to reproduce are especially useful.", Colours::textSubtle());

    configureFeedbackLabel(attachmentsHeading, "Screenshots", 17.0f, true);
    dropZone.setAccentColour(Colours::accentColor());
    dropZone.setTitle("Drop screenshots here");
    dropZone.setSubtitle("PNG or JPEG, up to four images, 10 MiB each");
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

    configureFeedbackLabel(attachmentSummary, "No additional screenshots selected", 12.5f);
    attachmentSummary.setColour(juce::Label::textColourId, Colours::textSubtle());
    clearAttachmentsButton.setName("Clear added screenshots");
    clearAttachmentsButton.setComponentID("clearFeedbackAttachments");
    clearAttachmentsButton.onClick = [this] {
        userScreenshots.clear();
        userScreenshotImages.clear();
        updateAttachmentSummary();
    };

    configureFeedbackLabel(diagnosticsHeading, "Include with this report", 17.0f, true);
    configureToggle(screenshotToggle, screenshotLabel, screenshotDetailLabel, "App screenshot", "Captured before this form opened", !config.automaticScreenshot.data.isEmpty());
    configureToggle(logToggle, logLabel, logDetailLabel, "Diagnostic log", "Recent entries with personal paths and device names removed", config.context.log.isNotEmpty());
    configureToggle(projectToggle, projectLabel, projectDetailLabel, "Current project", "Sanitized in-memory .osci or .sosci snapshot", !config.projectSnapshot.data.isEmpty());
    configureToggle(contextToggle, contextLabel, contextDetailLabel, "Technical details", "Audio, renderer, host and display information", !config.context.clientContext.isVoid());

    screenshotPreview.setComponentID("automaticScreenshotPreview");
    screenshotPreview.setCaption("Preview");
    screenshotPreview.setAccentColour(Colours::accentColor());
    screenshotPreview.setImage(config.automaticScreenshotPreview);
    screenshotPreview.onOpenRequested = [this] { openImagePreview(config.automaticScreenshotPreview, "App Screenshot"); };
    screenshotToggle.onClick = [this] { updateToggleAvailability(); };

    configureFeedbackLabel(privacyLabel,
                   "Required product, operating-system and architecture fields are always included.\n"
                   "Your report and private attachments are only available to the support team.",
                   12.5f);
    privacyLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    privacyLabel.setJustificationType(juce::Justification::topLeft);

    configureFeedbackLabel(errorLabel, {}, 12.5f, true);
    errorLabel.setColour(juce::Label::textColourId, Colours::danger());
    errorLabel.setJustificationType(juce::Justification::centredLeft);
    errorLabel.setVisible(false);

    progressBar.setName("Feedback upload progress");
    progressBar.setComponentID("feedbackProgress");
    progressBar.setVisible(false);
    configureFeedbackLabel(progressLabel, {}, 12.5f);
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
             &feedbackCard, &attachmentsCard, &diagnosticsCard, &submissionCard, &introLabel, &feedbackHeading,
             &kindLabel, &kindBox, &emailLabel, &emailEditor, &titleLabel, &titleEditor, &descriptionLabel,
             &descriptionEditor, &attachmentsHeading, &dropZone, &attachmentSummary, &clearAttachmentsButton,
             &userPreviewContainer, &diagnosticsHeading, &screenshotToggle, &screenshotLabel, &screenshotDetailLabel,
             &screenshotPreview, &logToggle, &logLabel, &logDetailLabel, &projectToggle, &projectLabel,
             &projectDetailLabel, &contextToggle, &contextLabel, &contextDetailLabel, &privacyLabel, &errorLabel,
             &progressBar, &progressLabel, &submitButton }) {
        addPanelContentAndMakeVisible(*component);
    }

    feedbackCard.toBack();
    attachmentsCard.toBack();
    diagnosticsCard.toBack();
    submissionCard.toBack();

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

    constexpr int sectionGap = 16;
    constexpr int cardPadding = 20;
    introLabel.setBounds(contentArea.removeFromTop(42));
    contentArea.removeFromTop(sectionGap);

    auto feedbackArea = contentArea.removeFromTop(350);
    feedbackCard.setBounds(feedbackArea);
    feedbackArea.reduce(cardPadding, cardPadding);
    feedbackHeading.setBounds(feedbackArea.removeFromTop(26));
    feedbackArea.removeFromTop(12);
    auto firstRow = feedbackArea.removeFromTop(62);
    auto kindArea = firstRow.removeFromLeft(220);
    kindLabel.setBounds(kindArea.removeFromTop(20));
    kindBox.setBounds(kindArea.removeFromTop(42));
    firstRow.removeFromLeft(16);
    emailLabel.setBounds(firstRow.removeFromTop(20));
    emailEditor.setBounds(firstRow.removeFromTop(42));
    feedbackArea.removeFromTop(12);
    titleLabel.setBounds(feedbackArea.removeFromTop(20));
    titleEditor.setBounds(feedbackArea.removeFromTop(42));
    feedbackArea.removeFromTop(12);
    descriptionLabel.setBounds(feedbackArea.removeFromTop(20));
    descriptionEditor.setBounds(feedbackArea.removeFromTop(104));
    contentArea.removeFromTop(sectionGap);

    const auto hasUserPreviews = !userScreenshotPreviews.empty();
    auto attachmentsArea = contentArea.removeFromTop(hasUserPreviews ? 420 : 304);
    attachmentsCard.setBounds(attachmentsArea);
    attachmentsArea.reduce(cardPadding, cardPadding);
    attachmentsHeading.setBounds(attachmentsArea.removeFromTop(26));
    attachmentsArea.removeFromTop(12);
    dropZone.setBounds(attachmentsArea.removeFromTop(184));
    attachmentsArea.removeFromTop(8);
    auto attachmentRow = attachmentsArea.removeFromTop(34);
    clearAttachmentsButton.setBounds(attachmentRow.removeFromRight(92).withSizeKeepingCentre(86, 30));
    attachmentSummary.setBounds(attachmentRow);
    if (hasUserPreviews) {
        attachmentsArea.removeFromTop(12);
        userPreviewContainer.setBounds(attachmentsArea.removeFromTop(104));
        const auto count = static_cast<int>(userScreenshotPreviews.size());
        constexpr int previewGap = 12;
        const auto previewWidth = juce::jmin(156, (userPreviewContainer.getWidth() - previewGap * juce::jmax(0, count - 1)) / juce::jmax(1, count));
        auto previewArea = userPreviewContainer.getLocalBounds();
        for (auto& preview : userScreenshotPreviews) {
            preview->setBounds(previewArea.removeFromLeft(previewWidth));
            previewArea.removeFromLeft(previewGap);
        }
    } else {
        userPreviewContainer.setBounds({});
    }
    contentArea.removeFromTop(sectionGap);

    auto diagnosticsArea = contentArea.removeFromTop(392);
    diagnosticsCard.setBounds(diagnosticsArea);
    diagnosticsArea.reduce(cardPadding, cardPadding);
    diagnosticsHeading.setBounds(diagnosticsArea.removeFromTop(26));
    diagnosticsArea.removeFromTop(10);
    auto layoutToggle = [](juce::Rectangle<int> row, jux::SwitchButton& toggle, juce::Label& label,
                           juce::Label& detailLabel, ImagePreviewComponent* preview = nullptr) {
        toggle.setBounds(row.removeFromLeft(44).withSizeKeepingCentre(44, 32));
        row.removeFromLeft(14);
        if (preview != nullptr) {
            preview->setBounds(row.removeFromRight(132).reduced(0, 4));
            row.removeFromRight(16);
        }
        auto textArea = row.withTrimmedTop(6).withTrimmedBottom(6);
        label.setBounds(textArea.removeFromTop(22));
        detailLabel.setBounds(textArea);
    };
    layoutToggle(diagnosticsArea.removeFromTop(86), screenshotToggle, screenshotLabel, screenshotDetailLabel, &screenshotPreview);
    layoutToggle(diagnosticsArea.removeFromTop(58), logToggle, logLabel, logDetailLabel);
    layoutToggle(diagnosticsArea.removeFromTop(58), projectToggle, projectLabel, projectDetailLabel);
    layoutToggle(diagnosticsArea.removeFromTop(58), contextToggle, contextLabel, contextDetailLabel);
    diagnosticsArea.removeFromTop(10);
    privacyLabel.setBounds(diagnosticsArea.removeFromTop(44));
    contentArea.removeFromTop(sectionGap);

    auto submissionArea = contentArea.removeFromTop(104);
    submissionCard.setBounds(submissionArea);
    submissionArea.reduce(cardPadding, 16);
    submitButton.setBounds(submissionArea.removeFromRight(184).withSizeKeepingCentre(184, 44));
    submissionArea.removeFromRight(18);
    errorLabel.setBounds(submissionArea.removeFromTop(30));
    progressLabel.setBounds(submissionArea.removeFromTop(22));
    progressBar.setBounds(submissionArea.removeFromTop(10));
}

juce::Point<int> FeedbackOverlay::getPreferredPanelSize() const {
    return getPanelSizeForContentSize(success ? juce::Point<int> { 600, 330 } : juce::Point<int> { 760, getFormContentHeight() });
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
    editor.setFont(juce::Font(juce::FontOptions(14.5f)));
    editor.setColour(juce::TextEditor::backgroundColourId, Colours::surfaceSunken());
    editor.setColour(juce::TextEditor::outlineColourId, Colours::neutralStroke(0.18f));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor());
}

void FeedbackOverlay::configureToggle(jux::SwitchButton& toggle,
                                      juce::Label& label,
                                      juce::Label& detailLabel,
                                      juce::String name,
                                      juce::String detail,
                                      bool enabled) {
    toggle.setName(name);
    toggle.setComponentID(name.replaceCharacter(' ', '_').toLowerCase());
    toggle.setToggleState(enabled, juce::dontSendNotification);
    toggle.setEnabled(enabled);
    configureFeedbackLabel(label, name, 14.0f, true);
    configureFeedbackLabel(detailLabel, detail, 13.0f);
    label.setColour(juce::Label::textColourId, enabled ? Colours::text() : Colours::textSubtle());
    detailLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
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
        userScreenshotImages.push_back(image);
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
    rebuildUserScreenshotPreviews();
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
    requestOverlayLayout();
}

void FeedbackOverlay::rebuildUserScreenshotPreviews() {
    userScreenshotPreviews.clear();
    userPreviewContainer.removeAllChildren();
    const juce::Component::SafePointer<FeedbackOverlay> safeThis(this);
    for (size_t index = 0; index < userScreenshotImages.size(); ++index) {
        auto preview = std::make_unique<ImagePreviewComponent>();
        preview->setComponentID("userScreenshotPreview" + juce::String(index + 1));
        preview->setAccentColour(Colours::accentColor());
        preview->setImage(userScreenshotImages[index]);
        const auto title = index < userScreenshots.size() ? userScreenshots[index].filename : "Screenshot " + juce::String(index + 1);
        preview->setCaption(title);
        const auto image = userScreenshotImages[index];
        preview->onOpenRequested = [safeThis, image, title] {
            if (safeThis != nullptr) {
                safeThis->openImagePreview(image, title);
            }
        };
        userPreviewContainer.addAndMakeVisible(*preview);
        userScreenshotPreviews.push_back(std::move(preview));
    }
    userPreviewContainer.setVisible(!userScreenshotPreviews.empty());
}

void FeedbackOverlay::updateToggleAvailability() {
    screenshotPreview.setAlpha(screenshotToggle.getToggleState() ? 1.0f : 0.52f);
}

void FeedbackOverlay::setFormEnabled(bool enabled) {
    for (auto* component : std::initializer_list<juce::Component*> {
             &kindBox, &emailEditor, &titleEditor, &descriptionEditor, &dropZone, &clearAttachmentsButton,
             &userPreviewContainer, &screenshotToggle, &screenshotPreview, &logToggle, &projectToggle,
             &contextToggle, &submitButton }) {
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
             &feedbackCard, &attachmentsCard, &diagnosticsCard, &submissionCard, &feedbackHeading, &kindLabel,
             &kindBox, &emailLabel, &emailEditor, &titleLabel, &titleEditor, &descriptionLabel, &descriptionEditor,
             &attachmentsHeading, &dropZone, &attachmentSummary, &clearAttachmentsButton, &userPreviewContainer,
             &diagnosticsHeading, &screenshotToggle, &screenshotLabel, &screenshotDetailLabel, &screenshotPreview,
             &logToggle, &logLabel, &logDetailLabel, &projectToggle, &projectLabel, &projectDetailLabel,
             &contextToggle, &contextLabel, &contextDetailLabel, &privacyLabel }) {
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

void FeedbackOverlay::openImagePreview(const juce::Image& image, juce::String title) {
    if (!image.isValid()) {
        return;
    }
    OverlayComponent::show(*this, std::make_unique<FeedbackImagePreviewOverlay>(config.closeButtonSvg, image, std::move(title)));
}

int FeedbackOverlay::getFormContentHeight() const {
    constexpr int introHeight = 42;
    constexpr int sectionGap = 16;
    constexpr int feedbackHeight = 350;
    constexpr int attachmentsHeight = 304;
    constexpr int previewHeight = 116;
    constexpr int diagnosticsHeight = 392;
    constexpr int submissionHeight = 104;
    return introHeight + sectionGap + feedbackHeight + sectionGap + attachmentsHeight
        + (userScreenshotPreviews.empty() ? 0 : previewHeight) + sectionGap + diagnosticsHeight + sectionGap + submissionHeight;
}

} // namespace osci
