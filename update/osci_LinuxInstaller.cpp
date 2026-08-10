#include <cstdlib>
#include <set>

#if ! JUCE_WINDOWS
#include <sys/stat.h>
#endif

#if JUCE_LINUX
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

constexpr int maxArchiveEntries = 10000;
constexpr juce::int64 maxEntryBytes = 1024LL * 1024LL * 1024LL;
constexpr juce::int64 maxExpandedBytes = 2LL * 1024LL * 1024LL * 1024LL;

juce::String expectedVst3ArchitectureDirectory() {
#if JUCE_ARM && JUCE_64BIT
    return "aarch64-linux";
#elif JUCE_INTEL && JUCE_64BIT
    return "x86_64-linux";
#else
    return {};
#endif
}

juce::StringArray allVst3Bundles (const osci::LinuxInstallManifest& manifest) {
    auto bundles = manifest.vst3Bundles;
    bundles.addArray (manifest.optionalVst3Bundles);
    return bundles;
}

juce::Result validateManifest (const osci::LinuxInstallManifest& manifest) {
    if (manifest.productSlug.isEmpty() || manifest.displayName.isEmpty() || manifest.standaloneFile.isEmpty()
        || manifest.desktopCategories.isEmpty() || manifest.standaloneFile.containsAnyOf ("/\\")
        || manifest.displayName.containsAnyOf ("\r\n") || manifest.desktopCategories.containsAnyOf ("\r\n")) {
        return juce::Result::fail ("Linux installation metadata is incomplete");
    }

    for (const auto character : manifest.productSlug) {
        if (!juce::CharacterFunctions::isLetterOrDigit (character) && character != '-' && character != '_'
            && character != '.') {
            return juce::Result::fail ("Linux installation metadata contains an invalid product identifier");
        }
    }

    if (manifest.productSlug == "." || manifest.productSlug == "..") {
        return juce::Result::fail ("Linux installation metadata contains an invalid product identifier");
    }

    juce::StringArray seenBundles;
    for (const auto& bundle : allVst3Bundles (manifest)) {
        if (!bundle.endsWith (".vst3") || bundle.containsAnyOf ("/\\") || seenBundles.contains (bundle)) {
            return juce::Result::fail ("Linux installation metadata contains an invalid VST3 bundle name");
        }
        seenBundles.add (bundle);
    }

    return juce::Result::ok();
}

bool isAbsoluteArchivePath (const juce::String& path) {
    return path.startsWithChar ('/') || path.startsWithChar ('\\')
        || (path.length() > 1 && juce::CharacterFunctions::isLetter (path[0]) && path[1] == ':');
}

bool isAllowedEntry (const juce::String& path, const osci::LinuxInstallManifest& manifest) {
    if (path == manifest.standaloneFile) {
        return true;
    }

    for (const auto& bundle : allVst3Bundles (manifest)) {
        if (path == bundle || path.startsWith (bundle + "/")) {
            return true;
        }
    }

    return false;
}

