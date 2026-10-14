// Discord Rich Presence: activity building and the Presence manager.  See discord_presence.h.
#include "discord_presence.h"

#include <cstdio>

namespace vj {
namespace discord {

const char* const kDownloadUrl = "https://jaguar.provenance-emu.com/app.html";
const char* const kAssetJaguar = "jaguar";
const char* const kAssetJaguarCd = "jaguarcd";
const char* const kAssetLibrary = "library";
const char* const kAssetApp = "app";

namespace {

constexpr size_t kMaxText = 128;   // Discord's limit for details / state / hover text (bytes)

// Discord refuses a text field shorter than 2 characters and longer than 128 bytes.
std::string fit(const std::string& s) {
    std::string t = truncateUtf8(s, kMaxText);
    if (t.size() == 1) t += "\xC2\xA0";   // a no-break space
    return t;
}

std::string field(const char* key, const std::string& value) { return std::string("\"") + key + "\":" + jsonString(value); }

}  // namespace

std::string displayTitle(const std::string& in) {
    // Drops "(...)" and "[...]" groups; "(Disc N)" stays (a multi-disc set tells its discs apart by it).
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        const char c = in[i];
        if (c == '(' || c == '[') {
            const size_t e = in.find(c == '(' ? ')' : ']', i + 1);
            if (e != std::string::npos) {
                if (c == '(' && in.compare(i + 1, 5, "Disc ") == 0) out += in.substr(i, e - i + 1);
                i = e + 1;
                continue;
            }
        }
        out.push_back(c);
        i++;
    }
    auto tidy = [](const std::string& s) {   // collapse runs of spaces, trim
        std::string r;
        for (char c : s)
            if (!(c == ' ' && (r.empty() || r.back() == ' '))) r.push_back(c);
        while (!r.empty() && r.back() == ' ') r.pop_back();
        return r;
    };
    const std::string r = tidy(out);
    return r.empty() ? tidy(in) : r;   // nothing but tags: show the original rather than a blank line
}

std::string stateLine(const PresenceInput& in) {
    std::string s = in.disc && in.playing ? "Jaguar CD" : "Atari Jaguar";
    if (!in.playing) return s;
    if (in.paused) s += " | Paused";
    if (in.netplay == "hosting") s += " | Netplay: hosting";
    else if (in.netplay == "joined") s += " | Netplay: joined";
    else if (in.netplay == "waiting") s += " | Netplay: waiting for a partner";
    if (in.hardcore) s += " | Hardcore";
    if (in.achTotal > 0) s += " | " + std::to_string(in.achUnlocked) + "/" + std::to_string(in.achTotal) + " achievements";
    return s;
}

std::string buildActivity(const PresenceInput& in, bool withArtUrl) {
    std::string a = "{\"type\":0";
    a += "," + field("details", fit(in.playing && !in.title.empty() ? in.title : "Browsing the library"));
    a += "," + field("state", fit(stateLine(in)));
    if (in.startUnix > 0) a += ",\"timestamps\":{\"start\":" + std::to_string(in.startUnix) + "}";

    const bool art = withArtUrl && in.showArt && in.playing && !in.artUrl.empty();
    a += ",\"assets\":{";
    const char* generic = !in.playing ? kAssetLibrary : in.disc ? kAssetJaguarCd : kAssetJaguar;
    a += field("large_image", art ? in.artUrl : std::string(generic));
    a += "," + field("large_text", fit(in.playing && !in.title.empty() ? in.title : "Virtual Jaguar"));
    a += "," + field("small_image", kAssetApp);
    a += "," + field("small_text", fit("Virtual Jaguar" + (in.version.empty() ? std::string() : " " + in.version)));
    a += "}";

    if (in.showButtons) {
        a += ",\"buttons\":[{\"label\":" + jsonString("Get Virtual Jaguar") + ",\"url\":" + jsonString(kDownloadUrl) + "}";
        if (in.playing && in.raGameId > 0)
            a += ",{\"label\":" + jsonString("View on RetroAchievements") + ",\"url\":" +
                 jsonString("https://retroachievements.org/game/" + std::to_string(in.raGameId)) + "}";
        a += "]";
    }
    a += "}";
    return a;
}

std::string chooseArtUrl(lib::Section section, const std::string& datName, const std::string& dataRoot,
                         const std::string& upstreamBase, const std::string& mirrorBase,
                         const std::function<bool(const std::string&)>& exists) {
    if (datName.empty() || section == lib::Section::Bios) return "";
    const char* type = lib::kThumbTypes[0];   // Named_Boxarts
    for (const lib::ArtSource& src : lib::artSources(section, upstreamBase, mirrorBase)) {
        if (src.base.empty()) continue;
        if (!exists(lib::thumbnailCachePath(dataRoot, type, datName, src.system))) continue;
        const std::string url = lib::thumbnailUrl(type, datName, src.base, src.system);
        if (url.size() <= 512) return url;   // a long URL would be refused by Discord
    }
    return "";
}

// ---------------------------------------------------------------------------------------------
// Presence
// ---------------------------------------------------------------------------------------------
void Presence::configure(const PresenceSettings& s) {
    const bool wasAvail = available();
    const std::string oldId = s_.appId;
    s_ = s;
    const bool want = available() && s_.enabled;
    if (client_ && (!want || s_.appId != oldId)) {
        client_->stop();   // clears the presence first
        client_.reset();
        lastActivity_.clear();
    }
    if (want && !client_) {
        client_.reset(new Client());
        Client::Options o = haveOverride_ ? optionsOverride_ : Client::Options();
        o.appId = s_.appId;
        o.pid = s_.pid;
        client_->start(o);
        lastActivity_.clear();
    }
    (void)wasAvail;
}

void Presence::update(const PresenceInput& in) {
    if (!client_) return;
    const std::string act = buildActivity(in, true);
    if (act == lastActivity_) return;
    lastActivity_ = act;
    const bool hasUrl = in.showArt && in.playing && !in.artUrl.empty();
    client_->setActivity(act, hasUrl ? buildActivity(in, false) : std::string());
}

void Presence::shutdown() {
    if (client_) {
        client_->stop();
        client_.reset();
    }
    lastActivity_.clear();
}

std::string Presence::statusText() const {
    if (s_.appId.empty())
        return "Not available: this build has no Discord application ID (set discord.app_id in config.ini, or build with -DVJ_DISCORD_APP_ID=...).";
    if (s_.instance > 1) return "Only the first window of the app shows its status on Discord.";
    if (!s_.enabled) return "Off.";
    if (!client_) return "Starting...";
    switch (client_->status()) {
    case Status::Connected: return "Connected to Discord.";
    case Status::InvalidAppId: return "Discord rejected the application ID (check discord.app_id).";
    case Status::Searching: return "Discord is not running (looking again every 15 seconds).";
    case Status::Stopped: break;
    }
    return "Starting...";
}

}  // namespace discord
}  // namespace vj
