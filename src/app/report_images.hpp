#pragma once
#include "game/detection/perception.hpp"
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <memory>

namespace hive::app {
inline QImage game_image(std::shared_ptr<const Frame> frame,std::optional<Rect> region={}) {
    if (!frame || !frame->valid()) return {};
    QImage image(frame->rgba.data(),frame->width,frame->height,frame->width*4,QImage::Format_RGBA8888,
        [](void* data) { delete static_cast<std::shared_ptr<const Frame>*>(data); },new std::shared_ptr<const Frame>(frame));
    if (!region) return image;
    if (!region->valid() || region->x<0 || region->y<0 || region->x+region->width>1 || region->y+region->height>1) return {};
    const int x=std::lround(region->x*image.width()),y=std::lround(region->y*image.height());
    const int right=std::lround((region->x+region->width)*image.width()),bottom=std::lround((region->y+region->height)*image.height());
    if (right<=x || bottom<=y) return {};
    return image.copy(x,y,right-x,bottom-y);
}
inline QImage resource_image(std::shared_ptr<const Frame> frame) {
    if (!frame) return {};
    const auto regions=game::resource_hud_regions(*frame); if (!regions) return {};
    const auto& first=regions->front();
    const int width=std::lround(first.width),height=std::lround(first.height);
    QImage result(width,height*2+2,QImage::Format_RGB32); result.fill(QColor("#152126"));
    QPainter painter(&result); const auto source=game_image(std::move(frame));
    for (int i=0;i<2;++i) {
        const auto& region=(*regions)[i];
        painter.drawImage(QRect(0,i*(height+2),width,height),source,QRectF(region.x,region.y,region.width,region.height));
    }
    return result;
}
inline bool save_failure_frame(const QString& path,std::shared_ptr<const Frame> frame,const QString& message) {
    auto image=game_image(frame);
    if (image.isNull()) return false;
    image.setText("Failure",message);
    const auto& bounds=frame->window.bounds;
    image.setText("Window bounds",QString("%1,%2 %3x%4").arg(bounds.x).arg(bounds.y).arg(bounds.width).arg(bounds.height));
    image.setText("Frame age ms",QString::number((monotonic_now()-frame->captured).count()));
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file,"PNG") && file.commit();
}
}
