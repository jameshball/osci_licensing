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

void FeedbackFieldLabel::setField(juce::String text, bool required) {
    displayText = std::move(text);
    isRequired = required;
    setText(displayText + (isRequired ? " (required)" : juce::String()), juce::dontSendNotification);
    setName(displayText + (isRequired ? ", required" : juce::String()));
    setInterceptsMouseClicks(false, false);
}

void FeedbackFieldLabel::paint(juce::Graphics& g) {
    const juce::Font font(juce::FontOptions(13.0f, juce::Font::bold));
    g.setFont(font);
    g.setColour(Colours::text());
    const auto bounds = getLocalBounds();
    g.drawText(displayText, bounds, juce::Justification::centredLeft, false);
    if (isRequired) {
        const auto textWidth = juce::roundToInt(font.getStringWidthFloat(displayText));
        g.setColour(Colours::danger());
        g.drawText("*", bounds.withTrimmedLeft(textWidth + 4), juce::Justification::centredLeft, false);
    }
}

FeedbackSettingsButton::FeedbackSettingsButton(juce::String settingsSvg)
    : iconButton("Report settings", std::move(settingsSvg), Colours::textMuted(), Colours::text()) {
    setName("Report settings");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks(false, true);
    iconButton.setName("Report settings");
    iconButton.setComponentID("feedbackSettingsButton");
    iconButton.setTooltip("Report settings");
    iconButton.onClick = [this] {
        if (onClick != nullptr) {
            onClick();
        }
    };
    addAndMakeVisible(iconButton);
}

void FeedbackSettingsButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.75f);
    g.setColour(Colours::neutralFill(0.08f));
    g.fillEllipse(bounds);
    g.setColour(Colours::neutralStroke(0.28f));
    g.drawEllipse(bounds, 1.25f);
}

