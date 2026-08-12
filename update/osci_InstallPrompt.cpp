#include "osci_InstallPrompt.h"

namespace osci {
namespace {

juce::String makeInstallWarningMessage (const juce::Array<DetectedDawProcess>& detectedDaws) {
#if JUCE_LINUX
    juce::String message = "Keep this app or plugin host open until installation finishes. Restart it afterward to use the update.";
#else
    juce::String message = "Save your work before continuing. If this is running inside a DAW, close the host before completing the installer.";
#endif

    if (!detectedDaws.isEmpty()) {
#if JUCE_LINUX
        message << "\n\nDetected running DAW/plugin host processes: " << DawProcessDetector::joinDisplayNames (detectedDaws)
                << ".\n\nKeep them open until installation finishes, then restart them.";
#else
        message << "\n\nDetected running DAW/plugin host processes: " << DawProcessDetector::joinDisplayNames (detectedDaws)
                << ".\n\nClose them before completing the installer.";
#endif
    }

    return message;
}

} // namespace

void InstallPrompt::showConfirmation (Options options) {
    const auto hasParent = options.parent != nullptr;
    auto safeParent = juce::Component::SafePointer<juce::Component> (options.parent);
    DawProcessDetector::scanAsync (
        [hasParent, safeParent, options = std::move (options)] (juce::Array<DetectedDawProcess> detectedDaws) mutable {
            if (hasParent && safeParent == nullptr) {
                return;
            }

            const auto message = makeInstallWarningMessage (detectedDaws);
            auto confirmed = std::make_shared<std::function<void()>> (std::move (options.onConfirmed));
            auto cancelled = std::make_shared<std::function<void()>> (std::move (options.onCancelled));
            if (hasParent) {
                ErrorOverlay::Options overlayOptions;
                overlayOptions.closeButtonSvg = std::move (options.closeButtonSvg);
                overlayOptions.title = "Install Update";
                overlayOptions.message = message;
                overlayOptions.icon = ErrorOverlay::Icon::Warning;
                overlayOptions.messageJustification = juce::Justification::centredTop;
                overlayOptions.preferredPanelSize = detectedDaws.isEmpty() ? juce::Point<int> { 460, 270 }
                                                                           : juce::Point<int> { 500, 340 };
                overlayOptions.buttons.push_back ({ "Install", [confirmed] {
                    if (*confirmed != nullptr) {
                        (*confirmed)();
                    }
                }, true });
                overlayOptions.buttons.push_back ({ "Cancel", [cancelled] {
                    if (*cancelled != nullptr) {
                        (*cancelled)();
                    }
                }, false });
                overlayOptions.onDismissed = [cancelled] {
                    if (*cancelled != nullptr) {
                        (*cancelled)();
                    }
                };
                ErrorOverlay::show (*safeParent.getComponent(), std::move (overlayOptions));
                return;
            }

            juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::WarningIcon, "Install Update", message, "Install", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([confirmed, cancelled] (int result) {
                    auto& callback = result == 0 ? *cancelled : *confirmed;
                    if (callback != nullptr) {
                        callback();
                    }
                }));
        });
}

void InstallPrompt::showError (juce::Component* parent,
                               juce::String closeButtonSvg,
                               juce::StringRef title,
                               juce::StringRef message) {
    if (parent != nullptr) {
        ErrorOverlay::Options options;
        options.closeButtonSvg = std::move (closeButtonSvg);
        options.title = title;
        options.message = message;
        options.icon = ErrorOverlay::Icon::Error;
        options.preferredPanelSize = { 440, 320 };
        options.buttons.push_back ({ "OK", {}, true });
        ErrorOverlay::show (*parent, std::move (options));
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, title, juce::String (message));
}

} // namespace osci
