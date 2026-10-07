#pragma once
#include "hive/game/detection/perception.hpp"
#include <QImage>
#include <QPainter>
#include <memory>

namespace hive::app {
inline QImage game_image(std::shared_ptr<const Frame> frame) {
    if (!frame || !frame->valid()) return {};
    return QImage(frame->rgba.data(),frame->width,frame->height,frame->width*4,QImage::Format_RGBA8888,
        [](void* data) { delete static_cast<std::shared_ptr<const Frame>*>(data); },new std::shared_ptr<const Frame>(frame));
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
}
