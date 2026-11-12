#include "app.h"
#include "paths.h"
#include "platform/file_access.h"

#include "imgui.h"

#include <filesystem>

namespace vj {
namespace {

constexpr int kPickBase = 1;
constexpr int kPickCopyFolder = 2;
constexpr int kBoot = 3;

bool queueIpsAction(void* context, const char* path) {
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    event.user.code = 0x56495053;
    event.user.data1 = path ? SDL_strdup(path) : nullptr;
    event.user.data2 = context;
    if (SDL_PushEvent(&event)) return true;
    SDL_free(event.user.data1);
    return false;
}

void SDLCALL ipsPicked(void* context, const char* const* paths, int) {
    queueIpsAction(context, paths ? paths[0] : nullptr);
}

} // namespace

void App::openIpsPatch(const std::string& path, const std::string& base) {
    if (!window_ || !imguiUp_) {
        toast("Open IPS patches from the standalone window", 5.0, 3);
        return;
    }
    if (ipsDialogPending_) return;
    ipsPath_ = absolutePath(path);
    ipsCart_ = {};
    ipsError_.clear();
    ipsExpectedBase_[0] = ipsExpectedOutput_[0] = '\0';
    ipsConfirmReplace_ = false;
    ipsPopupRequested_ = true;
    const std::string focused = deskPad.active ? deskPad.nav.focusedKey() : std::string();
    if (!base.empty()) prepareIpsBase(base);
    else if (ips::cartridgePath(focused)) prepareIpsBase(focused);
    else if (host.isLoaded() && ips::cartridgePath(host.loadedPath())) prepareIpsBase(host.loadedPath());
}

void App::prepareIpsBase(const std::string& path) {
    ipsCart_ = {};
    ipsError_.clear();
    ipsConfirmReplace_ = false;
    ipsReplaceAvailable_ = ips::prepare(absolutePath(path), ipsPath_, ipsCart_, ipsError_)
                           && ipsCart_.output != ipsCart_.original && ips::canReplace(ipsCart_.path);
}

void App::ipsDialogResult(int action, const std::string& path) {
    ipsDialogPending_ = false;
    if (ipsPath_.empty()) return;
    if (action == kBoot) {
        auto cart = std::make_shared<ips::Cartridge>(std::move(ipsCart_));
        if (openContentImpl(cart->path, cart)) {
            ipsPath_.clear();
            ips::Bytes{}.swap(cart->original);
        } else {
            ipsCart_ = std::move(*cart);
            ipsError_ = "Could not boot the patched cartridge";
            ipsPopupRequested_ = true;
        }
        return;
    }
    if (path.empty()) return;
    if (action == kPickBase) {
        prepareIpsBase(path);
    } else if (action == kPickCopyFolder && !ipsCart_.output.empty()) {
        auto file = std::filesystem::u8path(path) / (ips::identity(ipsCart_.outputHash) + ".j64");
        ipsError_.clear();
        if (ips::saveCopy(ipsCart_, file.u8string(), ipsError_))
            toast("Saved patched copy to " + file.u8string(), 6.0);
    }
}

void App::drawIpsPatch() {
    if (ipsPopupRequested_) {
        ImGui::OpenPopup("Apply IPS patch");
        ipsPopupRequested_ = false;
    }
    if (!ImGui::BeginPopupModal("Apply IPS patch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 44);
    ImGui::TextWrapped("Patch: %s", ipsPath_.c_str());
    ImGui::BeginDisabled(ipsDialogPending_);
    if (ipsConfirmReplace_) {
        ImGui::TextWrapped("Replace %s?", ipsCart_.path.c_str());
        ImGui::TextWrapped("The original bytes will be saved to %s.", ips::recoveryPath(ipsCart_.path).c_str());
        if (ImGui::Button("Keep original")) ipsConfirmReplace_ = false;
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Confirm replacement")) {
            ipsConfirmReplace_ = false;
            ipsError_.clear();
            if (ips::replace(ipsCart_, ipsError_)) {
                toast("Replaced cartridge. Recovery copy: " + ips::recoveryPath(ipsCart_.path), 8.0);
                ImGui::CloseCurrentPopup();
                ipsPath_.clear();
                ipsCart_ = {};
            }
        }
    } else {
        const bool ready = !ipsCart_.output.empty();
        if (ready) {
            ImGui::TextWrapped("Base: %s (%zu bytes)", ipsCart_.path.c_str(), ipsCart_.original.size());
            ImGui::TextWrapped("Base file SHA-256: %s", ipsCart_.baseHash.c_str());
            ImGui::TextWrapped("Patched SHA-256: %s", ipsCart_.outputHash.c_str());
            if (ipsCart_.strippedHeader)
                ImGui::TextWrapped("IPS offsets start after the 512-byte copier header. The output omits that header.");
            ImGui::TextWrapped("IPS has no built-in base checksum. Enter expected SHA-256 values if available.");
            ImGui::InputText("Expected base SHA-256", ipsExpectedBase_, sizeof ipsExpectedBase_);
            ImGui::InputText("Expected patched SHA-256", ipsExpectedOutput_, sizeof ipsExpectedOutput_);
        }
        const bool verified = ready && ips::matchesHash(ipsExpectedBase_, ipsCart_.baseHash)
                                    && ips::matchesHash(ipsExpectedOutput_, ipsCart_.outputHash);
        if (ready && !verified) ImGui::TextWrapped("An expected SHA-256 does not match.");
        ImGui::BeginDisabled(!verified);
        if (ImGui::Button("Boot patched") && queueIpsAction(reinterpret_cast<void*>(kBoot), nullptr)) {
            ipsDialogPending_ = true;
            ImGui::CloseCurrentPopup();
        }
        if (verified) ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Save patched copy…")) {
            ipsDialogPending_ = true;
            files::openFolder(ipsPicked, reinterpret_cast<void*>(kPickCopyFolder), window_, nullptr, false);
        }
        ImGui::EndDisabled();
        if (verified && ipsReplaceAvailable_) {
            ImGui::SameLine();
            if (ImGui::Button("Replace original…")) ipsConfirmReplace_ = true;
        }
        if (ImGui::Button("Choose base cartridge…")) {
            static const SDL_DialogFileFilter filters[] = {{"Jaguar cartridges", "j64;jag;rom;bin"}};
            ipsDialogPending_ = true;
            files::openFile(ipsPicked, reinterpret_cast<void*>(kPickBase), window_, filters, 1, nullptr, false);
        }
        if (!verified) ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            ipsPath_.clear();
            ipsCart_ = {};
        }
    }
    if (!ipsError_.empty()) ImGui::TextWrapped("%s", ipsError_.c_str());
    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

} // namespace vj
