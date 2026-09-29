#include "thumbnail.hpp"
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

template <typename T>
struct Ref {
    T* ptr = nullptr;
    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { if (ptr) ptr->Release(); }
    T** out() { if (ptr) { ptr->Release(); ptr = nullptr; } return &ptr; }
    T* operator->() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};

constexpr uint32_t BACKGROUND = 0x00202020;  // BGRX

std::wstring lowerExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

// Scales any WIC source to fit the box and draws it centred on a dark DIB.
HBITMAP renderToDib(IWICImagingFactory* factory, IWICBitmapSource* source, int width, int height) {
    UINT sw = 0, sh = 0;
    if (FAILED(source->GetSize(&sw, &sh)) || !sw || !sh) return nullptr;
    const double scale = std::min(double(width) / sw, double(height) / sh);
    const UINT tw = std::max(1u, UINT(std::lround(sw * scale)));
    const UINT th = std::max(1u, UINT(std::lround(sh * scale)));

    Ref<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.out())) ||
        FAILED(scaler->Initialize(source, tw, th, WICBitmapInterpolationModeFant))) return nullptr;
    Ref<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.out())) ||
        FAILED(converter->Initialize(scaler.ptr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    std::vector<uint8_t> pixels(size_t(tw) * th * 4);
    if (FAILED(converter->CopyPixels(nullptr, tw * 4, UINT(pixels.size()), pixels.data()))) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) return nullptr;
    auto* target = static_cast<uint32_t*>(bits);
    std::fill(target, target + size_t(width) * height, BACKGROUND);
    const int left = (width - int(tw)) / 2, top = (height - int(th)) / 2;
    for (UINT y = 0; y < th; ++y) {
        for (UINT x = 0; x < tw; ++x) {
            const uint8_t* p = &pixels[(size_t(y) * tw + x) * 4];
            const uint32_t inverse = 255 - p[3];  // source is premultiplied
            const uint32_t b = p[0] + ((BACKGROUND & 0xff) * inverse + 127) / 255;
            const uint32_t g = p[1] + (((BACKGROUND >> 8) & 0xff) * inverse + 127) / 255;
            const uint32_t r = p[2] + (((BACKGROUND >> 16) & 0xff) * inverse + 127) / 255;
            target[size_t(top + int(y)) * width + left + int(x)] =
                (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
        }
    }
    return bitmap;
}

HBITMAP imageThumbnail(const std::wstring& path, int width, int height) {
    Ref<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.out())))) return nullptr;
    Ref<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, decoder.out()))) return nullptr;
    Ref<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.out()))) return nullptr;
    return renderToDib(factory.ptr, frame.ptr, width, height);
}

HBITMAP videoThumbnail(const std::wstring& path, int width, int height) {
    Ref<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(attributes.out(), 1)) ||
        FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))) return nullptr;
    Ref<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.ptr, reader.out()))) return nullptr;
    const DWORD stream = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader->SetStreamSelection(stream, TRUE))) return nullptr;

    Ref<IMFMediaType> wanted;
    if (FAILED(MFCreateMediaType(wanted.out())) ||
        FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(reader->SetCurrentMediaType(stream, nullptr, wanted.ptr))) return nullptr;
    Ref<IMFMediaType> actual;
    UINT32 vw = 0, vh = 0;
    if (FAILED(reader->GetCurrentMediaType(stream, actual.out())) ||
        FAILED(MFGetAttributeSize(actual.ptr, MF_MT_FRAME_SIZE, &vw, &vh)) || !vw || !vh) return nullptr;
    LONG stride = 0;
    if (FAILED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, vw, &stride)) || stride == 0)
        stride = -LONG(vw) * 4;
    const bool bottomUp = stride < 0;
    const size_t pitch = size_t(bottomUp ? -stride : stride);

    // Aim for the middle of the clip; the reader delivers frames from the previous
    // key frame on, so read forward until the target time is reached.
    LONGLONG target = 0;
    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) &&
        duration.vt == VT_UI8) target = LONGLONG(duration.uhVal.QuadPart / 2);
    PropVariantClear(&duration);
    if (target > 0) {
        PROPVARIANT position;
        if (SUCCEEDED(InitPropVariantFromInt64(target, &position))) {
            reader->SetCurrentPosition(GUID_NULL, position);
            PropVariantClear(&position);
        }
    }

    Ref<IMFSample> best;
    for (int i = 0; i < 240; ++i) {
        DWORD flags = 0;
        LONGLONG stamp = 0;
        IMFSample* sample = nullptr;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &stamp, &sample))) break;
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) {
            if (sample) sample->Release();
            break;
        }
        if (!sample) continue;
        if (best.ptr) best.ptr->Release();
        best.ptr = sample;  // best now owns the reference
        if (stamp >= target) break;
    }
    if (!best) return nullptr;

    Ref<IMFMediaBuffer> buffer;
    if (FAILED(best->ConvertToContiguousBuffer(buffer.out()))) return nullptr;
    BYTE* data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length)) || !data) return nullptr;
    HBITMAP result = nullptr;
    if (length >= pitch * vh) {
        // Hand the frame to WIC as an ordinary top-down 32-bit bitmap.
        std::vector<uint8_t> frame(size_t(vw) * vh * 4);
        for (UINT32 y = 0; y < vh; ++y) {
            const BYTE* row = data + pitch * (bottomUp ? vh - 1 - y : y);
            uint8_t* out = &frame[size_t(y) * vw * 4];
            for (UINT32 x = 0; x < vw; ++x) {  // force opaque alpha; RGB32 leaves it undefined
                out[x * 4 + 0] = row[x * 4 + 0];
                out[x * 4 + 1] = row[x * 4 + 1];
                out[x * 4 + 2] = row[x * 4 + 2];
                out[x * 4 + 3] = 255;
            }
        }
        Ref<IWICImagingFactory> factory;
        Ref<IWICBitmap> bitmap;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(factory.out()))) &&
            SUCCEEDED(factory->CreateBitmapFromMemory(vw, vh, GUID_WICPixelFormat32bppBGRA, vw * 4,
                                                      UINT(frame.size()), frame.data(), bitmap.out())))
            result = renderToDib(factory.ptr, bitmap.ptr, width, height);
    }
    buffer->Unlock();
    return result;
}

}  // namespace

HBITMAP createThumbnail(const std::wstring& path, int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    try {
        if (lowerExtension(path) == L".mp4") {
            if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return nullptr;
            HBITMAP bitmap = videoThumbnail(path, width, height);
            MFShutdown();
            return bitmap;
        }
        return imageThumbnail(path, width, height);
    } catch (...) {
        return nullptr;
    }
}
