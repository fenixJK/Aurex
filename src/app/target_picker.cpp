#include "target_picker.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
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
    if (dialog.exec()!=QDialog::Accepted) return {};
    return result;
}
}
