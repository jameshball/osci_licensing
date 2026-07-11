#include "osci_FeedbackComponents.h"

namespace osci {

void FeedbackCard::paint(juce::Graphics& g) {
    constexpr auto radius = 14.0f;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours::surfaceRaised().withAlpha(0.42f));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(Colours::neutralStroke(0.16f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

} // namespace osci
