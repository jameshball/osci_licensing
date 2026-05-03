namespace osci {
namespace {
    struct KnownProcess {
        const char* normalisedName;
        const char* displayName;
        bool exact = false;
    };

    constexpr KnownProcess knownProcesses[] = {
        { "logicpro", "Logic Pro" },
        { "abletonlive", "Ableton Live" },
        { "reaper", "REAPER" },
        { "bitwigstudio", "Bitwig Studio" },
        { "cubase", "Cubase" },
        { "nuendo", "Nuendo" },
        { "studioone", "Studio One" },
        { "flstudio", "FL Studio" },
        { "fl64", "FL Studio", true },
        { "fl", "FL Studio", true },
        { "protools", "Pro Tools" },
        { "reason", "Reason" },
        { "waveform", "Waveform" },
        { "garageband", "GarageBand" },
        { "mainstage", "MainStage" },
        { "pluginval", "pluginval" },
        { "audiopluginhost", "JUCE AudioPluginHost" },
        { "juceaudiopluginhost", "JUCE AudioPluginHost" },
        { "audio_plugin_host", "JUCE AudioPluginHost" },
        { "auval", "AU validation tool", true },
        { "auvaltool", "AU validation tool", true },
        { "vst3plugintesthost", "VST3 plugin test host" },
    };

    juce::String normaliseProcessName (juce::String text) {
        text = text.trim().unquoted();

        if (text.containsChar ('/') || text.containsChar ('\\')) {
            text = juce::File::createFileWithoutCheckingPath (text).getFileName();
        }

        text = text.upToFirstOccurrenceOf (".exe", false, true);
        text = text.upToFirstOccurrenceOf (".app", false, true);

        juce::String result;
        const auto lower = text.toLowerCase();
        for (int index = 0; index < lower.length(); ++index) {
            const auto character = lower[index];
            if (juce::CharacterFunctions::isLetterOrDigit (character)) {
                result << character;
            }
        }

        return result;
    }

    juce::StringArray parseProcessList (const juce::String& output) {
        juce::StringArray lines;
        lines.addLines (output);

        juce::StringArray names;
        for (auto line : lines) {
            line = line.trim();
            if (line.isEmpty()) {
                continue;
            }

#if JUCE_WINDOWS
            if (line.startsWithChar ('"')) {
                line = line.fromFirstOccurrenceOf ("\"", false, false)
                           .upToFirstOccurrenceOf ("\"", false, false);
            } else if (line.containsChar (',')) {
                line = line.upToFirstOccurrenceOf (",", false, false);
            }
#endif

            names.addIfNotAlreadyThere (line);
        }

        return names;
    }

    juce::String processListCommandDescription() {
#if JUCE_WINDOWS
        return "tasklist";
#else
        return "ps";
#endif
    }

    juce::String readProcessList() {
        juce::ChildProcess process;
        juce::StringArray args;

#if JUCE_WINDOWS
        args.add ("cmd.exe");
        args.add ("/C");
        args.add ("tasklist /FO CSV /NH");
#else
        args.add ("/bin/ps");
        args.add ("-axo");
        args.add ("comm=");
#endif

        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            juce::Logger::writeToLog ("DAW detection: failed to start " + processListCommandDescription());
            return {};
        }

        const auto output = process.readAllProcessOutput();
        if (! process.waitForProcessToFinish (3000)) {
            process.kill();
        }

        return output;
    }
}

juce::Array<DetectedDawProcess> DawProcessDetector::scan() {
    juce::Array<DetectedDawProcess> detected;
    juce::StringArray seenDisplayNames;

    for (const auto& processName : parseProcessList (readProcessList())) {
        juce::String displayName;
        if (! isKnownDawProcessName (processName, &displayName)) {
            continue;
        }

        if (seenDisplayNames.contains (displayName)) {
            continue;
        }

        detected.add ({ processName, displayName });
        seenDisplayNames.add (displayName);
    }

    return detected;
}

void DawProcessDetector::scanAsync (ScanCallback callback) {
    if (callback == nullptr) {
        return;
    }

    juce::Thread::launch ([callback = std::move (callback)]() mutable {
        auto detected = scan();
        juce::MessageManager::callAsync ([callback = std::move (callback), detected = std::move (detected)]() mutable {
            callback (std::move (detected));
        });
    });
}

bool DawProcessDetector::isKnownDawProcessName (juce::StringRef processName, juce::String* matchedDisplayName) {
    const auto normalised = normaliseProcessName (juce::String (processName));
    if (normalised.isEmpty()) {
        return false;
    }

    for (const auto& known : knownProcesses) {
        const juce::String candidate (known.normalisedName);
        const auto matches = known.exact ? normalised == candidate
                                         : normalised.contains (candidate);
        if (! matches) {
            continue;
        }

        if (matchedDisplayName != nullptr) {
            *matchedDisplayName = known.displayName;
        }

        return true;
    }

    return false;
}

juce::String DawProcessDetector::joinDisplayNames (const juce::Array<DetectedDawProcess>& processes) {
    juce::StringArray names;
    for (const auto& process : processes) {
        names.addIfNotAlreadyThere (process.displayName);
    }

    return names.joinIntoString (", ");
}

} // namespace osci
