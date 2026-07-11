#include "osci_FeedbackImagePreviewOverlay.h"

namespace osci {

FeedbackImagePreviewOverlay::FeedbackImagePreviewOverlay(juce::String closeButtonSvg, juce::Image imageToUse, juce::String title)
    : OverlayComponent(closeButtonSvg), image(std::move(imageToUse)) {
    setName("Screenshot preview");
    setComponentID("feedbackImagePreviewOverlay");
    setOverlayTitle(title.isNotEmpty() ? title : "Screenshot Preview");
    setReserveHeaderSpace(true);

    canvas.setImage(image);
    addPanelContentAndMakeVisible(canvas);

    metadataLabel.setText(juce::String(image.getWidth()) + " × " + juce::String(image.getHeight()) + " px", juce::dontSendNotification);
    metadataLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    metadataLabel.setColour(juce::Label::textColourId, Colours::textSubtle());
    metadataLabel.setJustificationType(juce::Justification::centred);
    addPanelContentAndMakeVisible(metadataLabel);
}

void FeedbackImagePreviewOverlay::resizeContent(juce::Rectangle<int> contentArea) {
    metadataLabel.setBounds(contentArea.removeFromBottom(34));
    contentArea.removeFromBottom(10);
    canvas.setBounds(contentArea);
}

juce::Point<int> FeedbackImagePreviewOverlay::getPreferredPanelSize() const {
    return getPanelSizeForContentSize({ 820, 500 });
}

void FeedbackImagePreviewOverlay::ImageCanvas::setImage(juce::Image newImage) {
    image = std::move(newImage);
    repaint();
}

void FeedbackImagePreviewOverlay::ImageCanvas::paint(juce::Graphics& g) {
    constexpr auto radius = 14.0f;
    auto bounds = getLocalBounds().toFloat();
    {
        const juce::Graphics::ScopedSaveState saveState(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, radius);
        g.reduceClipRegion(clip);
        drawImageCheckerboard(g, bounds.getSmallestIntegerContainer(), 16);

        if (image.isValid()) {
            const auto destination = juce::RectanglePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize)
                                         .appliedTo(image.getBounds().toFloat(), bounds.reduced(18.0f));
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(image, destination);
        }
    }

    g.setColour(Colours::neutralStroke(0.22f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

} // namespace osci
