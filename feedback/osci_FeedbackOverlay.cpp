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

bool encodePng(const juce::Image& image, juce::MemoryBlock& destination) {
    if (!image.isValid()) {
        return false;
    }

    juce::PNGImageFormat png;
    juce::MemoryOutputStream output(destination, false);
    if (!png.writeImageToStream(image, output)) {
        destination.reset();
        return false;
    }

    return true;
}
} // namespace

void FeedbackFieldLabel::setField(juce::String text, bool required) {
    displayText = std::move(text);
    isRequired = required;
    setText(displayText + (isRequired ? " (required)" : juce::String()), juce::dontSendNotification);
    setName(displayText + (isRequired ? ", required" : juce::String()));
    setInterceptsMouseClicks(false, false);
}

void FeedbackFieldLabel::paint(juce::Graphics& g) {
    const juce::Font font(juce::FontOptions(14.0f, juce::Font::bold));
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

FeedbackOverlay::FeedbackOverlay(FeedbackOverlayConfig configToUse)
    : OverlayComponent(configToUse.closeButtonSvg),
      juce::Thread("Feedback submission"),
      config(std::move(configToUse)),
      client(config.backend) {
    setName("Feedback");
    setComponentID("feedbackOverlay");
    setOverlayTitle("Send Feedback");
    setReserveHeaderSpace(true);

    includeAutomaticScreenshot = config.automaticScreenshotPreview.isValid();
    includeDiagnosticLog = config.context.log.isNotEmpty() || config.submissionProvider != nullptr;
    includeProjectSnapshot = !config.projectSnapshot.data.isEmpty() || config.submissionProvider != nullptr;

    auto configureKindButton = [](AnimatedTextButton& button, juce::String componentID) {
        button.setComponentID(std::move(componentID));
        button.setClickingTogglesState(true);
        button.setRadioGroupId(0x46656564);
        button.setColour(juce::TextButton::buttonColourId, Colours::neutralFill(0.10f));
        button.setColour(juce::TextButton::buttonOnColourId, Colours::accentColor());
        button.setColour(juce::TextButton::textColourOffId, Colours::text());
        button.setColour(juce::TextButton::textColourOnId, Colours::textOnAccent());
        button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    };
    configureKindButton(bugKindButton, "feedbackKindBug");
    configureKindButton(featureKindButton, "feedbackKindFeature");
    bugKindButton.setToggleState(true, juce::dontSendNotification);
    bugKindButton.snapAnimationToToggleState();
    featureKindButton.snapAnimationToToggleState();

    emailLabel.setField("Contact email", true);
    configureEditor(emailEditor, "Contact email", false);
    emailEditor.setInputRestrictions(254);
    emailEditor.setKeyboardType(juce::TextInputTarget::emailAddressKeyboard);
    emailEditor.setTextToShowWhenEmpty("you@example.com", Colours::textSubtle());
    emailEditor.setText(config.context.contactEmail, false);

    titleLabel.setField("Title", true);
    configureEditor(titleEditor, "Feedback title", false);
    titleEditor.setInputRestrictions(200);
    titleEditor.setTextToShowWhenEmpty("A short summary", Colours::textSubtle());

    descriptionLabel.setField("Details", true);
    configureEditor(descriptionEditor, "Feedback details", true);
    descriptionEditor.setInputRestrictions(20000);
    descriptionEditor.setTextToShowWhenEmpty("What happened? What did you expect? Steps to reproduce are especially useful.", Colours::textSubtle());

    formValidator.registerField("contactEmail", emailEditor,
                                StringValidation().trim()
                                    .required("Enter a contact email address.")
                                    .email("Enter a valid contact email address."));
    formValidator.registerField("title", titleEditor,
                                StringValidation().trim()
                                    .required("Add a short title for your feedback."));
    formValidator.registerField("description", descriptionEditor,
                                StringValidation().trim()
                                    .required("Tell us a little more about your feedback."));

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

    configureFeedbackLabel(attachmentSummary, {}, 13.5f);
    attachmentSummary.setColour(juce::Label::textColourId, Colours::textSubtle());

    screenshotPreview.setComponentID("automaticScreenshotPreview");
    screenshotPreview.setCaption("App screenshot");
    screenshotPreview.setAccentColour(Colours::accentColor());
    screenshotPreview.setMagnifierSvg(config.magnifierSvg);
    screenshotPreview.setImage(config.automaticScreenshotPreview);
    screenshotPreview.onOpenRequested = [this] { openImagePreview(config.automaticScreenshotPreview, "App Screenshot"); };
    screenshotPreview.setRemoveAction([this] {
        includeAutomaticScreenshot = false;
        updateAttachmentSummary();
    }, config.closeButtonSvg, "removeAutomaticScreenshot");

    configureFeedbackLabel(errorLabel, {}, 13.5f, true);
    errorLabel.setColour(juce::Label::textColourId, Colours::danger());
    errorLabel.setJustificationType(juce::Justification::centredLeft);
    errorLabel.setVisible(false);
    formValidator.onValidationChanged = [this](const FormValidator::Result& result) { updateValidationMessage(result); };

    settingsButton = std::make_unique<SvgButton>("Report settings", config.settingsButtonSvg, Colours::text());
    settingsButton->setComponentID("feedbackSettingsButton");
    settingsButton->setCircularBackground(true, 10);
    settingsButton->setRotateOnHover(true, juce::MathConstants<float>::pi / 3.0f);
    settingsButton->setHoverColour(Colours::text());
    settingsButton->onClick = [this] { openSettings(); };

    submitButton.setName("Send Feedback");
    submitButton.setComponentID("submitFeedback");
    submitButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor());
    submitButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    submitButton.onClick = [this] { startSubmission(); };

    for (auto* component : std::initializer_list<juce::Component*> {
             &feedbackCard, &attachmentsCard, &bugKindButton, &featureKindButton,
             &emailLabel, &emailEditor, &titleLabel, &titleEditor, &descriptionLabel, &descriptionEditor, &attachmentsHeading,
             &dropZone, &attachmentSummary, &previewContainer, &errorLabel,
             settingsButton.get(), &submitButton }) {
        addPanelContentAndMakeVisible(*component);
    }

    feedbackCard.toBack();
    attachmentsCard.toBack();
    errorLabel.setVisible(false);
    updateAttachmentSummary();
}

