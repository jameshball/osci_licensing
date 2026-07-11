#include "osci_FeedbackSuccessOverlay.h"

namespace osci {
namespace {
void configureSuccessLabel(juce::Label& label, juce::String text, float size, bool bold, juce::Colour colour) {
    label.setText(std::move(text), juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
}
} // namespace

FeedbackSuccessOverlay::FeedbackSuccessOverlay(juce::String closeButtonSvg, juce::String reference)
    : OverlayComponent(std::move(closeButtonSvg)) {
    setName("Feedback sent");
    setComponentID("feedbackSuccessOverlay");
    setOverlayTitle("Feedback Sent");
    setReserveHeaderSpace(true);

    configureSuccessLabel(confirmationLabel, "Thank you. Your feedback has been sent.", 18.0f, true, Colours::text());
    configureSuccessLabel(referenceLabel, "Reference " + std::move(reference), 18.0f, true, Colours::text());
    configureSuccessLabel(supportLabel, "Keep this reference if you contact support about the report.", 14.0f, false, Colours::textMuted());

    doneButton.setName("Done");
    doneButton.setComponentID("dismissFeedbackSuccess");
    doneButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor());
    doneButton.setColour(juce::TextButton::textColourOffId, Colours::textOnAccent());
    doneButton.onClick = [this] { requestDismiss(); };

    for (auto* component : std::initializer_list<juce::Component*> {
             &confirmationLabel, &referenceLabel, &supportLabel, &doneButton }) {
        addPanelContentAndMakeVisible(*component);
    }
}

void FeedbackSuccessOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    confirmationLabel.setBounds(contentArea.removeFromTop(34));
    contentArea.removeFromTop(8);
    referenceLabel.setBounds(contentArea.removeFromTop(34));
    contentArea.removeFromTop(6);
    supportLabel.setBounds(contentArea.removeFromTop(28));
    doneButton.setBounds(contentArea.removeFromBottom(42).withSizeKeepingCentre(170, 42));
}

juce::Point<int> FeedbackSuccessOverlay::getPreferredPanelSize() const {
    return getPanelSizeForContentSize({ 520, 190 });
}

} // namespace osci
