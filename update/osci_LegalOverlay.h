#pragma once

namespace osci {

// A visible unchecked state is important on the dark notice background.
class LegalCheckbox final : public juce::ToggleButton {
public:
    LegalCheckbox() {
        setMouseClickGrabsKeyboardFocus(false);
    }

private:
    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        const auto box = juce::Rectangle<float>(4.0f, (getHeight() - 16.0f) * 0.5f, 16.0f, 16.0f);
        g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.8f : 0.55f));
        g.drawRoundedRectangle(box, 3.0f, 1.2f);
        if (getToggleState()) {
            juce::Path tick;
            tick.startNewSubPath(box.getX() + 3, box.getY() + 8);
            tick.lineTo(box.getX() + 6, box.getY() + 11);
            tick.lineTo(box.getX() + 13, box.getY() + 4);
            g.setColour(juce::Colours::white);
            g.strokePath(tick, juce::PathStrokeType(1.8f));
        }
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText(getButtonText(), getLocalBounds().withTrimmedLeft(28), juce::Justification::centredLeft, 3);
        if (hasKeyboardFocus(true)) {
            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
        }
    }
};

class LegalOverlay final : public OverlayComponent {
public:
    LegalOverlay(juce::var documents, std::function<void()> onContinue, bool preferences = false)
        : bundle(std::move(documents)), continuation(std::move(onContinue)) {
        if (!LegalState::valid(bundle)) {
            setOverlayTitle("Installation documents unavailable");
            setDismissible(preferences);
            description.setText("Open the installer to repair this installation and restore its Privacy & Terms documents. An internet connection is needed for the repair.", juce::dontSendNotification);
            description.setFont(juce::FontOptions(14.0f));
            description.setJustificationType(juce::Justification::topLeft);
            addPanelContentAndMakeVisible(description);
            proceed.setButtonText("Get installer");
            proceed.onClick = [] { juce::URL("https://osci-render.com/download").launchInDefaultBrowser(); };
            addPanelContentAndMakeVisible(proceed);
            return;
        }
        const bool firstPrivacy = !state.hasSeenOtherRevision(bundle, "privacy");
        const bool firstTerms = !state.hasSeenOtherRevision(bundle, "terms");
        requireAgreement = !state.termsAccepted(bundle);
        setOverlayTitle(preferences ? "Privacy & Terms" : (firstPrivacy && firstTerms ? "Before continuing" : "Privacy & Terms"));
        setDismissible(preferences);
        juce::String intro = "Review the terms and privacy policy for osci-render and sosci.";
        if (!firstPrivacy && !state.privacyAcknowledged(bundle)) {
            const auto summary = bundle["documents"]["privacy"]["summary"].toString();
            intro += "\n\nPrivacy Policy updated. " + summary;
        }
        if (!firstTerms && requireAgreement) {
            intro += "\n\nTerms updated. " + bundle["documents"]["terms"]["summary"].toString();
        }
        description.setText(intro, juce::dontSendNotification);
        description.setFont(juce::FontOptions(14.0f));
        description.setJustificationType(juce::Justification::topLeft);
        addPanelContentAndMakeVisible(description);
        privacy.setButtonText("Privacy Policy"); terms.setButtonText("Terms & Conditions");
        privacy.onClick = [this] { showDocument("privacy"); };
        terms.onClick = [this] { showDocument("terms"); };
        addPanelContentAndMakeVisible(privacy); addPanelContentAndMakeVisible(terms);
        reader.setMultiLine(true); reader.setReadOnly(true); reader.setScrollbarsShown(true);
        reader.setFont(juce::FontOptions(14.0f));
        reader.setIndents(12, 12);
        reader.setText("Select a document above to read the exact revision, including while offline.");
        addPanelContentAndMakeVisible(reader);
        reader.setVisible(false);
        back.setButtonText("Back");
        back.onClick = [this] { reader.setVisible(false); updateVisibility(); requestOverlayLayout(); };
        addPanelContentAndMakeVisible(back);
        agreement.setButtonText("I agree to the terms and acknowledge the privacy policy");
        agreement.setVisible(requireAgreement);
        addPanelContentAndMakeVisible(agreement);
        agreement.setVisible(requireAgreement);
        disabled.setButtonText("Disable optional version statistics");
        disabled.setToggleState(state.statisticsDisabled(), juce::dontSendNotification);
        disabled.onClick = [this] {
            if (!state.setStatisticsDisabled(disabled.getToggleState())) {
                statistics.setText("The statistics preference could not be saved. Please try again.", juce::dontSendNotification);
            }
        };
        addPanelContentAndMakeVisible(disabled);
        statistics.setText("Help improve osci-render and sosci by sharing anonymous version and platform counts.", juce::dontSendNotification);
        statistics.setFont(juce::FontOptions(13.0f));
        statistics.setJustificationType(juce::Justification::topLeft);
        addPanelContentAndMakeVisible(statistics);
        statisticsSettings.setButtonText("Statistics settings");
        statisticsSettings.onClick = [this] { statisticsExpanded = !statisticsExpanded; updateVisibility(); requestOverlayLayout(); };
        addPanelContentAndMakeVisible(statisticsSettings);
        proceed.setButtonText(preferences ? "Save" : "Continue");
        proceed.setEnabled(!requireAgreement);
        agreement.onClick = [this] { proceed.setEnabled(!requireAgreement || agreement.getToggleState()); };
        proceed.onClick = [this] {
            if (!state.acknowledge(bundle, !requireAgreement || agreement.getToggleState(), state.statisticsDisabled())) {
                statistics.setText("The choice could not be saved. Please try again. Statistics remain unavailable until the choice is saved.", juce::dontSendNotification);
                requestOverlayLayout();
                return;
            }
            auto next = std::move(continuation);
            dismiss();
            if (next) { juce::MessageManager::callAsync(std::move(next)); }
        };
        addPanelContentAndMakeVisible(proceed);
        updateVisibility();
    }

