#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <stdio.h>

using Microsoft::WRL::ComPtr;

static HRESULT Convert(const wchar_t* input, const wchar_t* output)
{
    constexpr UINT side = 128;
    ComPtr<ID3D11Device> d3d;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(hr = d3d.As(&dxgi))) return hr;
    ComPtr<ID2D1Factory1> factory;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
        nullptr, reinterpret_cast<void**>(factory.GetAddressOf())))) return hr;
    ComPtr<ID2D1Device> device;
    if (FAILED(hr = factory->CreateDevice(dxgi.Get(), &device))) return hr;
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(hr = device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context))) return hr;
    ComPtr<ID2D1DeviceContext5> d2d;
    if (FAILED(hr = context.As(&d2d))) return hr;
    ComPtr<IStream> svg;
    if (FAILED(hr = SHCreateStreamOnFileEx(input, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &svg))) return hr;
    ComPtr<ID2D1SvgDocument> document;
    const D2D1_SIZE_F viewport = {static_cast<float>(side), static_cast<float>(side)};
    if (FAILED(hr = d2d->CreateSvgDocument(svg.Get(), viewport, &document))) return hr;

    D2D1_BITMAP_PROPERTIES1 properties = {};
    properties.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    properties.dpiX = properties.dpiY = 96;
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    const D2D1_SIZE_U size = {side, side};
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(hr = d2d->CreateBitmap(size, nullptr, 0, &properties, &target))) return hr;
    d2d->SetTarget(target.Get());
    d2d->BeginDraw();
    const D2D1_COLOR_F clear = {0, 0, 0, 0};
    d2d->Clear(&clear);
    d2d->DrawSvgDocument(document.Get());
    hr = d2d->EndDraw();
    d2d->SetTarget(nullptr);
    if (FAILED(hr)) return hr;

    properties.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
    ComPtr<ID2D1Bitmap1> readable;
    if (FAILED(hr = d2d->CreateBitmap(size, nullptr, 0, &properties, &readable))) return hr;
    if (FAILED(hr = readable->CopyFromBitmap(nullptr, target.Get(), nullptr))) return hr;
    D2D1_MAPPED_RECT mapped = {};
    if (FAILED(hr = readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return hr;
    bool visible = false;
    for (UINT y = 0; y < side && !visible; ++y)
        for (UINT x = 0; x < side; ++x)
            if (mapped.bits[y * mapped.pitch + x * 4 + 3]) { visible = true; break; }

    ComPtr<IWICImagingFactory> wic;
    hr = visible ? CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))
                 : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    ComPtr<IWICBitmap> source;
    if (SUCCEEDED(hr)) hr = wic->CreateBitmapFromMemory(side, side, GUID_WICPixelFormat32bppPBGRA,
        mapped.pitch, mapped.pitch * side, mapped.bits, &source);
    ComPtr<IWICStream> outputStream;
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&outputStream);
    if (SUCCEEDED(hr)) hr = outputStream->InitializeFromFilename(output, GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(outputStream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(side, side);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    ComPtr<IWICFormatConverter> convert;
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&convert);
    if (SUCCEEDED(hr)) hr = convert->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(convert.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    HRESULT unmap = readable->Unmap();
    return FAILED(hr) ? hr : unmap;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        fwprintf(stderr, L"Usage: svg_to_png.exe <input.svg> <output.png>\n");
        return 2;
    }
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init))
    {
        fwprintf(stderr, L"Could not initialize COM: 0x%08lx\n", static_cast<unsigned long>(init));
        return 1;
    }
    HRESULT hr = Convert(argv[1], argv[2]);
    CoUninitialize();
    if (FAILED(hr))
    {
        fwprintf(stderr, L"Could not render %ls: 0x%08lx\n", argv[1], static_cast<unsigned long>(hr));
        return 1;
    }
    return 0;
}
