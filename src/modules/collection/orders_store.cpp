#include "orders.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>

namespace hive::features::collection {
namespace {
qint64 integer(const QJsonValue& value,qint64 maximum) {
    const auto n=value.toDouble(-1);
    if (n<0 || n>maximum || n!=std::floor(n)) throw std::runtime_error("Invalid saved item quantity or time");
    return static_cast<qint64>(n);
}
QJsonObject encode(const ItemOrder& order) {
    return {{"item",QString::fromStdString(order.item)},{"amount",int(order.amount)},{"runs",int(order.runs)},{"unlimited",order.unlimited}};
}
ItemOrder decode(const QJsonObject& value) {
    if (!value["item"].isString() || !value["unlimited"].isBool()) throw std::runtime_error("Invalid saved item order");
    return {value["item"].toString().toStdString(),unsigned(integer(value["amount"],999)),unsigned(integer(value["runs"],999)),value["unlimited"].toBool()};
}
QJsonObject encode(const ItemQueue& queue) {
    QJsonArray orders;
    for (const auto& p:queue.orders) { auto item=encode(p.order); item["completed"]=int(p.completed); item["blocked"]=qint64(p.blocked_until.time_since_epoch().count()); orders.append(item); }
    return {{"orders",orders},{"last",QString::fromStdString(queue.last)},{"ready",qint64(queue.ready.time_since_epoch().count())},
        {"crafting",queue.crafting?QJsonValue(encode(*queue.crafting)):QJsonValue(QJsonValue::Null)}};
}
ItemQueue decode_queue(const QJsonValue& value) {
    if (!value.isObject()) throw std::runtime_error("Missing saved item queue");
    const auto object=value.toObject();
    if (!object["orders"].isArray() || object["orders"].toArray().size()>64 || !object["last"].isString()) throw std::runtime_error("Invalid saved item queue");
    ItemQueue q; q.last=object["last"].toString().toStdString(); q.ready=game::WallTime(Time(integer(object["ready"],9000000000000LL)));
    if (!object["crafting"].isNull()) q.crafting=decode(object["crafting"].toObject());
    for (const auto value:object["orders"].toArray()) {
        const auto entry=value.toObject();
        q.orders.push_back({decode(entry),unsigned(integer(entry["completed"],1000000000)),game::WallTime(Time(integer(entry["blocked"],9000000000000LL)))});
    }
    return q;
}
class FileStore final : public ItemOrderStore {
    QString path_;
public:
    explicit FileStore(const std::filesystem::path& path) {
        path_=QFileInfo(path).filePath();
        if (path_.isEmpty()) throw std::invalid_argument("Missing item queue path");
    }
    ItemOrderState load() const override {
        if (!QFileInfo::exists(path_)) return {};
        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly) || file.size()>65536) throw std::runtime_error("Cannot read item queue progress");
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        if (error.error!=QJsonParseError::NoError || !doc.isObject() || doc.object()["version"].toInt()!=1) throw std::runtime_error("Invalid item queue progress");
        ItemOrderState state{decode_queue(doc.object()["blender"]),decode_queue(doc.object()["shrine"])}; validate(state); return state;
    }
    void save(const ItemOrderState& state) override {
        validate(state);
        if (!QDir().mkpath(QFileInfo(path_).absolutePath())) throw std::runtime_error("Cannot create item queue folder");
        const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"blender",encode(state.blender)},{"shrine",encode(state.shrine)}}).toJson(QJsonDocument::Compact);
        QSaveFile file(path_);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) throw std::runtime_error("Cannot save item queue progress");
    }
};
}
std::shared_ptr<ItemOrderStore> item_order_file_store(const std::filesystem::path& path) { return std::make_shared<FileStore>(path); }
}
