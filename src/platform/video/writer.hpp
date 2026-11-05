#pragma once
#include <QImage>
#include <QString>
#include <memory>

namespace hive::video {
// A writer belongs to its encoding worker. Frames are fixed-size, with elapsed
// presentation times, so slow capture never speeds up the resulting clip.
class Writer {
public:
    virtual ~Writer()=default;
    virtual void append(const QImage& frame,qint64 milliseconds)=0;
    virtual void finish()=0;
};
std::unique_ptr<Writer> writer(const QString& file,QSize size,int fps,int bitrate);
}
