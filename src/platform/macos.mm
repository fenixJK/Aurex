#include "platform.hpp"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <Vision/Vision.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace hive {
namespace {
std::string text(NSString* value) { return value ? std::string(value.UTF8String) : std::string(); }
bool roblox_app(NSRunningApplication* app) {
    return app && !app.terminated && ([app.bundleIdentifier isEqualToString:@"com.roblox.RobloxPlayer"]
        || [app.bundleIdentifier isEqualToString:@"com.roblox.Roblox"]);
}
template<class T> struct Reply {
    std::mutex mutex;
    std::condition_variable changed;
    bool done{};
    T value{};
    std::string error;
    void wait() {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return done; })) throw std::runtime_error("Screen capture timed out");
        if (!error.empty()) throw std::runtime_error(error);
    }
};
CGKeyCode code(Key key) {
    switch(key) {
    case Key::shift_lock: return 56;
    case Key::game_menu: return 53; case Key::respawn: return 15; case Key::confirm: case Key::chat_submit: return 36; case Key::chat: return 44;
    case Key::digit_0: return 29; case Key::digit_8: return 28; case Key::digit_9: return 25; case Key::select_all: return 0;
    case Key::forward: return 13; case Key::backward: return 1; case Key::left: return 0;
    case Key::right: return 2; case Key::jump: return 49; case Key::interact: return 14;
    case Key::rotate_left: return 43; case Key::rotate_right: return 47;
    case Key::pitch_up: return 116; case Key::pitch_down: return 121;
    case Key::zoom_in: return 34; case Key::zoom_out: return 31;
    case Key::hotbar_1: return 18; case Key::hotbar_2: return 19; case Key::hotbar_3: return 20;
    case Key::hotbar_4: return 21; case Key::hotbar_5: return 23; case Key::hotbar_6: return 22; case Key::hotbar_7: return 26;
    default: throw std::invalid_argument("Unsupported keyboard key");
    }
}
std::optional<Window> window_entry(NSDictionary* entry) {
    CGRect rect{};
    if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)entry[(__bridge NSString*)kCGWindowBounds], &rect)) return {};
    return Window{[entry[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue],
        [entry[(__bridge NSString*)kCGWindowOwnerPID] longLongValue], text(entry[(__bridge NSString*)kCGWindowName]),
        {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height}};
}
std::optional<Window> window_info(std::uint64_t id) {
    const void* value=reinterpret_cast<const void*>(static_cast<uintptr_t>(id));
    const auto ids=CFArrayCreate(nullptr,&value,1,nullptr);
    NSArray* list=CFBridgingRelease(CGWindowListCreateDescriptionFromArray(ids));
    CFRelease(ids);
    for (NSDictionary* entry in list) {
        if ([entry[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue] != id) continue;
        return window_entry(entry);
    }
    return std::nullopt;
}
class MacDesktop final : public Desktop {
    bool primary_down_{};
    std::optional<Point> pending_pointer_;
    std::mutex capture_mutex_;
    Window capture_window_;
    SCContentFilter* capture_filter_{};
public:
    bool client_lifecycle_available() const override { return true; }
    std::vector<std::int64_t> client_processes() override {
        @autoreleasepool {
            std::vector<std::int64_t> result;
            for (NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications)
                if (roblox_app(app)) result.push_back(app.processIdentifier);
            return result;
        }
    }
    bool client_running(std::int64_t pid) override {
        @autoreleasepool {
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            return roblox_app(app);
        }
    }
    bool close_client(const Window& window) override {
        @autoreleasepool {
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:window.process];
            if (!app || app.terminated) return true;
            return roblox_app(app) && [app terminate];
        }
    }
    bool force_close_client(const Window& window) override {
        @autoreleasepool {
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:window.process];
            if (!app || app.terminated) return true;
            return roblox_app(app) && [app forceTerminate];
        }
    }
    std::future<bool> open_client(const std::string& uri) override {
        std::promise<bool> result;
        @autoreleasepool {
            NSURL* url=[NSURL URLWithString:[NSString stringWithUTF8String:uri.c_str()]];
            result.set_value(url && [NSWorkspace.sharedWorkspace openURL:url]);
        }
        return result.get_future();
    }
    bool focus_client(const Window& window) override {
        @autoreleasepool {
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:window.process];
            if (!roblox_app(app)) return false;
            [app unhide];
            if (AXIsProcessTrusted()) {
                auto application=AXUIElementCreateApplication(static_cast<pid_t>(window.process));
                CFTypeRef windows=nullptr;
                if (AXUIElementCopyAttributeValue(application,kAXWindowsAttribute,&windows)==kAXErrorSuccess && windows) {
                    for (id item in (__bridge NSArray*)windows) {
                        auto candidate=(__bridge AXUIElementRef)item;
                        CFTypeRef position=nullptr,size=nullptr;
                        CGPoint origin{}; CGSize extent{};
                        const bool matched=AXUIElementCopyAttributeValue(candidate,kAXPositionAttribute,&position)==kAXErrorSuccess
                            && AXUIElementCopyAttributeValue(candidate,kAXSizeAttribute,&size)==kAXErrorSuccess
                            && AXValueGetValue(static_cast<AXValueRef>(position),kAXValueTypeCGPoint,&origin)
                            && AXValueGetValue(static_cast<AXValueRef>(size),kAXValueTypeCGSize,&extent)
                            && std::abs(origin.x-window.bounds.x)<2 && std::abs(origin.y-window.bounds.y)<2
                            && std::abs(extent.width-window.bounds.width)<2 && std::abs(extent.height-window.bounds.height)<2;
                        if (position) CFRelease(position);
                        if (size) CFRelease(size);
                        if (!matched) continue;
                        AXUIElementSetAttributeValue(candidate,kAXMinimizedAttribute,kCFBooleanFalse);
                        AXUIElementPerformAction(candidate,kAXRaiseAction);
                        break;
                    }
                    CFRelease(windows);
                }
                AXUIElementSetAttributeValue(application,kAXFrontmostAttribute,kCFBooleanTrue);
                CFRelease(application);
            }
            if (app.active) return true;
            [NSApp yieldActivationToApplication:app];
            [app activateFromApplication:NSRunningApplication.currentApplication options:0];
            return [app activateWithOptions:0];
        }
    }
    bool focus_client_process(std::int64_t pid) override {
        @autoreleasepool {
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            if (!roblox_app(app)) return false;
            [app unhide];
            if (app.active) return true;
            [NSApp yieldActivationToApplication:app];
            [app activateFromApplication:NSRunningApplication.currentApplication options:0];
            return [app activateWithOptions:0];
        }
    }
    Permissions permissions() override {
        const bool capture = CGPreflightScreenCaptureAccess(), input = AXIsProcessTrusted();
        return {capture, input, capture && input ? "Ready" : "Allow Screen Recording and Accessibility for this app in System Settings"};
    }
    void request_capture_permission() override {
        if (!CGPreflightScreenCaptureAccess()) CGRequestScreenCaptureAccess();
        [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
    }
    void request_input_permission() override {
        [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
    }
    std::vector<Window> windows() override {
        @autoreleasepool {
            std::vector<Window> result;
            NSArray* list = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
            for (NSDictionary* entry in list) {
                if ([entry[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;
                const auto pid = [entry[(__bridge NSString*)kCGWindowOwnerPID] intValue];
                NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
                if (!roblox_app(app)) continue;
                if (auto window = window_entry(entry); window && window->bounds.width >= 300 && window->bounds.height >= 200)
                    result.push_back(*window);
            }
            std::stable_sort(result.begin(),result.end(),[](const Window& a,const Window& b) {
                return a.bounds.width*a.bounds.height>b.bounds.width*b.bounds.height;
            });
            return result;
        }
    }
    Snapshot inspect(std::uint64_t id) override {
        @autoreleasepool {
            auto window = window_info(id);
            const bool escape = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 53);
            const bool pause = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 120);
            if (!window) return {{}, false, AXIsProcessTrusted() != 0, escape};
            const auto active = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
            return {*window, active == window->process, AXIsProcessTrusted() != 0, escape, pause};
        }
    }
    Frame capture(std::uint64_t id) override { return capture(id,{0,0,1,1}); }
    Frame capture(std::uint64_t id,Rect region) override {
        if (!region.valid() || region.x<0 || region.y<0 || region.x+region.width>1 || region.y+region.height>1)
            throw std::invalid_argument("Invalid capture region");
        @autoreleasepool {
            if (!CGPreflightScreenCaptureAccess()) throw std::runtime_error("Screen Recording permission is required");
            auto window = window_info(id);
            if (!window) throw std::runtime_error("Game window is no longer visible");
            std::lock_guard capture_lock(capture_mutex_);
            if (!capture_filter_ || capture_window_.id!=window->id || capture_window_.process!=window->process
                || capture_window_.bounds!=window->bounds) {
                auto content = std::make_shared<Reply<__strong SCShareableContent*>>();
                [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:NO completionHandler:^(SCShareableContent* value, NSError* error) {
                    std::lock_guard lock(content->mutex);
                    content->value = value; content->error = text(error.localizedDescription); content->done = true; content->changed.notify_one();
                }];
                content->wait();
                SCWindow* target = nil;
                for (SCWindow* candidate in content->value.windows) if (candidate.windowID == id) { target = candidate; break; }
                if (!target) throw std::runtime_error("Game window disappeared before capture");
                capture_filter_ = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target];
                capture_window_=*window;
            }
            SCContentFilter* filter=capture_filter_;
            SCStreamConfiguration* config = [SCStreamConfiguration new];
            config.width = static_cast<size_t>(filter.contentRect.size.width * filter.pointPixelScale);
            config.height = static_cast<size_t>(filter.contentRect.size.height * filter.pointPixelScale);
            config.showsCursor = NO;
            config.ignoreShadowsSingleWindow = YES;
            auto reply = std::make_shared<Reply<Frame>>();
            const Window observed = *window;
            const auto captured=monotonic_now();
            [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError* error) {
                std::lock_guard lock(reply->mutex);
                reply->error = text(error.localizedDescription);
                if (image) {
                    // Window capture ignores sourceRect on supported macOS
                    // versions. Crop the CGImage before allocating/copying RGBA.
                    const double width=CGImageGetWidth(image),height=CGImageGetHeight(image);
                    const double left=std::floor(width*region.x),top=std::floor(height*region.y);
                    const double right=std::min(width,std::ceil(width*(region.x+region.width)));
                    const double bottom=std::min(height,std::ceil(height*(region.y+region.height)));
                    CGImageRef cropped=CGImageCreateWithImageInRect(image,CGRectMake(left,top,right-left,bottom-top));
                    auto& frame = reply->value;
                    frame.window = observed; frame.captured = captured;
                    frame.region={left/width,top/height,(right-left)/width,(bottom-top)/height};
                    frame.width = cropped?static_cast<int>(CGImageGetWidth(cropped)):0;
                    frame.height = cropped?static_cast<int>(CGImageGetHeight(cropped)):0;
                    frame.rgba.resize(static_cast<size_t>(frame.width)*frame.height*4);
                    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                    auto context = CGBitmapContextCreate(frame.rgba.data(), frame.width, frame.height, 8, frame.width*4, space, static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
                    CGColorSpaceRelease(space);
                    if (context) {
                        CGContextDrawImage(context, CGRectMake(0,0,frame.width,frame.height), cropped); CGContextRelease(context);
                    } else reply->error = "Could not allocate capture buffer";
                    if (cropped) CGImageRelease(cropped);
                } else if (reply->error.empty()) reply->error = "Screen capture returned no image";
                reply->done = true; reply->changed.notify_one();
            }];
            try { reply->wait(); }
            catch (...) { capture_filter_=nil; throw; }
            auto latest = window_info(id);
            if (!latest || latest->bounds != window->bounds) throw std::runtime_error("Window geometry changed during capture");
            return std::move(reply->value);
        }
    }
    std::vector<TextLine> recognize(const Frame& frame) override {
        @autoreleasepool {
            if (!frame.valid()) throw std::invalid_argument("Invalid OCR frame");
            auto provider = CGDataProviderCreateWithData(nullptr, frame.rgba.data(), frame.rgba.size(), nullptr);
            auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            auto image = CGImageCreate(frame.width,frame.height,8,32,frame.width*4,space,static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big,provider,nullptr,false,kCGRenderingIntentDefault);
            CGDataProviderRelease(provider); CGColorSpaceRelease(space);
            if (!image) throw std::runtime_error("Could not create OCR image");
            VNRecognizeTextRequest* request = [VNRecognizeTextRequest new];
            request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            request.recognitionLanguages = @[@"en-US"]; request.usesLanguageCorrection = NO;
            VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
            CGImageRelease(image);
            NSError* error = nil;
            if (![handler performRequests:@[request] error:&error]) throw std::runtime_error(text(error.localizedDescription));
            std::vector<TextLine> lines;
            for (VNRecognizedTextObservation* observation in request.results) {
                VNRecognizedText* candidate = [observation topCandidates:1].firstObject;
                if (!candidate) continue;
                CGRect r = observation.boundingBox;
                lines.push_back({text(candidate.string), {r.origin.x*frame.width, (1-r.origin.y-r.size.height)*frame.height,r.size.width*frame.width,r.size.height*frame.height},candidate.confidence});
            }
            return lines;
        }
    }
    void key(Key key, bool down) override {
        CGEventRef event;
        if (key == Key::primary) {
            auto locationEvent = CGEventCreate(nullptr);
            auto position = CGEventGetLocation(locationEvent); CFRelease(locationEvent);
            if (pending_pointer_) position=CGPointMake(pending_pointer_->x,pending_pointer_->y);
            event = CGEventCreateMouseEvent(nullptr, down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp, position, kCGMouseButtonLeft);
        } else event = CGEventCreateKeyboardEvent(nullptr, code(key), down);
        pending_pointer_.reset();
        if (!event) throw std::runtime_error("Could not create input event");
        CGEventSetFlags(event,(key==Key::pitch_up || key==Key::pitch_down)?kCGEventFlagMaskSecondaryFn:0);
        if (key==Key::shift_lock) CGEventSetFlags(event,down?kCGEventFlagMaskShift:0);
        if (key==Key::select_all) CGEventSetFlags(event,down?kCGEventFlagMaskCommand:0);
        CGEventPost(kCGHIDEventTap, event); CFRelease(event);
        if (key==Key::primary) primary_down_=down;
    }
    void pointer(Point point) override {
        auto event = CGEventCreateMouseEvent(nullptr, primary_down_?kCGEventLeftMouseDragged:kCGEventMouseMoved, CGPointMake(point.x, point.y), kCGMouseButtonLeft);
        if (!event) throw std::runtime_error("Could not create pointer event");
        CGEventPost(kCGHIDEventTap,event); CFRelease(event);
        pending_pointer_=point;
    }
    void scroll(int lines) override {
        if (lines<-8 || lines>8) throw std::invalid_argument("Invalid scroll distance");
        auto event=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,lines);
        if (!event) throw std::runtime_error("Could not create scroll event");
        if (pending_pointer_) CGEventSetLocation(event,CGPointMake(pending_pointer_->x,pending_pointer_->y));
        pending_pointer_.reset();
        CGEventPost(kCGHIDEventTap,event); CFRelease(event);
    }
};
}
std::unique_ptr<Desktop> make_desktop() { return std::make_unique<MacDesktop>(); }
}
