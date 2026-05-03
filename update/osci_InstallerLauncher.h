#pragma once

namespace osci
{

class InstallerLauncher final
{
public:
    static bool launchAndExitHost (const juce::File& installerOrPayload);

private:
    InstallerLauncher() = delete;
};

} // namespace osci
