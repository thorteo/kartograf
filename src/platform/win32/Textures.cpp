#include "platform/win32/Textures.h"

namespace tarkov::win32 {

ComPtr<ID3D11ShaderResourceView> createRgbaTexture(ID3D11Device* device, int width,
                                                   int height,
                                                   const std::uint8_t* pixels) {
    ComPtr<ID3D11ShaderResourceView> view;
    if (device == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return view;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels;
    initial.SysMemPitch = static_cast<UINT>(width) * 4;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &initial, texture.put()))) {
        return view;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texture.get(), &viewDesc, view.put()))) {
        view.reset();
    }
    return view;
}

}  // namespace tarkov::win32
