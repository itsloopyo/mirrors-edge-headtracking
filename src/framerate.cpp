#include "framerate.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "cameraunlock/logging/file_log.h"

namespace meht::framerate {
namespace {

namespace log = cameraunlock::logging;

constexpr char kSection[] = "[Engine.GameEngine]";
constexpr char kKey[] = "bSmoothFrameRate";
constexpr char kLine[] = "bSmoothFrameRate=False";

std::wstring TdEngineIniPath() {
    wchar_t docs[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) return {};
    return std::wstring(docs) + L"\\EA Games\\Mirror's Edge\\TdGame\\Config\\TdEngine.ini";
}

bool IsSectionHeader(const std::string& line) {
    size_t i = line.find_first_not_of(" \t");
    return i != std::string::npos && line[i] == '[';
}

bool IsAssignment(const std::string& line, const char* key) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos) return false;
    size_t k = 0;
    while (key[k] && i < line.size() &&
           std::tolower(static_cast<unsigned char>(line[i])) ==
               std::tolower(static_cast<unsigned char>(key[k]))) {
        ++i;
        ++k;
    }
    if (key[k] != '\0') return false;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i < line.size() && line[i] == '=';
}

// Sets bSmoothFrameRate=False inside [Engine.GameEngine], preserving every other
// byte (CRLF, comments, ordering). Replaces the existing key if present, else
// inserts it at the end of the section. Does nothing if the section is absent
// (the game always writes it, so absence means an unexpected/foreign config).
// Returns true if the file was changed.
bool DisableSmoothFrameRate() {
    const std::wstring path = TdEngineIniPath();
    if (path.empty()) {
        log::Line("[framerate] could not resolve Documents path; frame-rate cap left intact");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log::Line("[framerate] TdEngine.ini not found; run the game once so it is created");
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    in.close();
    const std::string content = buf.str();
    const char* eolStyle = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    std::string out;
    out.reserve(content.size() + 32);
    bool inTarget = false, sawSection = false, sawKey = false, changed = false;

    size_t pos = 0;
    while (pos <= content.size()) {
        const size_t nl = content.find('\n', pos);
        const bool last = (nl == std::string::npos);
        std::string body = content.substr(pos, last ? std::string::npos : nl - pos + 1);

        std::string eol;
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
            eol.insert(eol.begin(), body.back());
            body.pop_back();
        }

        if (IsSectionHeader(body)) {
            // Leaving the target section without the key seen: append it now.
            if (inTarget && !sawKey) {
                out += kLine;
                out += eolStyle;
                changed = true;
                sawKey = true;
            }
            inTarget = (body.find(kSection) != std::string::npos);
            if (inTarget) sawSection = true;
        } else if (inTarget && IsAssignment(body, kKey)) {
            if (body != kLine) changed = true;
            body = kLine;
            sawKey = true;
        }

        out += body;
        out += eol;
        if (last) break;
        pos = nl + 1;
    }

    // File ended while still inside the target section, key never seen.
    if (inTarget && !sawKey) {
        out += kLine;
        out += eolStyle;
        changed = true;
    }

    if (!sawSection) {
        log::Line("[framerate] [Engine.GameEngine] not present in TdEngine.ini; cap left intact");
        return false;
    }
    if (!changed) {
        log::Line("[framerate] frame-rate cap already disabled");
        return false;
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        log::Line("[framerate] failed to open TdEngine.ini for writing");
        return false;
    }
    ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
    log::Line("[framerate] frame-rate cap removed (bSmoothFrameRate=False); "
              "effective this launch, next launch at worst");
    return true;
}

}  // namespace

void Init(const Config& cfg) {
    if (!cfg.UnlockFrameRate) {
        log::Line("[framerate] unlock disabled; leaving the game's frame-rate cap intact");
        return;
    }
    DisableSmoothFrameRate();
}

}  // namespace meht::framerate
