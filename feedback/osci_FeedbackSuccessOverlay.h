#pragma once

namespace osci {

class FeedbackSuccessOverlay final : public OverlayComponent {
public:
    explicit FeedbackSuccessOverlay(juce::String reference);

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;
    juce::Point<int> getPreferredPanelSize() const override;

private:
    juce::Label confirmationLabel;
    juce::Label referenceLabel;
    juce::Label supportLabel;
    juce::TextButton doneButton { "Done" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSuccessOverlay)
};

} // namespace osci
