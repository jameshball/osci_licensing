#pragma once

namespace osci {

class FeedbackSettingsOverlay final : public OverlayComponent {
public:
    FeedbackSettingsOverlay(juce::String closeButtonSvg,
                            bool includeLog,
                            bool logAvailable,
                            bool includeProject,
                            bool projectAvailable,
                            std::function<void(bool, bool)> settingsChanged);

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;
    juce::Point<int> getPreferredPanelSize() const override;

private:
    void notifySettingsChanged();
    void configureToggle(jux::SwitchButton& toggle,
                         juce::Label& label,
                         juce::Label& detailLabel,
                         juce::String name,
                         juce::String detail,
                         bool enabled,
                         bool available);

    FeedbackCard card;
    jux::SwitchButton logToggle { "Include diagnostic log", false };
    juce::Label logLabel;
    juce::Label logDetailLabel;
    jux::SwitchButton projectToggle { "Include current project", false };
    juce::Label projectLabel;
    juce::Label projectDetailLabel;
    juce::Label alwaysIncludedHeading;
    juce::Label alwaysIncludedLabel;
    juce::Label privacyLabel;
    std::function<void(bool, bool)> onSettingsChanged;
    bool logIsAvailable = false;
    bool projectIsAvailable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSettingsOverlay)
};

} // namespace osci