void FeedbackSettingsButton::resized() {
    iconButton.setBounds(getLocalBounds().reduced(10));
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

    includeAutomaticScreenshot = !config.automaticScreenshot.data.isEmpty() && config.automaticScreenshotPreview.isValid();
    includeDiagnosticLog = config.context.log.isNotEmpty();
    includeProjectSnapshot = !config.projectSnapshot.data.isEmpty();

    configureFeedbackLabel(introLabel,
                           "Tell us what happened or what would make " + config.productDisplayName + " better.\n"
                           "Technical app and system details are included automatically; optional diagnostics remain under your control.",
                           14.5f);
    introLabel.setJustificationType(juce::Justification::topLeft);

    configureFeedbackLabel(feedbackHeading, "Your feedback", 17.0f, true);
    kindLabel.setField("Type", true);
    kindBox.setName("Feedback type, required");
    kindBox.setComponentID("feedbackKind");
    kindBox.addItem("Bug report", 1);
    kindBox.addItem("Feature request", 2);
    kindBox.setSelectedId(1, juce::dontSendNotification);

    emailLabel.setField("Contact email", true);
    configureEditor(emailEditor, "Contact email", false);
    emailEditor.setInputRestrictions(254);
    emailEditor.setText(config.context.contactEmail, false);

    titleLabel.setField("Title", true);
    configureEditor(titleEditor, "Feedback title", false);
    titleEditor.setInputRestrictions(200);
    titleEditor.setTextToShowWhenEmpty("A short summary", Colours::textSubtle());

    descriptionLabel.setField("Details", true);
    configureEditor(descriptionEditor, "Feedback details", true);
    descriptionEditor.setInputRestrictions(20000);
    descriptionEditor.setTextToShowWhenEmpty("What happened? What did you expect? Steps to reproduce are especially useful.", Colours::textSubtle());

    emailEditor.onTextChange = [this] {
        if (validationAttempted && isValidEmail(emailEditor.getText().trim())) {
            setEditorValid(emailEditor, true);
        }
    };
    titleEditor.onTextChange = [this] {
        if (validationAttempted && titleEditor.getText().trim().isNotEmpty()) {
            setEditorValid(titleEditor, true);
        }
    };
    descriptionEditor.onTextChange = [this] {
        if (validationAttempted && descriptionEditor.getText().trim().isNotEmpty()) {
            setEditorValid(descriptionEditor, true);
        }
    };

    configureFeedbackLabel(attachmentsHeading, "Screenshots", 17.0f, true);
    dropZone.setAccentColour(Colours::accentColor());
    dropZone.setTitle("Drop screenshots here");
    dropZone.setSubtitle("PNG or JPEG • up to four images • 10 MiB each");
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

    configureFeedbackLabel(attachmentSummary, {}, 12.5f);
    attachmentSummary.setColour(juce::Label::textColourId, Colours::textSubtle());

    screenshotPreview.setComponentID("automaticScreenshotPreview");
    screenshotPreview.setCaption("App screenshot");
    screenshotPreview.setAccentColour(Colours::accentColor());
    screenshotPreview.setImage(config.automaticScreenshotPreview);
    screenshotPreview.onOpenRequested = [this] { openImagePreview(config.automaticScreenshotPreview, "App Screenshot"); };
    screenshotPreview.setRemoveAction([this] {
        includeAutomaticScreenshot = false;
        updateAttachmentSummary();
    }, "removeAutomaticScreenshot");

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

    settingsButton = std::make_unique<FeedbackSettingsButton>(config.settingsButtonSvg);
    settingsButton->onClick = [this] { openSettings(); };

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
             &feedbackCard, &attachmentsCard, &introLabel, &feedbackHeading, &kindLabel, &kindBox, &emailLabel,
             &emailEditor, &titleLabel, &titleEditor, &descriptionLabel, &descriptionEditor, &attachmentsHeading,
             &dropZone, &attachmentSummary, &previewContainer, &errorLabel, &progressBar, &progressLabel,
             settingsButton.get(), &submitButton }) {
        addPanelContentAndMakeVisible(*component);
    }

    feedbackCard.toBack();
    attachmentsCard.toBack();
    updateAttachmentSummary();
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

    const auto hasPreviews = getAttachedScreenshotCount() > 0;
    auto attachmentsArea = contentArea.removeFromTop(hasPreviews ? 438 : 322);
    attachmentsCard.setBounds(attachmentsArea);
    attachmentsArea.reduce(cardPadding, cardPadding);
    attachmentsHeading.setBounds(attachmentsArea.removeFromTop(26));
    attachmentsArea.removeFromTop(12);
    dropZone.setBounds(attachmentsArea.removeFromTop(204));
    attachmentsArea.removeFromTop(10);
    attachmentSummary.setBounds(attachmentsArea.removeFromTop(30));
    if (hasPreviews) {
        attachmentsArea.removeFromTop(12);
        previewContainer.setBounds(attachmentsArea.removeFromTop(104));
        const auto count = getAttachedScreenshotCount();
        constexpr int previewGap = 12;
        const auto previewWidth = juce::jmin(156, (previewContainer.getWidth() - previewGap * juce::jmax(0, count - 1)) / juce::jmax(1, count));
        auto previewArea = previewContainer.getLocalBounds();
        if (includeAutomaticScreenshot) {
            screenshotPreview.setBounds(previewArea.removeFromLeft(previewWidth));
            previewArea.removeFromLeft(previewGap);
        }
        for (auto& preview : userScreenshotPreviews) {
            preview->setBounds(previewArea.removeFromLeft(previewWidth));
            previewArea.removeFromLeft(previewGap);
        }
    } else {
        previewContainer.setBounds({});
    }
    contentArea.removeFromTop(sectionGap);

    auto footer = contentArea.removeFromTop(72);
    submitButton.setBounds(footer.removeFromRight(184).withSizeKeepingCentre(184, 44));
    footer.removeFromRight(12);
    settingsButton->setBounds(footer.removeFromRight(44).withSizeKeepingCentre(44, 44));
    footer.removeFromRight(18);
    errorLabel.setBounds(footer.removeFromTop(30));
    progressLabel.setBounds(footer.removeFromTop(22));
    progressBar.setBounds(footer.removeFromTop(10));
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