juce::Result validateArchive (juce::ZipFile& archive, const osci::LinuxInstallManifest& manifest) {
    const auto architectureDirectory = expectedVst3ArchitectureDirectory();
    if (architectureDirectory.isEmpty()) {
        return juce::Result::fail ("This CPU architecture is not supported by the Linux installer");
    }

    const auto count = archive.getNumEntries();
    if (count <= 0 || count > maxArchiveEntries) {
        return juce::Result::fail ("The Linux update archive has an invalid number of entries");
    }

    std::set<juce::String> seen;
    juce::int64 expandedBytes = 0;
    bool foundStandalone = false;
    juce::StringArray foundBundles;
    juce::StringArray foundPluginBinaries;

    for (int index = 0; index < count; ++index) {
        const auto* entry = archive.getEntry (index);
        if (entry == nullptr) {
            return juce::Result::fail ("The Linux update archive contains an unreadable entry");
        }

        const auto directoryEntry = entry->filename.endsWithChar ('/') || entry->filename.endsWithChar ('\\');
        auto path = entry->filename.replaceCharacter ('\\', '/');
        while (path.endsWithChar ('/')) {
            path = path.dropLastCharacters (1);
        }

        if (path.isEmpty()) {
            continue;
        }

        const auto parts = juce::StringArray::fromTokens (path, "/", "");
        const auto unixMode = entry->externalFileAttributes >> 16;
        const auto unixFileType = unixMode & 0170000u;
        const auto unsupportedFileType = unixFileType != 0 && unixFileType != 0040000u && unixFileType != 0100000u;
        if (isAbsoluteArchivePath (path) || path.contains ("//") || parts.contains ("..") || parts.contains (".")
            || entry->isSymbolicLink || unsupportedFileType) {
            return juce::Result::fail ("The Linux update archive contains an unsafe path: " + entry->filename);
        }

        if (!seen.insert (path).second) {
            return juce::Result::fail ("The Linux update archive contains a duplicate entry: " + path);
        }

        if (!isAllowedEntry (path, manifest)) {
            return juce::Result::fail ("The Linux update archive contains an unexpected entry: " + path);
        }

        if (entry->uncompressedSize < 0 || entry->uncompressedSize > maxEntryBytes
            || expandedBytes > maxExpandedBytes - entry->uncompressedSize) {
            return juce::Result::fail ("The Linux update archive is too large when expanded");
        }

        expandedBytes += entry->uncompressedSize;
        foundStandalone = foundStandalone || (path == manifest.standaloneFile && !directoryEntry
                                                && unixFileType != 0040000u && entry->uncompressedSize > 0);
        for (const auto& bundle : allVst3Bundles (manifest)) {
            if (path == bundle || path.startsWith (bundle + "/")) {
                foundBundles.addIfNotAlreadyThere (bundle);
            }
            const auto binaryName = bundle.upToLastOccurrenceOf (".vst3", false, false) + ".so";
            const auto relativePath = path.fromFirstOccurrenceOf (bundle + "/", false, false);
            const auto binaryParts = juce::StringArray::fromTokens (relativePath, "/", "");
            if (!directoryEntry && entry->uncompressedSize > 0 && binaryParts.size() == 3
                && binaryParts[0] == "Contents" && binaryParts[1] == architectureDirectory
                && binaryParts[2] == binaryName) {
                foundPluginBinaries.addIfNotAlreadyThere (bundle);
            }
        }
    }

    if (!foundStandalone) {
        return juce::Result::fail ("The Linux update archive is missing the application binary " + manifest.standaloneFile);
    }

    for (const auto& bundle : allVst3Bundles (manifest)) {
        const auto required = manifest.vst3Bundles.contains (bundle) || foundBundles.contains (bundle);
        if (required && !foundPluginBinaries.contains (bundle)) {
            return juce::Result::fail ("The Linux update archive is missing the plugin binary for " + bundle);
        }
    }

    return juce::Result::ok();
}

juce::Result validateExtractedFiles (const juce::File& directory, const osci::LinuxInstallManifest& manifest) {
    const auto architectureDirectory = expectedVst3ArchitectureDirectory();
    const auto standalone = directory.getChildFile (manifest.standaloneFile);
    if (!standalone.existsAsFile() || standalone.getSize() <= 0) {
        return juce::Result::fail ("The extracted application binary is invalid");
    }

    for (const auto& bundle : allVst3Bundles (manifest)) {
        const auto bundleDirectory = directory.getChildFile (bundle);
        if (!bundleDirectory.exists() && manifest.optionalVst3Bundles.contains (bundle)) {
            continue;
        }
        const auto binaryName = bundle.upToLastOccurrenceOf (".vst3", false, false) + ".so";
        const auto binary = bundleDirectory.getChildFile ("Contents").getChildFile (architectureDirectory).getChildFile (binaryName);
        if (!bundleDirectory.isDirectory() || !binary.existsAsFile() || binary.getSize() <= 0) {
            return juce::Result::fail ("The extracted plugin binary is invalid for " + bundle);
        }
    }

    return juce::Result::ok();
}

