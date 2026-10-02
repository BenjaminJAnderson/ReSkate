#pragma once

#include <Windows.h>
#include <d3d12.h>

namespace dingosdk::overlay {
bool graphics_option_enabled(const wchar_t* name) noexcept;
void initialize_graphics_diagnostics() noexcept;
void record_device_failure(ID3D12Device* device, HRESULT result,
    const char* operation) noexcept;
}
