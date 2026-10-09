#pragma once
#include "core/runtime/model.hpp"
#include <memory>
#include <functional>

namespace hive {
struct Permissions { bool capture{}, input{}; std::string detail; };
class Desktop : public Input {
public:
    virtual bool client_lifecycle_available() const { return false; }
    virtual std::vector<std::int64_t> client_processes() {
        std::vector<std::int64_t> result;
        for (const auto& window:windows()) result.push_back(window.process);
        return result;
    }
    virtual bool client_running(std::int64_t) { return false; }
    virtual bool close_client(const Window&) { return false; }
    virtual bool open_client(const std::string&) { return false; }
    virtual bool focus_client(const Window&) { return false; }
    virtual bool focus_client_process(std::int64_t pid) {
        for (const auto& window : windows()) if (window.process==pid) return focus_client(window);
        return false;
    }
    virtual Permissions permissions() = 0;
    virtual void request_capture_permission() = 0;
    virtual void request_input_permission() = 0;
    virtual std::vector<Window> windows() = 0;
    virtual Snapshot inspect(std::uint64_t id) = 0;
    virtual Frame capture(std::uint64_t id) = 0;
    virtual std::vector<TextLine> recognize(const Frame& frame) = 0;
    virtual std::vector<TextLine> recognize(const Frame& frame,const std::function<bool()>& cancelled) {
        if (cancelled && cancelled()) throw std::runtime_error("OCR cancelled");
        auto result=recognize(frame);
        if (cancelled && cancelled()) throw std::runtime_error("OCR cancelled");
        return result;
    }
};
std::unique_ptr<Desktop> make_desktop();
}
