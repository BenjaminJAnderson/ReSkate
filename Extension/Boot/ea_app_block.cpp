#include "ea_app_block.h"

#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Log/logging.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dingosdk {
namespace {

using CreateProcessWFn = BOOL (WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
    DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
using CreateProcessAFn = BOOL (WINAPI*)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
    DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
using ShellExecuteExWFn = BOOL (WINAPI*)(SHELLEXECUTEINFOW*);

std::atomic<CreateProcessWFn> original_create_w{};
std::atomic<CreateProcessAFn> original_create_a{};
std::atomic<ShellExecuteExWFn> original_shell_execute{};

// The Origin SDK starts the EA app from HKLM\...\Origin\ClientPath when its
// LSX connection fails. ReSkate never wants it: Steam (or offline mode) is the
// platform, and the offline runtime answers every EA service locally.
constexpr std::array<std::wstring_view, 8> blocked{
    L"eadesktop.exe", L"ealauncher.exe", L"ealaunchhelper.exe", L"eabackgroundservice.exe",
    L"origin.exe", L"link2ea:", L"origin:", L"origin2:"};

bool ea_app(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return std::any_of(blocked.begin(), blocked.end(), [&](auto name) { return text.find(name) != std::wstring::npos; });
}

std::wstring widen(const char* text) {
    if (!text) return {};
    const auto size = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), size);
    return result;
}

bool refuse(const std::wstring& target) {
    try {
        logging::write(logging::Level::info, logging::Channel::runtime,
            L"Blocked the game from starting the EA app: " + target);
    } catch (...) {}
    SetLastError(ERROR_ACCESS_DENIED);
    return false;
}

BOOL WINAPI create_process_w(LPCWSTR application, LPWSTR command, LPSECURITY_ATTRIBUTES process,
                             LPSECURITY_ATTRIBUTES thread, BOOL inherit, DWORD flags, LPVOID environment,
                             LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION information) {
    try {
        const auto target = std::wstring(application ? application : L"") + L" " + (command ? command : L"");
        if (ea_app(target)) return refuse(target);
    } catch (...) {}
    return original_create_w.load()(application, command, process, thread, inherit, flags, environment,
        directory, startup, information);
}

BOOL WINAPI create_process_a(LPCSTR application, LPSTR command, LPSECURITY_ATTRIBUTES process,
                             LPSECURITY_ATTRIBUTES thread, BOOL inherit, DWORD flags, LPVOID environment,
                             LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION information) {
    try {
        const auto target = widen(application) + L" " + widen(command);
        if (ea_app(target)) return refuse(target);
    } catch (...) {}
    return original_create_a.load()(application, command, process, thread, inherit, flags, environment,
        directory, startup, information);
}

BOOL WINAPI shell_execute(SHELLEXECUTEINFOW* info) {
    try {
        if (info && info->lpFile) {
            const std::wstring target = info->lpFile;
            if (ea_app(target)) {
                info->hInstApp = reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
                return refuse(target);
            }
        }
    } catch (...) {}
    return original_shell_execute.load()(info);
}

template<class Fn>
void hook(const wchar_t* module_name, const char* name, void* replacement, std::atomic<Fn>& original) {
    const auto module = LoadLibraryExW(module_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto target = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    if (!target) throw std::runtime_error(std::string("Missing ") + name);
    void* relay{};
    const auto create = hook_prepare(target, replacement, &relay);
    if (create != HookOk) throw std::runtime_error(std::string("Cannot hook ") + name + ": " + hook_status_string(create));
    original.store(reinterpret_cast<Fn>(relay));
    const auto enable = hook_enable(target);
    if (enable != HookOk) throw std::runtime_error(std::string("Cannot enable ") + name + ": " + hook_status_string(enable));
}

} // namespace

bool start_ea_app_block(std::string& error) noexcept {
    try {
#pragma warning(push)
#pragma warning(disable: 4191)
        hook(L"kernelbase.dll", "CreateProcessW", reinterpret_cast<void*>(&create_process_w), original_create_w);
        hook(L"kernelbase.dll", "CreateProcessA", reinterpret_cast<void*>(&create_process_a), original_create_a);
        hook(L"shell32.dll", "ShellExecuteExW", reinterpret_cast<void*>(&shell_execute), original_shell_execute);
#pragma warning(pop)
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace dingosdk
