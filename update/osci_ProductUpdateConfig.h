#pragma once

namespace osci {

struct ProductUpdateConfig {
    juce::String productSlug;
    juce::String productName;
    juce::String currentVersion;
    juce::String compiledVariant;
    juce::Image productIcon;
    juce::String copyIconSvg;
    juce::String revealIconSvg;
    juce::String concealIconSvg;
    juce::String helpIconSvg;
    LinuxInstallManifest linuxInstallManifest;
    juce::MemoryBlock linuxIconPng;
    bool hasFreeFallback = false;
    std::function<void()> onUpdateSettingsChanged;
};

} // namespace osci
