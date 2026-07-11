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

FeedbackSettingsOverlay::FeedbackSettingsOverlay(juce::String closeButtonSvg,
                                                 bool includeLog,
                                                 bool logAvailable,
                                                 bool includeProject,
                                                 bool projectAvailable,
                                                 std::function<void(bool, bool)> settingsChanged)
    : OverlayComponent(std::move(closeButtonSvg)),
      onSettingsChanged(std::move(settingsChanged)),
      logIsAvailable(logAvailable),
      projectIsAvailable(projectAvailable) {
    setName("Feedback settings");
    setComponentID("feedbackSettingsOverlay");
    setOverlayTitle("Report Settings");
    setReserveHeaderSpace(true);

    configureSettingsLabel(optionalHeading, "Optional attachments", 17.0f, true);
    configureToggle(logToggle,
                    logLabel,
                    logDetailLabel,
                    "Diagnostic log",
                    "Recent osci-render entries with personal paths and device names removed",
                    includeLog,
                    logIsAvailable);
    configureToggle(projectToggle,
                    projectLabel,
                    projectDetailLabel,
                    "Current project",
                    "Sanitized in-memory .osci or .sosci snapshot",
                    includeProject,
                    projectIsAvailable);

    configureSettingsLabel(alwaysIncludedHeading, "Always included", 15.0f, true);
    configureSettingsLabel(alwaysIncludedLabel,
                           "Product and app version, operating system and architecture, locale, host and plugin format, "
                           "display size and scale, plus audio and renderer configuration.",
                           12.5f);
    alwaysIncludedLabel.setColour(juce::Label::textColourId, Colours::textMuted());

    configureSettingsLabel(privacyLabel,
                           "Your report and private attachments are only available to the support team.",
                           12.0f);
    privacyLabel.setColour(juce::Label::textColourId, Colours::textSubtle());

    for (auto* component : std::initializer_list<juce::Component*> {
             &card, &optionalHeading, &logToggle, &logLabel, &logDetailLabel, &projectToggle, &projectLabel,
             &projectDetailLabel, &alwaysIncludedHeading, &alwaysIncludedLabel, &privacyLabel }) {
        addPanelContentAndMakeVisible(*component);
    }
    card.toBack();

    logToggle.onClick = [this] { notifySettingsChanged(); };
    projectToggle.onClick = [this] { notifySettingsChanged(); };
}

void FeedbackSettingsOverlay::SettingsCard::paint(juce::Graphics& g) {
    constexpr auto radius = 14.0f;
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours::surfaceRaised().withAlpha(0.42f));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(Colours::neutralStroke(0.16f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void FeedbackSettingsOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    card.setBounds(contentArea);
    contentArea.reduce(22, 20);
    optionalHeading.setBounds(contentArea.removeFromTop(26));
    contentArea.removeFromTop(10);

    auto layoutToggle = [](juce::Rectangle<int> row,
                           jux::SwitchButton& toggle,
                           juce::Label& label,
                           juce::Label& detailLabel) {
        toggle.setBounds(row.removeFromLeft(44).withSizeKeepingCentre(44, 32));
        row.removeFromLeft(14);
        auto textArea = row.withTrimmedTop(5).withTrimmedBottom(5);
        label.setBounds(textArea.removeFromTop(22));
        detailLabel.setBounds(textArea);
    };

    if (logIsAvailable) {
        layoutToggle(contentArea.removeFromTop(70), logToggle, logLabel, logDetailLabel);
    }
    if (projectIsAvailable) {
        layoutToggle(contentArea.removeFromTop(70), projectToggle, projectLabel, projectDetailLabel);
    }

    contentArea.removeFromTop(12);
    alwaysIncludedHeading.setBounds(contentArea.removeFromTop(24));
    contentArea.removeFromTop(4);
    alwaysIncludedLabel.setBounds(contentArea.removeFromTop(58));
    contentArea.removeFromTop(10);
    privacyLabel.setBounds(contentArea.removeFromTop(34));
}

juce::Point<int> FeedbackSettingsOverlay::getPreferredPanelSize() const {
    const auto availableRows = static_cast<int>(logIsAvailable) + static_cast<int>(projectIsAvailable);
    return getPanelSizeForContentSize({ 600, 224 + availableRows * 70 });
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
    configureSettingsLabel(label, name, 14.0f, true);
    configureSettingsLabel(detailLabel, std::move(detail), 12.5f);
    detailLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    label.setVisible(available);
    detailLabel.setVisible(available);
}

} // namespace osci
