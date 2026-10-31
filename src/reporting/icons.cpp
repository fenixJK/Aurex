#include "report.hpp"
#include <QDirIterator>
#include <QResource>

static void initialize_report_icons() { Q_INIT_RESOURCE(report_icons); }

namespace hive::reporting {
QImage report_icon(const QString& key) {
    static const QMap<QString,QImage> icons=[] {
        initialize_report_icons();
        QMap<QString,QImage> result;
        const QString prefix=":/report-icons/";
        QDirIterator files(prefix,{"*.png"},QDir::Files,QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path=files.next();
            result.insert(path.mid(prefix.size()).chopped(4),QImage(path));
        }
        return result;
    }();
    return icons.value(key);
}
}
