#include "writer.hpp"
#import <AVFoundation/AVFoundation.h>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace hive::video {
namespace {
class MacWriter final : public Writer {
    AVAssetWriter* writer_;
    AVAssetWriterInput* input_;
    AVAssetWriterInputPixelBufferAdaptor* pixels_;
    QSize size_;
    bool finished_{};
    [[noreturn]] void fail() { throw std::runtime_error("The Mac video encoder could not write this clip"); }
public:
    MacWriter(const QString& file,QSize size,int fps,int bitrate):size_(size) {
        @autoreleasepool {
            NSError* error=nil;
            writer_=[[AVAssetWriter alloc] initWithURL:[NSURL fileURLWithPath:file.toNSString()] fileType:AVFileTypeMPEG4 error:&error];
            if (!writer_ || error) fail();
            NSDictionary* settings=@{AVVideoCodecKey:AVVideoCodecTypeH264,AVVideoWidthKey:@(size.width()),AVVideoHeightKey:@(size.height()),
                AVVideoCompressionPropertiesKey:@{AVVideoAverageBitRateKey:@(bitrate),AVVideoExpectedSourceFrameRateKey:@(fps),AVVideoMaxKeyFrameIntervalKey:@(fps*2)}};
            input_=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
            input_.expectsMediaDataInRealTime=YES;
            pixels_=[AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input_
                sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
                    (id)kCVPixelBufferWidthKey:@(size.width()),(id)kCVPixelBufferHeightKey:@(size.height())}];
            if (![writer_ canAddInput:input_]) fail();
            [writer_ addInput:input_];
            writer_.shouldOptimizeForNetworkUse=YES;
            if (![writer_ startWriting]) fail();
            [writer_ startSessionAtSourceTime:kCMTimeZero];
        }
    }
    ~MacWriter() override { if (!finished_) [writer_ cancelWriting]; }
    void append(const QImage& image,qint64 milliseconds) override {
        @autoreleasepool {
            if (image.size()!=size_) throw std::invalid_argument("Clip frame size changed");
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (!input_.readyForMoreMediaData) {
                if (writer_.status!=AVAssetWriterStatusWriting || std::chrono::steady_clock::now()>until) fail();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            CVPixelBufferRef pixel=nullptr;
            if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,pixels_.pixelBufferPool,&pixel)!=kCVReturnSuccess) fail();
            CVPixelBufferLockBaseAddress(pixel,0);
            const auto frame=image.convertToFormat(QImage::Format_ARGB32);
            auto* data=static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(pixel));
            const auto stride=CVPixelBufferGetBytesPerRow(pixel);
            for (int y=0;y<size_.height();++y) memcpy(data+y*stride,frame.constScanLine(y),size_.width()*4);
            CVPixelBufferUnlockBaseAddress(pixel,0);
            const bool ok=[pixels_ appendPixelBuffer:pixel withPresentationTime:CMTimeMake(milliseconds,1000)];
            CVPixelBufferRelease(pixel);
            if (!ok) fail();
        }
    }
    void finish() override {
        [input_ markAsFinished];
        const auto done=dispatch_semaphore_create(0);
        [writer_ finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
        if (dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC)) || writer_.status!=AVAssetWriterStatusCompleted) fail();
        finished_=true;
    }
};
}
std::unique_ptr<Writer> writer(const QString& file,QSize size,int fps,int bitrate) { return std::make_unique<MacWriter>(file,size,fps,bitrate); }
}