FeedbackOverlay::~FeedbackOverlay() {
    cancellationRequested.store(true, std::memory_order_relaxed);
    stopThread(config.backend.timeoutMs + 1000);
    cancelPendingUpdate();
}

void FeedbackOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    constexpr int sectionGap = 16;
    constexpr int cardPadding = 20;
    auto feedbackArea = contentArea.removeFromTop(346);
    feedbackCard.setBounds(feedbackArea);
    feedbackArea.reduce(cardPadding, cardPadding);
    auto kindButtons = feedbackArea.removeFromTop(42);
    const auto buttonWidth = (kindButtons.getWidth() - 10) / 2;
    bugKindButton.setBounds(kindButtons.removeFromLeft(buttonWidth));
    kindButtons.removeFromLeft(10);
    featureKindButton.setBounds(kindButtons);
    feedbackArea.removeFromTop(10);
    emailLabel.setBounds(feedbackArea.removeFromTop(20));
    emailEditor.setBounds(feedbackArea.removeFromTop(42));
    feedbackArea.removeFromTop(10);
    titleLabel.setBounds(feedbackArea.removeFromTop(20));
    titleEditor.setBounds(feedbackArea.removeFromTop(42));
    feedbackArea.removeFromTop(10);
    descriptionLabel.setBounds(feedbackArea.removeFromTop(20));
    descriptionEditor.setBounds(feedbackArea.removeFromTop(90));
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
}

juce::Point<int> FeedbackOverlay::getPreferredPanelSize() const {
    return getPanelSizeForContentSize({ 760, getFormContentHeight() });
}

void FeedbackOverlay::run() {
    auto projectSnapshot = config.projectSnapshot;
    if (config.submissionProvider != nullptr) {
        config.submissionProvider(pendingRequest,
                                  projectSnapshot,
                                  { includeDiagnosticLog, includeProjectSnapshot });
    }

    if (includeAutomaticScreenshot && config.automaticScreenshotPreview.isValid()) {
        auto automaticScreenshot = config.automaticScreenshot;
        if (automaticScreenshot.data.isEmpty()) {
            encodePng(config.automaticScreenshotPreview, automaticScreenshot.data);
        }
        if (!automaticScreenshot.data.isEmpty()) {
            pendingRequest.attachments.insert(pendingRequest.attachments.begin(), std::move(automaticScreenshot));
        }
    }

    if (includeProjectSnapshot) {
        if (!projectSnapshot.data.isEmpty()) {
            pendingRequest.attachments.push_back(std::move(projectSnapshot));
        }
    }

    FeedbackResponse newResponse;
    const auto result = client.submit(pendingRequest, newResponse, &cancellationRequested);
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        submissionResult = result;
        response = std::move(newResponse);
    }
    triggerAsyncUpdate();
}

