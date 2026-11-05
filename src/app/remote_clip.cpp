#include "remote_clip.hpp"
#include "platform/platform.hpp"
#include "platform/video/writer.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QPointer>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPainter>
#include <atomic>
#include <mutex>
#include <thread>

namespace hive::app {
struct ClipRecorder::Recording {
    std::atomic<bool> cancelled{},finished{};
    std::mutex mutex;
    std::shared_ptr<const Frame> frame;
    QImage screen_image;
    Time screen_at{};
    QPointer<QScreen> screen; // Only touched on the GUI thread.
    bool desktop{};
};
ClipRecorder::ClipRecorder(std::function<std::shared_ptr<const Frame>()> latest,QObject* parent)
    :QObject(parent),latest_(std::move(latest)) {
    timer_.setInterval(100);
    connect(&timer_,&QTimer::timeout,this,[this] {
        const auto state=active_.lock();
        if (!state || state->cancelled || state->finished) { timer_.stop(); return; }
        if (state->desktop) {
            if (!state->screen) { state->cancelled=true; return; }
            auto image=state->screen->grabWindow(0).toImage();
            std::lock_guard lock(state->mutex);
            state->screen_image=std::move(image); state->screen_at=monotonic_now();
        } else {
            auto frame=latest_();
            std::lock_guard lock(state->mutex); state->frame=std::move(frame);
        }
    });
}
ClipRecorder::~ClipRecorder() { cancel(); }
void ClipRecorder::cancel() { if (auto state=active_.lock()) state->cancelled=true; timer_.stop(); }
reporting::RemoteReply ClipRecorder::record(Window window,int seconds,int screen) {
    if (seconds<3 || seconds>30) throw std::invalid_argument("Choose a clip length from 3 to 30 seconds");
    if (const auto active=active_.lock();active && !active->finished) throw std::runtime_error("A clip is already being recorded");
    const auto screens=QGuiApplication::screens();
    if (screen<0 || screen>screens.size()) throw std::invalid_argument("Choose an available screen number");
    if (!screen && !window.id) throw std::runtime_error("Open Roblox before recording a game clip");
    auto state=std::make_shared<Recording>(); state->desktop=screen!=0;
    if (screen) state->screen=screens.at(screen-1);
    else state->frame=latest_();
    active_=state; timer_.start();
    const auto title=screen?QString("Screen %1 clip").arg(screen):QString("Roblox clip");
    reporting::RemoteReply reply{title};
    reply.deferred=[state,window,seconds,title] {
        struct Done { std::shared_ptr<Recording> state; ~Done() { state->finished=true; } } done{state};
        QTemporaryDir temporary;
        if (!temporary.isValid()) throw std::runtime_error("Could not create the temporary clip file");
        const auto file=temporary.filePath("clip.mp4");
        std::unique_ptr<Desktop> capture;
        std::unique_ptr<video::Writer> writer;
        QSize output;
        QElapsedTimer elapsed; elapsed.start();
        qint64 started=-1,last=-1;
        while (started<0 || elapsed.elapsed()-started<seconds*1000) {
            if (state->cancelled) return reporting::RemoteReply{title,"Recording cancelled."};
            QImage image;
            const auto now=monotonic_now();
            if (state->desktop) {
                std::lock_guard lock(state->mutex);
                if (state->screen_at>Time{} && now>=state->screen_at && now-state->screen_at<Time(1500)) image=state->screen_image;
            } else {
                std::shared_ptr<const Frame> frame;
                { std::lock_guard lock(state->mutex); frame=state->frame; }
                if (!frame || !frame->valid() || frame->window.id!=window.id || frame->window.process!=window.process
                    || now<frame->captured || now-frame->captured>Time(250) || frame->region!=Rect{0,0,1,1}) {
                    if (!capture) capture=make_desktop();
                    // Stay on the requested Roblox window. Never fall back to a desktop capture.
                    const auto current=capture->inspect(window.id).window;
                    if (current.id!=window.id || current.process!=window.process) throw std::runtime_error("The recorded Roblox window closed");
                    frame=std::make_shared<Frame>(capture->capture(window.id));
                }
                if (frame && frame->valid()) image=QImage(frame->rgba.data(),frame->width,frame->height,frame->width*4,QImage::Format_RGBA8888).copy();
            }
            if (image.isNull()) {
                if (elapsed.elapsed()>2000) throw std::runtime_error("No fresh capture is available; check screen recording permission");
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue;
            }
            if (!writer) {
                output=image.size();
                if (output.width()>1280 || output.height()>720) output.scale(1280,720,Qt::KeepAspectRatio);
                output.setWidth(std::max(2,output.width()/2*2)); output.setHeight(std::max(2,output.height()/2*2));
                writer=video::writer(file,output,10,1400000); started=elapsed.elapsed();
            }
            QImage canvas(output,QImage::Format_RGB32); canvas.fill(Qt::black);
            const auto scaled=image.scaled(output,Qt::KeepAspectRatio,Qt::FastTransformation);
            { QPainter painter(&canvas); painter.drawImage((output.width()-scaled.width())/2,(output.height()-scaled.height())/2,scaled); }
            const auto at=std::max(last+1,elapsed.elapsed()-started);
            writer->append(canvas,at); last=at;
            const auto delay=std::max<qint64>(1,100-(elapsed.elapsed()-started-at));
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        if (state->cancelled) return reporting::RemoteReply{title,"Recording cancelled."};
        writer->finish(); writer.reset();
        if (state->cancelled) return reporting::RemoteReply{title,"Recording cancelled."};
        QFile saved(file);
        if (!saved.open(QIODevice::ReadOnly) || saved.size()<32 || saved.size()>8*1024*1024)
            throw std::runtime_error("The encoded clip exceeds the upload limit; try a shorter clip");
        reporting::RemoteReply result{title,QString("%1 seconds · %2×%3 · silent").arg(seconds).arg(output.width()).arg(output.height())};
        result.attachment=reporting::MessageFile{"aurex-clip.mp4","video/mp4",title,saved.readAll()};
        return result;
    };
    return reply;
}
}