juce::Result ensureWritableDirectory (const juce::File& directory, osci::LinuxInstaller::MissingDirectoryPolicy policy) {
    if (!juce::File::isAbsolutePath (directory.getFullPathName())) {
        return juce::Result::fail ("Installation path must be absolute: " + directory.getFullPathName());
    }

    if (!directory.isDirectory()) {
        if (directory.exists()) {
            return juce::Result::fail ("Installation path is not a directory: " + directory.getFullPathName());
        }

        if (policy == osci::LinuxInstaller::MissingDirectoryPolicy::Create) {
            if (!directory.createDirectory()) {
                return juce::Result::fail ("Could not create installation directory: " + directory.getFullPathName());
            }
        } else if (policy == osci::LinuxInstaller::MissingDirectoryPolicy::Reject) {
            return juce::Result::fail ("Saved installation directory is unavailable: " + directory.getFullPathName());
        } else {
            auto existingParent = directory.getParentDirectory();
            while (existingParent != existingParent.getParentDirectory() && !existingParent.exists()) {
                existingParent = existingParent.getParentDirectory();
            }
            if (!existingParent.isDirectory() || !existingParent.hasWriteAccess()) {
                return juce::Result::fail ("Installation directory cannot be created: " + directory.getFullPathName());
            }
            return juce::Result::ok();
        }
    }

    if (policy != osci::LinuxInstaller::MissingDirectoryPolicy::Create) {
        return directory.hasWriteAccess()
            ? juce::Result::ok()
            : juce::Result::fail ("Installation directory is not writable: " + directory.getFullPathName());
    }

    const auto probe = directory.getNonexistentChildFile (".osci-write-test", {}, false);
    if (!probe.create() || !probe.deleteFile()) {
        return juce::Result::fail ("Installation directory is not writable: " + directory.getFullPathName());
    }

    return juce::Result::ok();
}

struct Replacement {
    juce::File target;
    juce::File staged;
    juce::File backup;
    bool hadTarget = false;
    bool installed = false;
};

bool exchangePaths (const juce::File& first, const juce::File& second) {
#if JUCE_LINUX && defined (SYS_renameat2)
    return ::syscall (SYS_renameat2,
                      AT_FDCWD, first.getFullPathName().toRawUTF8(),
                      AT_FDCWD, second.getFullPathName().toRawUTF8(),
                      RENAME_EXCHANGE) == 0;
#else
    juce::ignoreUnused (first, second);
    return false;
#endif
}

juce::StringArray rollbackReplacements (std::vector<Replacement>& replacements) {
    juce::StringArray errors;
    for (auto iterator = replacements.rbegin(); iterator != replacements.rend(); ++iterator) {
        auto& replacement = *iterator;
        if (replacement.installed && replacement.hadTarget && replacement.target.exists()
            && replacement.backup.exists() && exchangePaths (replacement.target, replacement.backup)) {
            replacement.installed = false;
            replacement.hadTarget = false;
            if (!replacement.backup.deleteRecursively()) {
                errors.add ("Could not remove the rolled-back replacement at " + replacement.backup.getFullPathName());
            }
        }
        if (replacement.installed && replacement.target.exists() && !replacement.target.deleteRecursively()) {
            errors.add ("Could not remove the incomplete replacement at " + replacement.target.getFullPathName());
        }
        if (replacement.hadTarget && replacement.backup.exists() && !replacement.backup.moveFileTo (replacement.target)) {
            errors.add ("Could not restore the previous installation from " + replacement.backup.getFullPathName());
        }
        if (replacement.staged.exists() && !replacement.staged.deleteRecursively()) {
            errors.add ("Could not remove staged files at " + replacement.staged.getFullPathName());
        }
    }
    return errors;
}

juce::Result failAfterRollback (const juce::Result& failure, std::vector<Replacement>& replacements) {
    const auto rollbackErrors = rollbackReplacements (replacements);
    return rollbackErrors.isEmpty()
        ? failure
        : juce::Result::fail (failure.getErrorMessage() + ". Rollback was incomplete: "
                              + rollbackErrors.joinIntoString (" "));
}

