#include "local_data.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>

namespace hive::app {
namespace {
constexpr qint64 maximum_size=1024*1024;
bool read_json(QIODevice& file,QSettings::SettingsMap& values) {
    const auto bytes=file.read(maximum_size+1);
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(bytes,&error);
    if (bytes.size()>maximum_size || error.error!=QJsonParseError::NoError || !document.isObject()) return false;
    const auto root=document.object();
    if (root["version"]!=QJsonValue(1) || !root["values"].isObject()) return false;
    values=root["values"].toObject().toVariantMap();
    return true;
}
bool write_json(QIODevice& file,const QSettings::SettingsMap& values) {
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"values",QJsonObject::fromVariantMap(values)}}).toJson();
    return bytes.size()<=maximum_size && file.write(bytes)==bytes.size();
}
void save(const QString& path,const QSettings::SettingsMap& values) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !write_json(file,values) || !file.commit())
        throw std::runtime_error(("Could not save settings: "+path).toStdString());
}
void check_local_data(const QString& directory) {
    const auto path=QDir(directory).filePath("settings.json");
    if (!QFileInfo::exists(path)) save(path,{});
    QFile file(path); QSettings::SettingsMap stored;
    if (!file.open(QIODevice::ReadOnly) || !read_json(file,stored))
        throw std::runtime_error(("Could not read settings: "+path+". The file has been left unchanged.").toStdString());
    QSettings settings(path,json_settings_format());
    if (!settings.isWritable()) throw std::runtime_error(("Move Aurex and AurexData to a writable folder. Cannot save to: "+directory).toStdString());
}
}
QSettings::Format json_settings_format() {
    static const auto format=QSettings::registerFormat("json",read_json,write_json,Qt::CaseSensitive);
    return format;
}
QString portable_data_path(const QString& executable_directory) {
    QDir folder(executable_directory);
#ifdef Q_OS_MACOS
    if (folder.dirName()=="MacOS") {
        auto bundle=folder;
        if (bundle.cdUp() && bundle.dirName()=="Contents" && bundle.cdUp() && bundle.dirName().endsWith(".app")) {
            bundle.cdUp(); folder=bundle;
        }
    }
#endif
    return folder.absoluteFilePath("AurexData");
}
void prepare_local_data(const QString& directory) {
    if (!QDir().mkpath(directory)) throw std::runtime_error(("Move Aurex to a writable folder. Cannot create: "+directory).toStdString());
    check_local_data(directory);
}
QString prepare_local_data() {
    const auto directory=portable_data_path(QCoreApplication::applicationDirPath());
    prepare_local_data(directory);
    return directory;
}
}
