#pragma once

namespace osci {

// Publisher scoped, OS-user local. Never transmitted as an installation ID.
class LegalState {
public:
    LegalState() = default;
    explicit LegalState(SettingsStore store) : settings(std::move(store)) {}

    static juce::var documentsFor(juce::StringRef product, juce::StringRef version) {
        auto store = SettingsStore::forSharedLicensing();
        const auto cached = juce::JSON::parse(store.getString("legal.cache." + juce::String(product) + "." + juce::String(version)));
        return valid(cached) ? cached : juce::var();
    }

    static bool cacheDocuments(juce::StringRef product, juce::StringRef version, const juce::var& bundle) {
        if (!valid(bundle)) { return false; }
        auto store = SettingsStore::forSharedLicensing();
        store.set("legal.cache." + juce::String(product) + "." + juce::String(version), juce::JSON::toString(bundle));
        return store.save();
    }

    static bool valid(const juce::var& bundle) {
        if (bundle["scope"].toString() != "osci-products" || !validRevision(bundle["revision"])) {
            return false;
        }
        for (const auto* kind : {"privacy", "terms"}) {
            const auto doc = bundle["documents"][kind];
            const auto text = doc["text"].toString();
            if (!validRevision(doc["revision"]) || !doc["text"].isString() || text.isEmpty() || text.length() > 100000
                || juce::SHA256(text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString() != doc["sha256"].toString()) {
                return false;
            }
        }
        return true;
    }

    bool hasAcknowledged(const juce::var& bundle) {
        settings.reload();
        return valid(bundle) && settings.getBool(key(bundle, "privacy", "acknowledged"))
            && settings.getBool(key(bundle, "terms", "accepted"));
    }

    bool hasSeenKind(const char* kind) {
        settings.reload();
        return settings.getString("legal.osci-products." + juce::String(kind) + ".lastShown").isNotEmpty();
    }

    bool hasSeenOtherRevision(const juce::var& bundle, const char* kind) {
        settings.reload();
        const auto prefix = "legal.osci-products." + juce::String(kind);
        const auto first = settings.getString(prefix + ".firstShown", settings.getString(prefix + ".lastShown"));
        return first.isNotEmpty() && first != bundle["documents"][kind]["revision"].toString();
    }

    bool termsAccepted(const juce::var& bundle) {
        settings.reload();
        return settings.getBool(key(bundle, "terms", "accepted"));
    }

    bool privacyAcknowledged(const juce::var& bundle) {
        settings.reload();
        return settings.getBool(key(bundle, "privacy", "acknowledged"));
    }

    bool recordShown(const juce::var& bundle) {
        if (!valid(bundle)) { return false; }
        for (const auto* kind : {"privacy", "terms"}) {
            const auto firstKey = "legal.osci-products." + juce::String(kind) + ".firstShown";
            if (settings.getString(firstKey).isEmpty()) {
                settings.set(firstKey, bundle["documents"][kind]["revision"]);
            }
            settings.set(key(bundle, kind, "shown"), true);
            settings.set("legal.osci-products." + juce::String(kind) + ".lastShown", bundle["documents"][kind]["revision"]);
        }
        return settings.save();
    }

    bool acknowledge(const juce::var& bundle, bool acceptTerms, bool statisticsDisabled) {
        if (!valid(bundle) || (!acceptTerms && !termsAccepted(bundle))) { return false; }
        const bool alreadyAccepted = termsAccepted(bundle);
        settings.set(key(bundle, "privacy", "acknowledged"), true);
        settings.set(key(bundle, "terms", "accepted"), true);
        if (!alreadyAccepted) {
            settings.set(key(bundle, "terms", "acceptedAt"), juce::Time::getCurrentTime().toISO8601(true));
        }
        settings.set("legal.osci-products.privacy.lastAcknowledged", bundle["documents"]["privacy"]["revision"]);
        settings.set("legal.osci-products.terms.lastAccepted", bundle["documents"]["terms"]["revision"]);
        settings.set("legal.osci-products.statisticsDisabled", statisticsDisabled);
        return settings.save();
    }

    bool statisticsDisabled() {
        settings.reload();
        return settings.getBool("legal.osci-products.statisticsDisabled", false);
    }

    bool setStatisticsDisabled(bool disabled) {
        settings.set("legal.osci-products.statisticsDisabled", disabled);
        return settings.save();
    }

private:
    static bool validRevision(const juce::var& value) {
        const auto revision = value.toString();
        return value.isString() && revision.isNotEmpty() && revision.length() <= 80
            && revision.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
    }
    SettingsStore settings = SettingsStore::forSharedLicensing();
    static juce::String key(const juce::var& bundle, const char* kind, const char* state) {
        return "legal.osci-products." + juce::String(kind) + "." + bundle["documents"][kind]["revision"].toString()
            + "." + bundle["documents"][kind]["sha256"].toString() + "." + state;
    }
};

} // namespace osci