juce::Result prepareReplacement (const juce::File& source, const juce::File& target, Replacement& replacement) {
    const auto suffix = juce::Uuid().toString();
    replacement.target = target;
    replacement.staged = target.getSiblingFile ("." + target.getFileName() + ".osci-stage-" + suffix);
    replacement.backup = target.getSiblingFile ("." + target.getFileName() + ".osci-backup-" + suffix);
    replacement.hadTarget = target.exists();

    const auto copied = source.isDirectory() ? source.copyDirectoryTo (replacement.staged)
                                              : source.copyFileTo (replacement.staged);
    if (!copied) {
        return juce::Result::fail ("Could not stage " + target.getFileName() + " in " + target.getParentDirectory().getFullPathName());
    }

    return juce::Result::ok();
}

juce::Result commitReplacement (Replacement& replacement) {
    if (replacement.hadTarget) {
        if (exchangePaths (replacement.staged, replacement.target)) {
            replacement.backup = replacement.staged;
            replacement.staged = juce::File();
            replacement.installed = true;
            return juce::Result::ok();
        }

#if JUCE_LINUX
        return juce::Result::fail ("The destination filesystem does not support atomic replacement of "
                                   + replacement.target.getFileName());
#endif
    }

    if (replacement.hadTarget && !replacement.target.moveFileTo (replacement.backup)) {
        return juce::Result::fail ("Could not preserve the existing " + replacement.target.getFileName());
    }

    if (!replacement.staged.moveFileTo (replacement.target)) {
        if (replacement.hadTarget && !replacement.backup.moveFileTo (replacement.target)) {
            return juce::Result::fail ("Could not install " + replacement.target.getFileName()
                                       + " or restore its backup at " + replacement.backup.getFullPathName());
        }
        return juce::Result::fail ("Could not install " + replacement.target.getFileName());
    }

    replacement.installed = true;
    return juce::Result::ok();
}

juce::String desktopExecValue (const juce::File& executable) {
    auto path = executable.getFullPathName();
    path = path.replace ("\\", "\\\\\\\\")
               .replace ("\"", "\\\\\\\"")
               .replace ("`", "\\\\`")
               .replace ("$", "\\\\$")
               .replace ("%", "%%");
    return "\"" + path + "\"";
}

bool writeLauncherIcon (const juce::MemoryBlock& sourceData, const juce::File& target) {
    const auto source = juce::ImageFileFormat::loadFrom (sourceData.getData(), sourceData.getSize());
    if (!source.isValid()) {
        return false;
    }

    const auto icon = source.rescaled (256, 256, juce::Graphics::ResamplingQuality::highResamplingQuality);
    juce::MemoryOutputStream output;
    return juce::PNGImageFormat().writeImageToStream (icon, output)
        && target.replaceWithData (output.getData(), output.getDataSize());
}

void refreshDesktopCaches (const juce::File& applications, const juce::File& icons) {
    juce::ChildProcess desktopProcess;
    juce::StringArray desktopCommand { "update-desktop-database", applications.getFullPathName() };
    desktopProcess.start (desktopCommand);
    desktopProcess.waitForProcessToFinish (10000);

    juce::ChildProcess iconProcess;
    juce::StringArray iconCommand { "gtk-update-icon-cache", "-f", "-t", icons.getFullPathName() };
    iconProcess.start (iconCommand);
    iconProcess.waitForProcessToFinish (10000);
}

juce::File resolveExistingPath (const juce::File& file) {
#if JUCE_WINDOWS
    return file;
#else
    auto existingAncestor = file;
    juce::StringArray missingParts;
    while (!existingAncestor.exists() && existingAncestor != existingAncestor.getParentDirectory()) {
        missingParts.insert (0, existingAncestor.getFileName());
        existingAncestor = existingAncestor.getParentDirectory();
    }

    auto* resolvedPath = ::realpath (existingAncestor.getFullPathName().toRawUTF8(), nullptr);
    if (resolvedPath == nullptr) {
        return file;
    }

    auto resolved = juce::File (juce::String::fromUTF8 (resolvedPath));
    std::free (resolvedPath);
    for (const auto& part : missingParts) {
        resolved = resolved.getChildFile (part);
    }
    return resolved;
#endif
}