void FeedbackOverlay::configureEditor(TextEditor& editor, juce::String name, bool multiline) {
    editor.setName(name + ", required");
    editor.setComponentID(name.replaceCharacter(' ', '_').toLowerCase());
    editor.setMultiLine(multiline, multiline);
    editor.setReturnKeyStartsNewLine(multiline);
    editor.setScrollbarsShown(multiline);
    editor.setFont(juce::Font(juce::FontOptions(14.5f)));
    editor.setColour(juce::TextEditor::backgroundColourId, Colours::surfaceSunken());
    editor.setColour(juce::TextEditor::outlineColourId, Colours::neutralStroke(0.18f));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor());
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
    if (includeAutomaticScreenshot && !config.automaticScreenshot.data.isEmpty()) {
        pendingRequest.attachments.insert(pendingRequest.attachments.begin(), config.automaticScreenshot);
    }
    if (includeProjectSnapshot && !config.projectSnapshot.data.isEmpty()) {
        pendingRequest.attachments.push_back(config.projectSnapshot);
    }
    if (!includeDiagnosticLog) {
        pendingRequest.log.clear();
        pendingRequest.logTruncated = false;
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

bool FeedbackOverlay::validateForm(bool focusFirstInvalid) {
    validationAttempted = true;
    const auto emailValid = isValidEmail(emailEditor.getText().trim());
    const auto titleValid = titleEditor.getText().trim().isNotEmpty();
    const auto descriptionValid = descriptionEditor.getText().trim().isNotEmpty();
    setEditorValid(emailEditor, emailValid);
    setEditorValid(titleEditor, titleValid);
    setEditorValid(descriptionEditor, descriptionValid);

    const auto invalidCount = static_cast<int>(!emailValid) + static_cast<int>(!titleValid) + static_cast<int>(!descriptionValid);
    if (invalidCount == 0) {
        errorLabel.setVisible(false);
        return true;
    }

    if (!emailValid && invalidCount == 1) {
        showInlineError("Enter a valid contact email address.");
    } else if (!titleValid && invalidCount == 1) {
        showInlineError("Add a short title for your feedback.");
    } else if (!descriptionValid && invalidCount == 1) {
        showInlineError("Tell us a little more about your feedback.");
    } else {
        showInlineError("Complete the highlighted required fields before sending.");
    }

    if (focusFirstInvalid) {
        if (!emailValid) {
            emailEditor.grabKeyboardFocus();
        } else if (!titleValid) {
            titleEditor.grabKeyboardFocus();
        } else {
            descriptionEditor.grabKeyboardFocus();
        }
    }
    return false;
}

bool FeedbackOverlay::isValidEmail(juce::StringRef emailRef) const {
    const auto email = juce::String(emailRef).trim();
    if (email.containsAnyOf(" \t\r\n") || email.contains("..")) {
        return false;
    }
    const auto at = email.indexOfChar('@');
    if (at <= 0 || at != email.lastIndexOfChar('@') || at >= email.length() - 1) {
        return false;
    }
    const auto domain = email.substring(at + 1);
    const auto dot = domain.lastIndexOfChar('.');
    return dot > 0 && dot < domain.length() - 1;
}

void FeedbackOverlay::setEditorValid(TextEditor& editor, bool valid) {
    editor.setColour(juce::TextEditor::outlineColourId, valid ? Colours::neutralStroke(0.18f) : Colours::danger());
    editor.setColour(juce::TextEditor::focusedOutlineColourId, valid ? Colours::accentColor() : Colours::danger());
    editor.repaint();
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

void FeedbackOverlay::removeUserScreenshot(size_t index) {
    if (index >= userScreenshots.size() || index >= userScreenshotImages.size()) {
        return;
    }
    userScreenshots.erase(userScreenshots.begin() + static_cast<std::ptrdiff_t>(index));
    userScreenshotImages.erase(userScreenshotImages.begin() + static_cast<std::ptrdiff_t>(index));
    updateAttachmentSummary();
}

void FeedbackOverlay::updateAttachmentSummary() {
    rebuildScreenshotPreviews();
    const auto count = getAttachedScreenshotCount();
    if (count == 0) {
        attachmentSummary.setText("No screenshots attached", juce::dontSendNotification);
        dropZone.setStatus(FileDropZoneComponent::Status::empty);
    } else {
        juce::String names;
        if (includeAutomaticScreenshot) {
            names = "App screenshot";
        }
        for (const auto& screenshot : userScreenshots) {
            if (names.isNotEmpty()) {
                names << ", ";
            }
            names << screenshot.filename;
        }
        attachmentSummary.setText(juce::String(count) + (count == 1 ? " screenshot attached: " : " screenshots attached: ") + names,
                                  juce::dontSendNotification);
        dropZone.setStatus(FileDropZoneComponent::Status::success, juce::String(count) + " ready");
    }
    requestOverlayLayout();
}

void FeedbackOverlay::rebuildScreenshotPreviews() {
    userScreenshotPreviews.clear();
    previewContainer.removeAllChildren();
    if (includeAutomaticScreenshot) {
        previewContainer.addAndMakeVisible(screenshotPreview);
    }

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
        preview->setRemoveAction([safeThis, index] {
            if (safeThis != nullptr) {
                safeThis->removeUserScreenshot(index);
            }
        }, "removeUserScreenshot" + juce::String(index + 1));
        previewContainer.addAndMakeVisible(*preview);
        userScreenshotPreviews.push_back(std::move(preview));
    }
    previewContainer.setVisible(getAttachedScreenshotCount() > 0);
}

void FeedbackOverlay::setFormEnabled(bool enabled) {
    for (auto* component : std::initializer_list<juce::Component*> {
             &kindBox, &emailEditor, &titleEditor, &descriptionEditor, &dropZone, &previewContainer,
             settingsButton.get(), &submitButton }) {
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
             &feedbackCard, &attachmentsCard, &feedbackHeading, &kindLabel, &kindBox, &emailLabel, &emailEditor,
             &titleLabel, &titleEditor, &descriptionLabel, &descriptionEditor, &attachmentsHeading, &dropZone,
             &attachmentSummary, &previewContainer, settingsButton.get() }) {
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

void FeedbackOverlay::openSettings() {
    const juce::Component::SafePointer<FeedbackOverlay> safeThis(this);
    OverlayComponent::show(
        *this,
        std::make_unique<FeedbackSettingsOverlay>(
            config.closeButtonSvg,
            includeDiagnosticLog,
            config.context.log.isNotEmpty(),
            includeProjectSnapshot,
            !config.projectSnapshot.data.isEmpty(),
            [safeThis](bool includeLog, bool includeProject) {
                if (safeThis != nullptr) {
                    safeThis->includeDiagnosticLog = includeLog;
                    safeThis->includeProjectSnapshot = includeProject;
                }
            }));
}

int FeedbackOverlay::getAttachedScreenshotCount() const {
    return static_cast<int>(userScreenshotPreviews.size()) + static_cast<int>(includeAutomaticScreenshot);
}

int FeedbackOverlay::getFormContentHeight() const {
    constexpr int introHeight = 42;
    constexpr int sectionGap = 16;
    constexpr int feedbackHeight = 350;
    constexpr int attachmentsHeight = 322;
    constexpr int previewHeight = 116;
    constexpr int footerHeight = 72;
    return introHeight + sectionGap + feedbackHeight + sectionGap + attachmentsHeight
        + (getAttachedScreenshotCount() > 0 ? previewHeight : 0) + sectionGap + footerHeight;
}

} // namespace osci
