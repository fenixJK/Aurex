#include "visit_cooldown_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <charconv>

namespace hive::features::visits {
namespace {
WallTime timestamp(const QJsonValue& value) {
    if (!value.isString()) throw std::runtime_error("Invalid saved cooldown timestamp");
    const auto bytes=value.toString().toLatin1(); Time::rep number{};
    const auto result=std::from_chars(bytes.constData(),bytes.constData()+bytes.size(),number);
    if (bytes.isEmpty() || bytes.size()>20 || result.ec!=std::errc{} || result.ptr!=bytes.constData()+bytes.size() || number<0)
        throw std::runtime_error("Invalid saved cooldown timestamp");
    return WallTime(Time(number));
}
class FileStore final : public CooldownStore {
    QString path_;
    mutable std::unique_ptr<QLockFile> lock_;
    void lock() const {
        if (lock_) return;
        if (!QDir().mkpath(QFileInfo(path_).absolutePath())) throw std::runtime_error("Cannot open saved cooldown folder");
        auto guard=std::make_unique<QLockFile>(QFileInfo(path_).absoluteFilePath()+".lock"); guard->setStaleLockTime(0);
        if (!guard->tryLock(0)) throw std::runtime_error("Saved cooldowns are already in use or cannot be locked");
        lock_=std::move(guard);
    }
public:
    explicit FileStore(const std::filesystem::path& path) {
#ifdef _WIN32
        path_=QString::fromStdWString(path.native());
#else
        path_=QString::fromStdString(path.native());
#endif
        if (path_.isEmpty()) throw std::invalid_argument("Missing saved cooldown path");
    }
    CooldownRecords load() const override {
        lock(); const QFileInfo info(path_);
        if (info.isSymLink()) throw std::runtime_error("Saved cooldowns cannot be a symbolic link");
        if (!info.exists()) return {};
        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly) || file.size()>65536) throw std::runtime_error("Cannot read saved cooldowns");
        const auto bytes=file.read(65537);
        if (file.error()!=QFileDevice::NoError || bytes.size()>65536) throw std::runtime_error("Cannot read complete cooldown state");
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error);
        if (error.error!=QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid saved cooldown document");
        const auto root=doc.object();
        if (root.size()!=2 || root["version"]!=QJsonValue(1) || !root["cooldowns"].isArray() || root["cooldowns"].toArray().size()>128)
            throw std::runtime_error("Unsupported saved cooldown schema");
        CooldownRecords records;
        for (const auto& row : root["cooldowns"].toArray()) {
            if (!row.isObject()) throw std::runtime_error("Invalid saved cooldown row");
            const auto value=row.toObject();
            if (value.size()!=4 || !value["id"].isString() || !value["estimated"].isBool()) throw std::runtime_error("Invalid saved cooldown fields");
            if (!records.emplace(value["id"].toString().toStdString(),Cooldown{timestamp(value["started"]),timestamp(value["ready"]),value["estimated"].toBool()}).second)
                throw std::runtime_error("Duplicate saved cooldown identity");
        }
        validate(records); return records;
    }
    void save(const CooldownRecords& records) override {
        validate(records); lock();
        QJsonArray values;
        for (const auto& [id,value] : records) values.append(QJsonObject{{"id",QString::fromStdString(id)},
            {"started",QString::number(value.started.time_since_epoch().count())},{"ready",QString::number(value.ready.time_since_epoch().count())},
            {"estimated",value.estimated}});
        const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"cooldowns",values}}).toJson(QJsonDocument::Compact);
        if (bytes.size()>65536 || QFileInfo(path_).isSymLink()) throw std::runtime_error("Cannot prepare saved cooldown file");
        QSaveFile file(path_); file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
            throw std::runtime_error("Cannot save cooldowns: "+file.errorString().toStdString());
    }
};
}
std::shared_ptr<CooldownStore> cooldown_file_store(std::filesystem::path file) { return std::make_shared<FileStore>(file); }
}
