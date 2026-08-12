#include "osci_FeedbackSettingsOverlay.h"

namespace osci {
namespace {
void configureSettingsLabel(juce::Label& label, juce::String text, float size, bool bold = false) {
    label.setText(std::move(text), juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, Colours::text());
    label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    label.setJustificationType(juce::Justification::topLeft);
    label.setInterceptsMouseClicks(false, false);
}
} // namespace

FeedbackSettingsOverlay::FeedbackSettingsOverlay(bool includeLog,
                                                 bool logAvailable,
                                                 bool includeProject,
                                                 bool projectAvailable,
                                                 std::function<void(bool, bool)> settingsChanged)
    : onSettingsChanged(std::move(settingsChanged)),
      logIsAvailable(logAvailable),
      projectIsAvailable(projectAvailable) {
    setName("Feedback settings");
    setComponentID("feedbackSettingsOverlay");
    setOverlayTitle("Report Settings");
    setReserveHeaderSpace(true);

    configureToggle(logToggle,
                    logLabel,
                    logDetailLabel,
                    "Diagnostic log",
                    "Recent app diagnostics from a privacy-safe set of log categories",
                    includeLog,
                    logIsAvailable);
    configureToggle(projectToggle,
                    projectLabel,
                    projectDetailLabel,
                    "Current project",
                    "Sanitized in-memory .osci or .sosci snapshot",
                    includeProject,
                    projectIsAvailable);

    configureSettingsLabel(alwaysIncludedHeading, "Always included", 16.0f, true);
    configureSettingsLabel(alwaysIncludedLabel,
                           "Product and app version, operating system and architecture, locale, host and plugin format, "
                           "display size and scale, plus audio and renderer configuration.",
                           13.5f);
    alwaysIncludedLabel.setColour(juce::Label::textColourId, Colours::textMuted());

    configureSettingsLabel(privacyLabel,
                           "Your report and private attachments are only available to the support team.",
                           13.0f);
    privacyLabel.setColour(juce::Label::textColourId, Colours::textSubtle());

    for (auto* component : std::initializer_list<juce::Component*> {
             &card, &logToggle, &logLabel, &logDetailLabel, &projectToggle, &projectLabel,
             &projectDetailLabel, &alwaysIncludedHeading, &alwaysIncludedLabel, &privacyLabel }) {
        addPanelContentAndMakeVisible(*component);
    }
    card.toBack();

    logToggle.onClick = [this] { notifySettingsChanged(); };
    projectToggle.onClick = [this] { notifySettingsChanged(); };
}

void FeedbackSettingsOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    card.setBounds(contentArea);
    contentArea.reduce(12, 8);

    auto layoutToggle = [](juce::Rectangle<int> row,
                           jux::SwitchButton& toggle,
                           juce::Label& label,
                           juce::Label& detailLabel) {
        toggle.setBounds(row.removeFromLeft(30).withSizeKeepingCentre(30, 32));
        row.removeFromLeft(2);
        auto textArea = row.withTrimmedTop(2).withTrimmedBottom(2);
        label.setBounds(textArea.removeFromTop(20));
        detailLabel.setBounds(textArea);
    };

    if (logIsAvailable) {
        layoutToggle(contentArea.removeFromTop(42), logToggle, logLabel, logDetailLabel);
    }
    if (projectIsAvailable) {
        layoutToggle(contentArea.removeFromTop(42), projectToggle, projectLabel, projectDetailLabel);
    }

    contentArea.removeFromTop(5);
    alwaysIncludedHeading.setBounds(contentArea.removeFromTop(20));
    alwaysIncludedLabel.setBounds(contentArea.removeFromTop(34));
    contentArea.removeFromTop(4);
    privacyLabel.setBounds(contentArea.removeFromTop(18));
}

juce::Point<int> FeedbackSettingsOverlay::getPreferredPanelSize() const {
    const auto availableRows = static_cast<int>(logIsAvailable) + static_cast<int>(projectIsAvailable);
    return getPanelSizeForContentSize({ 470, 97 + availableRows * 42 });
}

void FeedbackSettingsOverlay::notifySettingsChanged() {
    if (onSettingsChanged != nullptr) {
        onSettingsChanged(logToggle.getToggleState(), projectToggle.getToggleState());
    }
}

void FeedbackSettingsOverlay::configureToggle(jux::SwitchButton& toggle,
                                              juce::Label& label,
                                              juce::Label& detailLabel,
                                              juce::String name,
                                              juce::String detail,
                                              bool enabled,
                                              bool available) {
    toggle.setName(name);
    toggle.setComponentID(name.replaceCharacter(' ', '_').toLowerCase());
    toggle.setToggleState(enabled && available, juce::dontSendNotification);
    toggle.setVisible(available);
    configureSettingsLabel(label, name, 15.0f, true);
    configureSettingsLabel(detailLabel, std::move(detail), 13.5f);
    detailLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    label.setVisible(available);
    detailLabel.setVisible(available);
}

} // namespace osci