    void parentHierarchyChanged() override {
        OverlayComponent::parentHierarchyChanged();
        if (getParentComponent() != nullptr && !shownRecorded) {
            shownRecorded = state.recordShown(bundle);
        }
    }

    static void ensure(juce::Component& parent, const juce::var& bundle, std::function<void()> next) {
        LegalState state;
        if (state.hasAcknowledged(bundle)) { next(); return; }
        auto overlay = std::make_unique<LegalOverlay>(bundle, std::move(next));
        OverlayComponent::show(parent, std::move(overlay));
    }

private:
    juce::var bundle;
    LegalState state;
    bool requireAgreement = false;
    bool shownRecorded = false;
    std::function<void()> continuation;
    juce::Label description, statistics;
    juce::TextButton privacy, terms, proceed, back, statisticsSettings;
    bool statisticsExpanded = false;
    LegalCheckbox agreement, disabled;
    juce::TextEditor reader;
    void showDocument(const char* kind) {
        const auto document = bundle["documents"][kind];
        reader.setText(document["text"].toString(), false);
        reader.setCaretPosition(0);
        reader.setVisible(true);
        updateVisibility();
        requestOverlayLayout();
    }
    void updateVisibility() {
        const bool reading = reader.isVisible();
        for (auto* component : std::initializer_list<juce::Component*>{&description, &privacy, &terms, &statistics, &statisticsSettings, &proceed}) {
            component->setVisible(!reading);
        }
        agreement.setVisible(!reading && requireAgreement);
        disabled.setVisible(!reading && statisticsExpanded);
        back.setVisible(reading);
    }
    int labelHeight(const juce::Label& label, int width) const {
        juce::AttributedString text;
        text.append(label.getText(), label.getFont(), juce::Colours::white);
        juce::TextLayout layout;
        layout.createLayout(text, static_cast<float>(juce::jmax(120, width - 4)));
        return juce::jmax(36, juce::roundToInt(layout.getHeight()) + 8);
    }
    juce::Point<int> getPreferredPanelSize() const override {
        const auto width = juce::jmax(160, juce::jmin(560, getWidth() - 80) - 48);
        const int contentHeight = reader.isVisible() ? 420 : labelHeight(description, width) + labelHeight(statistics, width) + 138 + (requireAgreement ? 48 : 0) + (statisticsExpanded ? 36 : 0);
        return getPanelSizeForContentSize({512, contentHeight});
    }
    void resizeContent(juce::Rectangle<int> area) override {
        if (reader.isVisible()) {
            back.setBounds(area.removeFromBottom(32).removeFromLeft(100));
            area.removeFromBottom(12);
            reader.setBounds(area);
            return;
        }
        description.setBounds(area.removeFromTop(labelHeight(description, area.getWidth())));
        auto links = area.removeFromTop(34);
        privacy.setBounds(links.removeFromLeft((links.getWidth() - 12) / 2)); links.removeFromLeft(12);
        terms.setBounds(links); area.removeFromTop(12);
        if (requireAgreement) { agreement.setBounds(area.removeFromTop(42)); area.removeFromTop(6); }
        statistics.setBounds(area.removeFromTop(labelHeight(statistics, area.getWidth())));
        statisticsSettings.setBounds(area.removeFromTop(28).removeFromLeft(160));
        if (statisticsExpanded) { disabled.setBounds(area.removeFromTop(36)); }
        area.removeFromTop(16);
        proceed.setBounds(area.removeFromTop(34).removeFromRight(120));
    }
};

} // namespace osci