bool directoriesAreEquivalent (const juce::File& first, const juce::File& second) {
    if (first == second || resolveExistingPath (first) == resolveExistingPath (second)) {
        return true;
    }

#if JUCE_WINDOWS
    return false;
#else
    struct stat firstInfo {};
    struct stat secondInfo {};
    return ::stat (first.getFullPathName().toRawUTF8(), &firstInfo) == 0
        && ::stat (second.getFullPathName().toRawUTF8(), &secondInfo) == 0
        && firstInfo.st_dev == secondInfo.st_dev && firstInfo.st_ino == secondInfo.st_ino;
#endif
}

bool containsPath (const juce::File& parent, const juce::File& child) {
    const auto resolvedParent = resolveExistingPath (parent).getFullPathName();
    const auto resolvedChild = resolveExistingPath (child).getFullPathName();
    return resolvedChild == resolvedParent
        || resolvedChild.startsWith (juce::File::addTrailingSeparator (resolvedParent));
}

juce::Result validateArtifactPaths (const osci::LinuxInstallManifest& manifest,
                                    const osci::LinuxInstallLocations& current,
                                    const std::optional<osci::LinuxInstallLocations>& previous) {
    juce::Array<juce::File> currentArtifacts {
        current.standaloneDirectory.getChildFile (manifest.standaloneFile)
    };
    for (const auto& bundle : allVst3Bundles (manifest)) {
        currentArtifacts.add (current.vst3Directory.getChildFile (bundle));
    }

    for (int first = 0; first < currentArtifacts.size(); ++first) {
        for (int second = first + 1; second < currentArtifacts.size(); ++second) {
            if (containsPath (currentArtifacts[first], currentArtifacts[second])
                || containsPath (currentArtifacts[second], currentArtifacts[first])) {
                return juce::Result::fail ("Installation paths overlap product files");
            }
        }
    }

    if (previous.has_value()) {
        juce::Array<juce::File> previousArtifacts {
            previous->standaloneDirectory.getChildFile (manifest.standaloneFile)
        };
        for (const auto& bundle : allVst3Bundles (manifest)) {
            previousArtifacts.add (previous->vst3Directory.getChildFile (bundle));
        }

        for (const auto& oldArtifact : previousArtifacts) {
            for (const auto& newArtifact : currentArtifacts) {
                if (resolveExistingPath (oldArtifact) != resolveExistingPath (newArtifact)
                    && (containsPath (oldArtifact, newArtifact) || containsPath (newArtifact, oldArtifact))) {
                    return juce::Result::fail ("Installation paths overlap the previous installation");
                }
            }
        }
    }

    return juce::Result::ok();
}

void removeOldProductFiles (const osci::LinuxInstallManifest& manifest,
                            const osci::LinuxInstallLocations& previous,
                            const osci::LinuxInstallLocations& current,
                            bool removeStandalone,
                            juce::StringArray& warnings) {
    if (removeStandalone && !directoriesAreEquivalent (previous.standaloneDirectory, current.standaloneDirectory)) {
        const auto oldStandalone = previous.standaloneDirectory.getChildFile (manifest.standaloneFile);
        if (oldStandalone.exists() && !oldStandalone.deleteRecursively()) {
            warnings.add ("Could not remove the previous application at " + oldStandalone.getFullPathName());
        }
    }

    if (!directoriesAreEquivalent (previous.vst3Directory, current.vst3Directory)) {
        for (const auto& bundle : allVst3Bundles (manifest)) {
            const auto oldBundle = previous.vst3Directory.getChildFile (bundle);
            if (oldBundle.exists() && !oldBundle.deleteRecursively()) {
                warnings.add ("Could not remove the previous plugin at " + oldBundle.getFullPathName());
            }
        }
    }
}

} // namespace

