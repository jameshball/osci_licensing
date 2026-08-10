namespace osci {

LinuxInstallSettings::LinuxInstallSettings (juce::String productSlug)
    : LinuxInstallSettings (std::move (productSlug), SettingsStore::forSharedLicensing()) {
}

LinuxInstallSettings::LinuxInstallSettings (juce::String productSlug, SettingsStore store)
    : product (std::move (productSlug)), settings (std::move (store)) {
}

juce::Result LinuxInstallSettings::load (LinuxInstallLocations& locations) const {
    std::optional<LinuxInstallLocations> saved;
    const auto result = loadSaved (saved);
    if (result.wasOk()) {
        locations = saved.value_or (defaults());
    }
    return result;
}

juce::Result LinuxInstallSettings::loadSaved (std::optional<LinuxInstallLocations>& locations) const {
    const auto standalone = settings.getString (standaloneKey (product));
    const auto vst3 = settings.getString (vst3Key (product));
    if (standalone.isEmpty() && vst3.isEmpty()) {
        locations = std::nullopt;
        return juce::Result::ok();
    }

    if (standalone.isEmpty() || vst3.isEmpty()) {
        return juce::Result::fail ("Saved Linux installation locations are incomplete");
    }

    locations = LinuxInstallLocations { juce::File (standalone), juce::File (vst3) };
    return juce::Result::ok();
}

juce::Result LinuxInstallSettings::save (const LinuxInstallLocations& locations) {
    settings.set (standaloneKey (product), locations.standaloneDirectory.getFullPathName());
    settings.set (vst3Key (product), locations.vst3Directory.getFullPathName());
    return settings.save() ? juce::Result::ok() : juce::Result::fail ("Could not save Linux installation locations");
}

juce::Result LinuxInstallSettings::clear() {
    settings.remove (standaloneKey (product));
    settings.remove (vst3Key (product));
    return settings.save() ? juce::Result::ok() : juce::Result::fail ("Could not clear Linux installation locations");
}

LinuxInstallLocations LinuxInstallSettings::defaults() {
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return { home.getChildFile (".local/bin"), home.getChildFile (".vst3") };
}

juce::String LinuxInstallSettings::standaloneKey (juce::StringRef productSlug) {
    return "install." + juce::String (productSlug) + ".standaloneDirectory";
}

juce::String LinuxInstallSettings::vst3Key (juce::StringRef productSlug) {
    return "install." + juce::String (productSlug) + ".vst3Directory";
}

} // namespace osci
