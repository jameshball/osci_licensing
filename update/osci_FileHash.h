#pragma once

namespace osci {

inline juce::String fileSha256Hex (const juce::File& file) {
    juce::FileInputStream input (file);
    if (! input.openedOk()) {
        return {};
    }

    return juce::SHA256 (input).toHexString().toLowerCase();
}

} // namespace osci
