#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include "core/FileIo.h"

namespace gm::engine {
inline bool ValidImage(const std::string& bytes) {
    if(bytes.size()<12 || bytes.size()>64*1024*1024) return false;
    struct Apartment {
        HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ~Apartment(){if(SUCCEEDED(hr)) CoUninitialize();}
    } apartment;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),(UINT)bytes.size()));
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if(!stream || FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) ||
       FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) ||
       FAILED(decoder->GetFrame(0,&frame))) return false;
    UINT width=0,height=0;
    if(FAILED(frame->GetSize(&width,&height)) || !width || !height || uint64_t(width)*height>100000000 || width>100000) return false;
    if(FAILED(factory->CreateFormatConverter(&converter)) ||
       FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
    std::vector<BYTE> stripe;
    try {stripe.resize(size_t(width)*4*std::min<UINT>(64,height));} catch(...) {return false;}
    for(UINT y=0;y<height;y+=64) {
        WICRect rect{0,(INT)y,(INT)width,(INT)std::min<UINT>(64,height-y)};
        if(FAILED(converter->CopyPixels(&rect,width*4,(UINT)stripe.size(),stripe.data()))) return false;
    }
    return true;
}
inline bool ValidImageFile(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; DWORD read=0; std::string bytes;
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart>=12 && size.QuadPart<=64*1024*1024;
    if(ok) {
        try {bytes.resize((size_t)size.QuadPart);} catch(...) {ok=false;}
        if(ok) ok=ReadFile(file,bytes.data(),(DWORD)bytes.size(),&read,nullptr) && read==bytes.size();
    }
    CloseHandle(file);
    return ok && ValidImage(bytes);
}
}
