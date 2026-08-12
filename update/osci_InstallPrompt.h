#pragma once

namespace osci {

class InstallPrompt final {
public:
    struct Options {
        juce::Component* parent = nullptr;
        std::function<void()> onConfirmed;
        std::function<void()> onCancelled;
    };

    static void showConfirmation (Options options);
    static void showError (juce::Component* parent, juce::StringRef title, juce::StringRef message);

private:
    InstallPrompt() = delete;
};

} // namespace osci