void FeedbackOverlay::handleAsyncUpdate() {
    submissionActive = false;
    setDismissible(true);
    juce::Result result = juce::Result::ok();
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        result = submissionResult;
    }
    if (result.failed()) {
        setFormEnabled(true);
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
    editor.setFont(juce::Font(juce::FontOptions(15.5f)));
    editor.setColour(juce::TextEditor::backgroundColourId, Colours::surfaceSunken());
    editor.setColour(juce::TextEditor::outlineColourId, Colours::neutralStroke(0.18f));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor());
}

void FeedbackOverlay::startSubmission() {
    if (submissionActive || !validateForm()) {
        return;
    }
    pendingRequest = config.context;
    pendingRequest.kind = featureKindButton.getToggleState() ? FeedbackKind::featureRequest : FeedbackKind::bug;
    pendingRequest.title = titleEditor.getText().trim();
    pendingRequest.description = descriptionEditor.getText().trim();
    pendingRequest.contactEmail = emailEditor.getText().trim();
    pendingRequest.attachments = userScreenshots;
    submissionActive = true;
    cancellationRequested.store(false, std::memory_order_relaxed);
    errorLabel.setVisible(false);
    submitButton.setButtonText("Sending...");
    setDismissible(false);
    setFormEnabled(false);
    startThread();
}

bool FeedbackOverlay::validateForm(bool focusFirstInvalid) {
    return formValidator.validate(focusFirstInvalid).isValid();
}

void FeedbackOverlay::updateValidationMessage(const FormValidator::Result& result) {
    if (result.isValid()) {
        errorLabel.setVisible(false);
        return;
    }

    const auto* firstIssue = result.getFirstIssue();
    if (result.getIssueCount() == 1 && firstIssue != nullptr) {
        showInlineError(firstIssue->error.message);
    } else {
        showInlineError("Complete the highlighted required fields before sending.");
    }
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
        if (userScreenshots.empty()) {
            dropZone.setStatus(FileDropZoneComponent::Status::empty);
        } else {
            dropZone.setStatus(FileDropZoneComponent::Status::success, juce::String(userScreenshots.size()) + " ready");
        }
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
        preview->setMagnifierSvg(config.magnifierSvg);
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
        }, config.closeButtonSvg, "removeUserScreenshot" + juce::String(index + 1));
        previewContainer.addAndMakeVisible(*preview);
        userScreenshotPreviews.push_back(std::move(preview));
    }
    previewContainer.setVisible(getAttachedScreenshotCount() > 0);
}

void FeedbackOverlay::setFormEnabled(bool enabled) {
    for (auto* component : std::initializer_list<juce::Component*> {
             &bugKindButton, &featureKindButton, &emailEditor, &titleEditor, &descriptionEditor, &dropZone, &previewContainer,
             settingsButton.get(), &submitButton }) {
        component->setEnabled(enabled);
    }
}

void FeedbackOverlay::showInlineError(juce::String message) {
    errorLabel.setText(std::move(message), juce::dontSendNotification);
    errorLabel.setVisible(true);
}

void FeedbackOverlay::showSuccess() {
    replaceWith(std::make_unique<FeedbackSuccessOverlay>(config.closeButtonSvg, response.reference));
}

void FeedbackOverlay::openImagePreview(const juce::Image& image, juce::String title) {
    if (!image.isValid()) {
        return;
    }
    OverlayComponent::show(*this, std::make_unique<ImagePreviewOverlay>(config.closeButtonSvg, image, std::move(title)));
}

void FeedbackOverlay::openSettings() {
    const juce::Component::SafePointer<FeedbackOverlay> safeThis(this);
    OverlayComponent::show(
        *this,
        std::make_unique<FeedbackSettingsOverlay>(
            config.closeButtonSvg,
            includeDiagnosticLog,
            config.context.log.isNotEmpty() || config.submissionProvider != nullptr,
            includeProjectSnapshot,
            !config.projectSnapshot.data.isEmpty() || config.submissionProvider != nullptr,
            [safeThis](bool includeLog, bool includeProject) {
                if (safeThis != nullptr) {
                    safeThis->includeDiagnosticLog = includeLog;
                    safeThis->includeProjectSnapshot = includeProject;
                }
            }));
}

int FeedbackOverlay::getAttachedScreenshotCount() const {
    return static_cast<int>(userScreenshots.size()) + static_cast<int>(includeAutomaticScreenshot);
}

int FeedbackOverlay::getFormContentHeight() const {
    constexpr int sectionGap = 16;
    constexpr int feedbackHeight = 346;
    constexpr int attachmentsHeight = 322;
    constexpr int previewHeight = 116;
    constexpr int footerHeight = 72;
    return feedbackHeight + sectionGap + attachmentsHeight
        + (getAttachedScreenshotCount() > 0 ? previewHeight : 0) + sectionGap + footerHeight;
}

} // namespace osci
