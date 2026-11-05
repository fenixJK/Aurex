#include "writer.hpp"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <stdexcept>

namespace hive::video {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Windows could not encode the clip (Media Foundation error "
        +std::to_string(static_cast<unsigned long>(result))+"). Check that Windows media components are installed.");
}
struct Runtime {
    bool com{},media{};
    Runtime() {
        const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (result!=RPC_E_CHANGED_MODE) { check(result); com=true; }
        const auto started=MFStartup(MF_VERSION);
        if (FAILED(started)) { if (com) CoUninitialize(); check(started); }
        media=true;
    }
    ~Runtime() { if (media) MFShutdown(); if (com) CoUninitialize(); }
};
class WindowsWriter final : public Writer {
    Runtime runtime_;
    ComPtr<IMFSinkWriter> writer_;
    DWORD stream_{};
    QSize size_;
    int fps_;
public:
    WindowsWriter(const QString& file,QSize size,int fps,int bitrate):size_(size),fps_(fps) {
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes,1));
        check(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
        check(MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(file.utf16()),nullptr,attributes.Get(),&writer_));
        ComPtr<IMFMediaType> output,input;
        check(MFCreateMediaType(&output)); check(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        check(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264)); check(output->SetUINT32(MF_MT_AVG_BITRATE,bitrate));
        check(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,size.width(),size.height()));
        check(MFSetAttributeRatio(output.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        check(writer_->AddStream(output.Get(),&stream_));
        check(MFCreateMediaType(&input)); check(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        check(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12));
        check(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,size.width(),size.height()));
        check(MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        check(writer_->SetInputMediaType(stream_,input.Get(),nullptr)); check(writer_->BeginWriting());
    }
    void append(const QImage& image,qint64 milliseconds) override {
        if (image.size()!=size_) throw std::invalid_argument("Clip frame size changed");
        const auto rgb=image.convertToFormat(QImage::Format_RGB32);
        const int width=size_.width(),height=size_.height();
        ComPtr<IMFMediaBuffer> buffer; check(MFCreateMemoryBuffer(width*height*3/2,&buffer));
        BYTE* data=nullptr; check(buffer->Lock(&data,nullptr,nullptr));
        const auto byte=[](int value) { return BYTE(std::clamp(value,0,255)); };
        // NV12 avoids the bottom-up RGB buffer convention on Windows. Both
        // planes are top-down; chroma is averaged over each 2x2 pixel block.
        for (int y=0;y<height;++y) {
            const auto* row=reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
            for (int x=0;x<width;++x) data[y*width+x]=byte(((66*qRed(row[x])+129*qGreen(row[x])+25*qBlue(row[x])+128)>>8)+16);
        }
        for (int y=0;y<height;y+=2) for (int x=0;x<width;x+=2) {
            int r=0,g=0,b=0;
            for (int dy=0;dy<2;++dy) {
                const auto* row=reinterpret_cast<const QRgb*>(rgb.constScanLine(y+dy));
                for (int dx=0;dx<2;++dx) { r+=qRed(row[x+dx]); g+=qGreen(row[x+dx]); b+=qBlue(row[x+dx]); }
            }
            r/=4; g/=4; b/=4;
            auto* uv=data+width*height+(y/2)*width+x;
            uv[0]=byte(((-38*r-74*g+112*b+128)>>8)+128);
            uv[1]=byte(((112*r-94*g-18*b+128)>>8)+128);
        }
        check(buffer->Unlock()); check(buffer->SetCurrentLength(width*height*3/2));
        ComPtr<IMFSample> sample; check(MFCreateSample(&sample)); check(sample->AddBuffer(buffer.Get()));
        check(sample->SetSampleTime(milliseconds*10000)); check(sample->SetSampleDuration(10000000/fps_));
        check(writer_->WriteSample(stream_,sample.Get()));
    }
    void finish() override { check(writer_->Finalize()); }
};
}
std::unique_ptr<Writer> writer(const QString& file,QSize size,int fps,int bitrate) { return std::make_unique<WindowsWriter>(file,size,fps,bitrate); }
}
