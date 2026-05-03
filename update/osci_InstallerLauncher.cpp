namespace osci
{

bool InstallerLauncher::launchAndExitHost (const juce::File& installerOrPayload) {
    if (! installerOrPayload.existsAsFile()) {
        return false;
    }

    const auto launched = juce::Process::openDocument (installerOrPayload.getFullPathName(), {});
    if (launched && juce::JUCEApplicationBase::isStandaloneApp()) {
        juce::MessageManager::callAsync ([]
        {
            juce::JUCEApplicationBase::quit();
        });
    }

    return launched;
}

} // namespace osci
