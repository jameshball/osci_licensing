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
    LegalOverlay(juce::var documents, std::function<void()> onContinue, bool preferences = false,
                 juce::String repairProduct = {}, juce::String repairVersion = {})
        : bundle(std::move(documents)), documentsValid(LegalState::valid(bundle)), preferencesMode(preferences),
          product(std::move(repairProduct)), version(std::move(repairVersion)), continuation(std::move(onContinue)) {
        if (!documentsValid) {
            setDismissible(preferences);
            setOverlayTitle("Restoring Privacy & Terms");
            description.setText("Downloading the documents for this installation...", juce::dontSendNotification);
            description.setFont(juce::FontOptions(14.0f));
            description.setJustificationType(juce::Justification::topLeft);
            addPanelContentAndMakeVisible(description);
            proceed.setButtonText("Retry");
            proceed.setEnabled(false);
            proceed.onClick = [this] { repairDocuments(); };
            addPanelContentAndMakeVisible(proceed);
            installer.setButtonText("Get installer");
            installer.onClick = [] { juce::URL("https://osci-render.com/download").launchInDefaultBrowser(); };
            addPanelContentAndMakeVisible(installer);
            installer.setVisible(false);
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
        if (!documentsValid && getParentComponent() != nullptr && !repairStarted) {
            repairDocuments();
            return;
        }
        if (getParentComponent() != nullptr && !shownRecorded) {
            shownRecorded = state.recordShown(bundle);
        }
    }

    static void ensure(juce::Component& parent, const juce::var& bundle, std::function<void()> next,
                       juce::String product = {}, juce::String version = {}) {
        LegalState state;
        if (state.hasAcknowledged(bundle)) { next(); return; }
        auto overlay = std::make_unique<LegalOverlay>(bundle, std::move(next), false, std::move(product), std::move(version));
        OverlayComponent::show(parent, std::move(overlay));
    }

private:
    juce::var bundle;
    const bool documentsValid;
    const bool preferencesMode;
    const juce::String product;
    const juce::String version;
    LegalState state;
    bool requireAgreement = false;
    bool shownRecorded = false;
    bool repairStarted = false;
    std::function<void()> continuation;
    juce::Label description, statistics;
    juce::TextButton privacy, terms, proceed, installer, back, statisticsSettings;
    bool statisticsExpanded = false;
    LegalCheckbox agreement, disabled;
    juce::TextEditor reader;
    void repairDocuments() {
        repairStarted = true;
        if (product.isEmpty() || version.isEmpty()) {
            showRepairFailure("The installation details needed for an automatic repair are unavailable.");
            return;
        }
        setOverlayTitle("Restoring Privacy & Terms");
        description.setText("Downloading the documents for this installation...", juce::dontSendNotification);
        proceed.setEnabled(false);
        installer.setVisible(false);
        requestOverlayLayout();
        const juce::Component::SafePointer<LegalOverlay> owner(this);
        const auto repairProduct = product;
        const auto repairVersion = version;
        juce::Thread::launch([owner, repairProduct, repairVersion] {
            juce::var downloaded;
            auto result = BackendClient().getLegal(repairProduct, repairVersion, downloaded);
            if (result.wasOk() && !LegalState::cacheDocuments(repairProduct, repairVersion, downloaded)) {
                result = juce::Result::fail("The downloaded documents could not be saved.");
            }
            juce::MessageManager::callAsync([owner, result, downloaded, repairProduct, repairVersion] {
                if (owner == nullptr) {
                    return;
                }
                if (result.failed()) {
                    owner->showRepairFailure(result.getErrorMessage());
                    return;
                }
                auto next = std::move(owner->continuation);
                const bool preferences = owner->preferencesMode;
                LegalState state;
                if (!preferences && state.hasAcknowledged(downloaded)) {
                    owner->dismiss();
                    if (next != nullptr) {
                        juce::MessageManager::callAsync(std::move(next));
                    }
                    return;
                }
                owner->replaceWith(std::make_unique<LegalOverlay>(downloaded, std::move(next), preferences,
                                                                  repairProduct, repairVersion));
            });
        });
    }
    void showRepairFailure(const juce::String& detail) {
        setOverlayTitle("Privacy & Terms unavailable");
        description.setText("The documents could not be downloaded. Check the connection and try again.\n\n" + detail,
                            juce::dontSendNotification);
        proceed.setEnabled(true);
        installer.setVisible(true);
        requestOverlayLayout();
    }
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
        if (!documentsValid)
            return getPanelSizeForContentSize({512, labelHeight(description, width) + 50});
        const int contentHeight = reader.isVisible() ? 420 : labelHeight(description, width) + labelHeight(statistics, width) + 138 + (requireAgreement ? 48 : 0) + (statisticsExpanded ? 36 : 0);
        return getPanelSizeForContentSize({512, contentHeight});
    }
    void resizeContent(juce::Rectangle<int> area) override {
        if (!documentsValid) {
            auto buttons = area.removeFromBottom(34);
            proceed.setBounds(buttons.removeFromRight(120));
            if (installer.isVisible()) {
                buttons.removeFromRight(12);
                installer.setBounds(buttons.removeFromRight(120));
            }
            area.removeFromBottom(16);
            description.setBounds(area);
            return;
        }
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
