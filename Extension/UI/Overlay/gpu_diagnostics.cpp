#include "Engine/Core/Log/logging.h"
#include "gpu_diagnostics.h"

#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;
struct Log {
    std::atomic<bool> removal_recorded{};
};
Log& log() { static auto* value = new Log; return *value; }
}

bool graphics_option_enabled(const wchar_t* name) noexcept {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(name, value, 2) == 1 && value[0] == L'1';
}


void initialize_graphics_diagnostics() noexcept {
    const auto error = GetLastError();
    try {
        const bool diagnostic = graphics_option_enabled(L"RESKATE_GPU_DIAGNOSTICS");
        dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "Graphics startup: gpu_diagnostics=%d", diagnostic);
        if (diagnostic) {
            // DRED must be configured before the game creates its D3D12 device.
            // Do not enable the debug layer or change the game's device flags.
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
            const auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&settings));
            dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "DRED settings result=0x%08lx", static_cast<unsigned long>(result));
            if (SUCCEEDED(result)) {
                settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }
    } catch (...) {}
    SetLastError(error);
}

void record_device_failure(ID3D12Device* device, HRESULT result,
    const char* operation) noexcept {
    const auto error = GetLastError();
    try {
        if (!device || log().removal_recorded.exchange(true)) {
            SetLastError(error);
            return;
        }
        const auto reason = device->GetDeviceRemovedReason();
        dingosdk::logging::printf(dingosdk::logging::Level::error, dingosdk::logging::Channel::graphics, "GPU failure: operation=%s result=0x%08lx removed_reason=0x%08lx",
            operation, static_cast<unsigned long>(result), static_cast<unsigned long>(reason));
        ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        const auto query = device->QueryInterface(IID_PPV_ARGS(&dred));
        dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "DRED query result=0x%08lx", static_cast<unsigned long>(query));
        if (SUCCEEDED(query)) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
            const auto status = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
            dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "DRED breadcrumbs result=0x%08lx", static_cast<unsigned long>(status));
            if (SUCCEEDED(status)) {
                auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                for (unsigned i = 0; node && i < 32; ++i, node = node->pNext) {
                    const UINT done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                    dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "Breadcrumb[%u] queue=%p list=%p completed=%u count=%u",
                        i, node->pCommandQueue, node->pCommandList, done, node->BreadcrumbCount);
                    if (node->pCommandHistory && done <= node->BreadcrumbCount) {
                        const UINT begin = done > 4 ? done - 4 : 0;
                        const UINT end = node->BreadcrumbCount - done > 4 ? done + 4 : node->BreadcrumbCount;
                        for (UINT op = begin; op < end; ++op)
                            dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "  op[%u]=%u", op,
                                static_cast<unsigned>(node->pCommandHistory[op]));
                    }
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
            const auto fault_status = dred->GetPageFaultAllocationOutput1(&fault);
            dingosdk::logging::printf(dingosdk::logging::Level::debug, dingosdk::logging::Channel::graphics, "DRED page_fault result=0x%08lx address=0x%llx existing=%p freed=%p",
                static_cast<unsigned long>(fault_status), fault.PageFaultVA,
                fault.pHeadExistingAllocationNode, fault.pHeadRecentFreedAllocationNode);
        }
    } catch (...) {}
    SetLastError(error);
}
}
