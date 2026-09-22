#pragma once

#include <d3d11.h>

#include <cstdint>

#include "platform/win32/ComPtr.h"

namespace tarkov::win32 {

/// An immutable RGBA8 texture and its shader view, from tightly packed pixels.
/// Empty on failure. Render thread: the immediate context is not shared.
ComPtr<ID3D11ShaderResourceView> createRgbaTexture(ID3D11Device* device, int width,
                                                   int height,
                                                   const std::uint8_t* pixels);

}  // namespace tarkov::win32