namespace osci {

LinuxInstaller::LinuxInstaller() : LinuxInstaller (Config {}) {
}

LinuxInstaller::LinuxInstaller (Config configToUse) : config (std::move (configToUse)) {
    if (config.dataHome == juce::File()) {
        const auto xdgDataHome = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
        config.dataHome = xdgDataHome.isNotEmpty() && juce::File::isAbsolutePath (xdgDataHome)
            ? juce::File (xdgDataHome)
            : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    }
}

juce::Result LinuxInstaller::install (const Request& request, Report& report) const {
    report = {};
    auto result = validateManifest (request.manifest);
    if (result.failed()) {
        return result;
    }

    juce::InterProcessLock installLock ("osci-linux-install-" + request.manifest.productSlug);
    if (!installLock.enter (0)) {
        return juce::Result::fail ("Another installation of " + request.manifest.displayName + " is already in progress");
    }

    if (!request.archive.existsAsFile()) {
        return juce::Result::fail ("The downloaded Linux update archive could not be found");
    }

    LinuxInstallSettings settings = config.settingsOptions.has_value()
        ? LinuxInstallSettings (request.manifest.productSlug, SettingsStore (*config.settingsOptions))
        : LinuxInstallSettings (request.manifest.productSlug);
    std::optional<LinuxInstallLocations> previousLocations;
    result = settings.loadSaved (previousLocations);
    if (result.failed()) {
        return result;
    }

    result = validateArtifactPaths (request.manifest, request.locations, previousLocations);
    if (result.failed()) {
        return result;
    }

    auto locationResult = validateLocations (request.locations, request.missingDirectoryPolicy);
    if (locationResult.failed()) {
        return locationResult;
    }

    if (request.progress != nullptr) {
        request.progress (0.05, "Validating package...");
    }

    juce::ZipFile archive (request.archive);
    auto archiveResult = validateArchive (archive, request.manifest);
    if (archiveResult.failed()) {
        return archiveResult;
    }

    const auto extractionDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("osci-linux-install", {}, false);
    if (!extractionDirectory.createDirectory()) {
        return juce::Result::fail ("Could not create a temporary installation directory");
    }

    struct ExtractionCleanup {
        juce::File directory;
        ~ExtractionCleanup() { directory.deleteRecursively(); }
    } cleanup { extractionDirectory };

    if (request.progress != nullptr) {
        request.progress (0.15, "Extracting package...");
    }

    auto extractionResult = archive.uncompressTo (extractionDirectory, false);
    if (extractionResult.failed()) {
        return juce::Result::fail ("Could not extract the Linux update: " + extractionResult.getErrorMessage());
    }

    result = validateExtractedFiles (extractionDirectory, request.manifest);
    if (result.failed()) {
        return result;
    }

    std::vector<Replacement> replacements;
    replacements.reserve (static_cast<size_t> (allVst3Bundles (request.manifest).size() + 1));
    replacements.emplace_back();
    result = prepareReplacement (extractionDirectory.getChildFile (request.manifest.standaloneFile),
                                 request.locations.standaloneDirectory.getChildFile (request.manifest.standaloneFile),
                                 replacements.back());

    for (const auto& bundle : allVst3Bundles (request.manifest)) {
        if (result.failed()) {
            break;
        }
        const auto source = extractionDirectory.getChildFile (bundle);
        if (!source.exists()) {
            continue;
        }
        replacements.emplace_back();
        result = prepareReplacement (source, request.locations.vst3Directory.getChildFile (bundle), replacements.back());
    }

    if (result.failed()) {
        return failAfterRollback (result, replacements);
    }

    if (request.progress != nullptr) {
        request.progress (0.45, "Installing application and plugins...");
    }

    for (auto& replacement : replacements) {
        result = commitReplacement (replacement);
        if (result.failed()) {
            return failAfterRollback (result, replacements);
        }
    }

    const auto standalonePath = request.locations.standaloneDirectory.getChildFile (request.manifest.standaloneFile);
    if (!standalonePath.setExecutePermission (true)) {
        return failAfterRollback (juce::Result::fail ("Could not make the installed application executable"), replacements);
    }

    for (const auto& bundle : allVst3Bundles (request.manifest)) {
        if (!request.locations.vst3Directory.getChildFile (bundle).isDirectory()) {
            continue;
        }
        juce::Array<juce::File> binaries;
        request.locations.vst3Directory.getChildFile (bundle).findChildFiles (binaries, juce::File::findFiles, true, "*.so");
        for (const auto& binary : binaries) {
            if (!binary.setExecutePermission (true)) {
                return failAfterRollback (juce::Result::fail ("Could not make the installed plugin executable: "
                                                               + binary.getFullPathName()), replacements);
            }
        }
    }

    const auto settingsResult = settings.save (request.locations);
    if (settingsResult.failed()) {
        const auto failure = failAfterRollback (settingsResult, replacements);
        if (previousLocations.has_value()) {
            settings.save (*previousLocations);
        } else {
            settings.clear();
        }
        return failure;
    }

    for (auto& replacement : replacements) {
        if (replacement.backup.exists() && !replacement.backup.deleteRecursively()) {
            report.warnings.add ("Could not remove installation backup at " + replacement.backup.getFullPathName());
        }
    }

    for (const auto& bundle : request.manifest.optionalVst3Bundles) {
        if (!extractionDirectory.getChildFile (bundle).exists()) {
            const auto staleBundle = request.locations.vst3Directory.getChildFile (bundle);
            if (staleBundle.exists() && !staleBundle.deleteRecursively()) {
                report.warnings.add ("Could not remove the obsolete plugin at " + staleBundle.getFullPathName());
            }
        }
    }

    report.standalonePath = standalonePath;
    for (const auto& replacement : replacements) {
        if (replacement.target.hasFileExtension ("vst3")) {
            report.vst3Paths.add (replacement.target);
        }
    }
    const auto applicationsDirectory = config.dataHome.getChildFile ("applications");
    const auto iconRoot = config.dataHome.getChildFile ("icons/hicolor");
    const auto iconDirectory = iconRoot.getChildFile ("256x256/apps");
    if (!applicationsDirectory.createDirectory() || !iconDirectory.createDirectory()) {
        report.warnings.add ("The application was installed, but its desktop launcher directories could not be created");
    } else {
        const auto icon = iconDirectory.getChildFile (request.manifest.productSlug + ".png");
        if (request.iconPng.isEmpty() || !writeLauncherIcon (request.iconPng, icon)) {
            report.warnings.add ("The application was installed, but its launcher icon could not be written");
        } else {
            report.iconPath = icon;
        }

        juce::String desktopEntry;
        desktopEntry << "[Desktop Entry]\n"
                     << "Type=Application\n"
                     << "Name=" << request.manifest.displayName << "\n"
                     << "Exec=" << desktopExecValue (standalonePath) << "\n"
                     << "Icon=" << request.manifest.productSlug << "\n"
                     << "Terminal=false\n"
                     << "Categories=" << request.manifest.desktopCategories << "\n";
        const auto desktopFile = applicationsDirectory.getChildFile (request.manifest.productSlug + ".desktop");
        if (!desktopFile.replaceWithText (desktopEntry)) {
            report.warnings.add ("The application was installed, but its desktop launcher could not be written");
        } else {
            report.desktopEntryPath = desktopFile;
        }

        if (config.refreshDesktopCaches) {
            refreshDesktopCaches (applicationsDirectory, iconRoot);
        }
    }

    if (previousLocations.has_value()) {
        removeOldProductFiles (request.manifest, *previousLocations, request.locations,
                               report.desktopEntryPath.existsAsFile(), report.warnings);
    }

    if (request.progress != nullptr) {
        request.progress (1.0, "Installation complete");
    }

    return juce::Result::ok();
}

juce::Result LinuxInstaller::validateLocations (const LinuxInstallLocations& locations, MissingDirectoryPolicy policy) {
    auto result = ensureWritableDirectory (locations.standaloneDirectory, policy);
    return result.failed() ? result : ensureWritableDirectory (locations.vst3Directory, policy);
}

} // namespace osci
