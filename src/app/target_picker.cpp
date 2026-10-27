#include "target_picker.hpp"
#include "platform/platform.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

namespace hive::app {
namespace {
class Preview final : public QLabel {
    std::function<void(Point)> selected_;
public:
    Preview(const QPixmap& image,std::function<void(Point)> selected):selected_(std::move(selected)) {
        setPixmap(image); setFixedSize(image.size()); setCursor(Qt::CrossCursor);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        const auto p=event->position();
        if (event->button()==Qt::LeftButton && rect().contains(p.toPoint())) selected_({p.x()/width(),p.y()/height()});
    }
};
}
std::optional<WindowPoint> pick_window_point(const Frame& frame,const QString& prompt,QWidget* parent) {
    if (!frame.valid()) throw std::runtime_error("Could not capture the game for target selection");
    QDialog dialog(parent); dialog.setWindowTitle("Select target");
    auto layout=new QVBoxLayout(&dialog); layout->addWidget(new QLabel(prompt));
    const auto screen=dialog.screen()->availableGeometry().size();
    const auto bounds=QSize(std::min(1000,screen.width()-80),std::min(700,screen.height()-140));
    QImage image(frame.rgba.data(),frame.width,frame.height,frame.width*4,QImage::Format_RGBA8888);
    std::optional<WindowPoint> result;
    layout->addWidget(new Preview(QPixmap::fromImage(image).scaled(bounds,Qt::KeepAspectRatio,Qt::SmoothTransformation),[&](Point point) {
        result=WindowPoint{frame.window,point}; dialog.accept();
    }));
    auto cancel=new QDialogButtonBox(QDialogButtonBox::Cancel); layout->addWidget(cancel);
    QObject::connect(cancel,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    QTimer::singleShot(0,&dialog,[&dialog] { dialog.raise(); dialog.activateWindow(); });
    if (dialog.exec()!=QDialog::Accepted) return {};
    return result;
}
std::optional<WindowPoint> pick_window_point(Desktop& desktop,const Window& window,const QString& prompt,QWidget* parent) {
    if (!window.id) throw std::runtime_error("Open Roblox with the target visible first");
    if (!desktop.permissions().capture) throw std::runtime_error("Screen capture permission is required");
    // The tool button belongs to Aurex. Windows captures the visible screen, so
    // focus Roblox and let it repaint before taking the selection preview.
    QDialog preparing(parent); preparing.setWindowTitle("Select target");
    auto layout=new QVBoxLayout(&preparing);
    layout->addWidget(new QLabel("Bringing Roblox to the front for bee selection…"));
    auto cancel=new QDialogButtonBox(QDialogButtonBox::Cancel); layout->addWidget(cancel);
    QObject::connect(cancel,&QDialogButtonBox::rejected,&preparing,&QDialog::reject);
    QElapsedTimer elapsed; elapsed.start();
    qint64 focused_at=-1,next_focus=0;
    std::optional<Frame> captured;
    std::exception_ptr failure;
    QTimer timer; timer.setInterval(50);
    QObject::connect(&timer,&QTimer::timeout,&preparing,[&] {
        try {
            const auto now=elapsed.elapsed();
            const auto snapshot=desktop.inspect(window.id);
            if (snapshot.escape_down) { preparing.reject(); return; }
            if (snapshot.window.id!=window.id || snapshot.window.process!=window.process)
                throw std::runtime_error("Roblox closed or changed windows; open your hive and try again");
            if (now>=3000) throw std::runtime_error("Could not bring Roblox to the front. Open your hive in Roblox, then try selecting the bee again");
            if (!snapshot.focused) {
                focused_at=-1;
                if (now>=next_focus) { desktop.focus_client(snapshot.window); next_focus=now+250; }
                return;
            }
            if (focused_at<0) focused_at=now;
            if (now-focused_at<100) return;
            auto frame=desktop.capture(window.id);
            if (!frame.valid() || frame.window.id!=window.id || frame.window.process!=window.process)
                throw std::runtime_error("Could not capture Roblox for bee selection. Keep the game visible and try again");
            captured=std::move(frame); preparing.accept();
        } catch (...) { failure=std::current_exception(); preparing.reject(); }
    });
    timer.start();
    const auto result=preparing.exec(); timer.stop();
    if (failure) std::rethrow_exception(failure);
    if (result!=QDialog::Accepted || !captured) return {};
    return pick_window_point(*captured,prompt,parent);
}
}
