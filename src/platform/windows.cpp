#include "platform.hpp"
#include "ocr.hpp"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <stdexcept>

namespace hive {
namespace {
std::string utf8(const std::wstring& s) { return winrt::to_string(s); }
Window describe(HWND hwnd) {
    RECT r{}; POINT origin{}; DWORD pid{}; wchar_t title[512]{};
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || !GetClientRect(hwnd,&r)
        || !ClientToScreen(hwnd,&origin) || r.right<=0 || r.bottom<=0) return {};
    GetWindowThreadProcessId(hwnd,&pid); GetWindowTextW(hwnd,title,512);
    return {reinterpret_cast<std::uint64_t>(hwnd),pid,utf8(title),{double(origin.x),double(origin.y),double(r.right),double(r.bottom)}};
}
WORD code(Key key) {
    switch (key) {
    case Key::shift_lock: return VK_LSHIFT;
    case Key::game_menu: return VK_ESCAPE; case Key::respawn: return 'R'; case Key::confirm: return VK_RETURN;
    case Key::digit_0: return '0'; case Key::digit_8: return '8'; case Key::digit_9: return '9';
    case Key::forward: return 'W'; case Key::backward: return 'S'; case Key::left: return 'A'; case Key::right: return 'D';
    case Key::jump: return VK_SPACE; case Key::interact: return 'E'; case Key::rotate_left: return VK_OEM_COMMA; case Key::rotate_right: return VK_OEM_PERIOD;
    case Key::pitch_up: return VK_PRIOR; case Key::pitch_down: return VK_NEXT;
    case Key::zoom_in: return 'I'; case Key::zoom_out: return 'O';
    case Key::hotbar_1: return '1'; case Key::hotbar_2: return '2'; case Key::hotbar_3: return '3';
    case Key::hotbar_4: return '4'; case Key::hotbar_5: return '5'; case Key::hotbar_6: return '6'; case Key::hotbar_7: return '7';
    default: throw std::invalid_argument("Unsupported keyboard key");
    }
}
bool roblox_process(std::int64_t pid) {
    if (pid<=0 || pid>MAXDWORD) return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid));
    if (!process) return false;
    wchar_t path[32768]{}; DWORD size=32768,exit_code{};
    const bool ok=GetExitCodeProcess(process,&exit_code) && exit_code==STILL_ACTIVE && QueryFullProcessImageNameW(process,0,path,&size);
    CloseHandle(process);
    if (!ok) return false;
    std::wstring name(path); const auto slash=name.find_last_of(L"\\/");
    return _wcsicmp(name.substr(slash==std::wstring::npos?0:slash+1).c_str(),L"RobloxPlayerBeta.exe")==0;
}
class WindowsDesktop final : public Desktop {
public:
    bool client_lifecycle_available() const override { return true; }
    std::vector<std::int64_t> client_processes() override {
        std::vector<std::int64_t> result;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if (snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Could not inspect Roblox processes");
        PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
        if (Process32FirstW(snapshot,&entry)) do {
            if (_wcsicmp(entry.szExeFile,L"RobloxPlayerBeta.exe")==0) result.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot,&entry));
        CloseHandle(snapshot); return result;
    }
    bool client_running(std::int64_t pid) override { return roblox_process(pid); }
    bool close_client(const Window& window) override {
        if (!roblox_process(window.process)) return true;
        const auto hwnd=reinterpret_cast<HWND>(window.id);
        DWORD pid{}; GetWindowThreadProcessId(hwnd,&pid);
        if (pid!=window.process) return false;
        return PostMessageW(hwnd,WM_CLOSE,0,0)!=0;
    }
    bool open_client(const std::string& uri) override {
        const auto wide=winrt::to_hstring(uri);
        SHELLEXECUTEINFOW info{}; info.cbSize=sizeof(info); info.fMask=SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb=L"open"; info.lpFile=wide.c_str(); info.nShow=SW_SHOWNORMAL;
        const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE) return false;
        const bool launched=ShellExecuteExW(&info)!=0;
        if (SUCCEEDED(initialized)) CoUninitialize();
        return launched;
    }
    bool focus_client(const Window& window) override {
        if (!roblox_process(window.process)) return false;
        const auto hwnd=reinterpret_cast<HWND>(window.id);
        DWORD pid{}; GetWindowThreadProcessId(hwnd,&pid);
        if (pid!=window.process || !IsWindowVisible(hwnd)) return false;
        if (IsIconic(hwnd)) return ShowWindowAsync(hwnd,SW_RESTORE)!=0;
        return GetForegroundWindow()==hwnd || SetForegroundWindow(hwnd);
    }
    bool focus_client_process(std::int64_t pid) override {
        if (!roblox_process(pid)) return false;
        struct Search { DWORD pid; HWND window{}; } search{static_cast<DWORD>(pid)};
        EnumWindows([](HWND hwnd,LPARAM data) -> BOOL {
            auto& search=*reinterpret_cast<Search*>(data);
            DWORD pid{}; GetWindowThreadProcessId(hwnd,&pid);
            if (pid==search.pid && IsWindowVisible(hwnd) && !GetWindow(hwnd,GW_OWNER)) {
                search.window=hwnd; return FALSE;
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&search));
        return search.window && focus_client({reinterpret_cast<std::uint64_t>(search.window),pid,{},{}});
    }
    Permissions permissions() override { return {true,true,"Run at the same privilege level as Roblox"}; }
    void request_capture_permission() override {}
    void request_input_permission() override {}
    std::vector<Window> windows() override {
        std::vector<Window> result;
        EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
            auto window=describe(hwnd);
            if (!window.id || window.bounds.width<300 || window.bounds.height<200) return TRUE;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(window.process));
            if (!process) return TRUE;
            wchar_t path[32768]{}; DWORD size=32768;
            bool ok=QueryFullProcessImageNameW(process,0,path,&size); CloseHandle(process);
            if (ok) {
                std::wstring name(path); auto slash=name.find_last_of(L"\\/");
                if (slash!=std::wstring::npos) name=name.substr(slash+1);
                if (_wcsicmp(name.c_str(),L"RobloxPlayerBeta.exe")==0)
                    reinterpret_cast<std::vector<Window>*>(data)->push_back(std::move(window));
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&result));
        return result;
    }
    Snapshot inspect(std::uint64_t id) override {
        HWND hwnd=reinterpret_cast<HWND>(id);
        return {describe(hwnd),GetForegroundWindow()==hwnd,true,(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0,(GetAsyncKeyState(VK_F2)&0x8000)!=0};
    }
    Frame capture(std::uint64_t id) override {
        auto snapshot=inspect(id);
        if (!snapshot.window.id || !snapshot.focused) throw std::runtime_error("Keep Roblox visible and in the foreground for capture");
        const auto& r=snapshot.window.bounds;
        Frame frame{snapshot.window,monotonic_now(),static_cast<int>(r.width),static_cast<int>(r.height),{}};
        frame.rgba.resize(static_cast<size_t>(frame.width)*frame.height*4);
        HDC screen=GetDC(nullptr), memory=screen?CreateCompatibleDC(screen):nullptr;
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=frame.width;
        info.bmiHeader.biHeight=-frame.height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HBITMAP bitmap=memory?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
        HGDIOBJ previous=bitmap && memory ? SelectObject(memory,bitmap) : nullptr;
        if (previous==HGDI_ERROR) previous=nullptr;
        bool ok=previous && BitBlt(memory,0,0,frame.width,frame.height,screen,static_cast<int>(r.x),static_cast<int>(r.y),SRCCOPY | CAPTUREBLT) && GdiFlush();
        if (ok) {
            auto bytes=static_cast<std::uint8_t*>(pixels);
            for (size_t i=0; i<frame.rgba.size(); i+=4) {
                frame.rgba[i]=bytes[i+2]; frame.rgba[i+1]=bytes[i+1]; frame.rgba[i+2]=bytes[i]; frame.rgba[i+3]=255;
            }
        }
        if (previous) SelectObject(memory,previous);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr,screen);
        if (!ok) throw std::runtime_error("Window capture failed");
        if (describe(reinterpret_cast<HWND>(id)).bounds != r) throw std::runtime_error("Window geometry changed during capture");
        return frame;
    }
    std::vector<TextLine> recognize(const Frame& frame) override { return recognize(frame,{}); }
    std::vector<TextLine> recognize(const Frame& frame,const std::function<bool()>& cancelled) override try {
        if (!frame.valid()) throw std::invalid_argument("Invalid OCR frame");
        thread_local struct Apartment {
            Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;
        using namespace winrt::Windows;
        thread_local auto engine=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(L"en-US"));
        if (!engine) throw std::runtime_error("Install an English Windows OCR language");
        return recognize_tiled({frame.width,frame.height,frame.rgba},static_cast<int>(Media::Ocr::OcrEngine::MaxImageDimension()),[&](const OcrImage& image) {
            Storage::Streams::DataWriter writer;
            std::vector<std::uint8_t> bgra(image.rgba.begin(),image.rgba.end());
            for (size_t i=0;i<bgra.size();i+=4) std::swap(bgra[i],bgra[i+2]);
            writer.WriteBytes(bgra);
            auto bitmap=Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),Graphics::Imaging::BitmapPixelFormat::Bgra8,image.width,image.height,Graphics::Imaging::BitmapAlphaMode::Premultiplied);
            auto result=engine.RecognizeAsync(bitmap).get();
            std::vector<TextLine> lines;
            for (auto line : result.Lines()) {
                double left=image.width, top=image.height, right=0, bottom=0;
                std::vector<TextLine> words;
                for (auto word : line.Words()) {
                    const auto r=word.BoundingRect();
                    left=std::min(left,double(r.X)); top=std::min(top,double(r.Y)); right=std::max(right,double(r.X+r.Width)); bottom=std::max(bottom,double(r.Y+r.Height));
                    words.push_back({winrt::to_string(word.Text()),{r.X,r.Y,r.Width,r.Height},std::nullopt});
                }
                for (auto& column:split_ocr_columns({winrt::to_string(line.Text()),{left,top,right-left,bottom-top},std::nullopt},words))
                    lines.push_back(std::move(column));
            }
            bitmap.Close(); writer.Close();
            return lines;
        },cancelled);
    }
    catch (const winrt::hresult_error& error) { throw std::runtime_error(winrt::to_string(error.message())); }
    void key(Key key, bool down) override {
        if (key==Key::select_all) {
            INPUT chord[2]{};
            for (auto& input:chord) input.type=INPUT_KEYBOARD;
            chord[0].ki.wVk=down?VK_CONTROL:'A'; chord[1].ki.wVk=down?'A':VK_CONTROL;
            for (auto& input:chord) input.ki.dwFlags=down?0:KEYEVENTF_KEYUP;
            if (SendInput(2,chord,sizeof(INPUT))!=2) throw std::runtime_error("Number selection was blocked by Windows");
            return;
        }
        INPUT input{};
        if (key==Key::primary) { input.type=INPUT_MOUSE; input.mi.dwFlags=down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; }
        else {
            const auto scan=MapVirtualKeyW(code(key),MAPVK_VK_TO_VSC_EX);
            if (!scan || ((scan>>8)!=0 && (scan>>8)!=0xe0)) throw std::runtime_error("Unsupported keyboard scan code");
            input.type=INPUT_KEYBOARD; input.ki.wScan=static_cast<WORD>(scan&0xff);
            input.ki.dwFlags=KEYEVENTF_SCANCODE | (down?0:KEYEVENTF_KEYUP) | ((scan>>8)==0xe0?KEYEVENTF_EXTENDEDKEY:0);
        }
        if (SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("Input was blocked by Windows");
    }
    void pointer(Point point) override {
        if (!SetCursorPos(static_cast<int>(point.x),static_cast<int>(point.y))) throw std::runtime_error("Pointer movement failed");
    }
    void scroll(int lines) override {
        if (lines<-8 || lines>8) throw std::invalid_argument("Invalid scroll distance");
        INPUT input{}; input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_WHEEL;
        input.mi.mouseData=static_cast<DWORD>(lines*WHEEL_DELTA);
        if (SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("Scrolling was blocked by Windows");
    }
};
}
std::unique_ptr<Desktop> make_desktop() { return std::make_unique<WindowsDesktop>(); }
}
