#include "store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <charconv>
#include <algorithm>

namespace hive::features::planters {
namespace {
void keys(const QJsonObject& object,std::initializer_list<const char*> expected) {
    if (object.size()!=static_cast<qsizetype>(expected.size())) throw std::invalid_argument("Unexpected planter state fields");
    for (const auto key : expected) if (!object.contains(key)) throw std::invalid_argument("Missing planter state field");
}
QJsonObject object(const QJsonValue& value) {
    if (!value.isObject()) throw std::invalid_argument("Expected planter state object"); return value.toObject();
}
QString text(const QJsonValue& value) {
    if (!value.isString() || value.toString().size()>160) throw std::invalid_argument("Invalid planter state text"); return value.toString();
}
template<class T> T integer(const QJsonValue& value) {
    const auto bytes=text(value).toLatin1(); T result{};
    const auto parsed=std::from_chars(bytes.constData(),bytes.constData()+bytes.size(),result);
    if (bytes.isEmpty() || parsed.ec!=std::errc{} || parsed.ptr!=bytes.constData()+bytes.size() || result<0)
        throw std::invalid_argument("Invalid planter state integer");
    return result;
}
double number(const QJsonValue& value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble())) throw std::invalid_argument("Invalid planter state number"); return value.toDouble();
}
QJsonObject encode(const Plant& p) {
    QJsonValue growth=QJsonValue::Null;
    if (p.growth) growth=QJsonObject{{"observed",QString::number(p.growth->observed.time_since_epoch().count())},{"fraction",p.growth->fraction},{"offline",QString::number(p.growth->offline.count())}};
    return {{"missing",p.missing},{"material",QString::fromStdString(p.material)},{"growth",growth},{"offline",QString::number(p.offline.count())},{"id",QString::number(p.id)},{"planter",QString::fromStdString(p.stats.planter)},{"field",QString::fromStdString(p.stats.field)},
        {"nectar",static_cast<int>(p.stats.nectar)},{"nectarBonus",p.stats.nectar_bonus},{"speedBonus",p.stats.speed_bonus},{"fullSeconds",p.stats.full_seconds},
        {"phase",static_cast<int>(p.phase)},{"placed",QString::number(p.placed.time_since_epoch().count())},{"due",QString::number(p.due.time_since_epoch().count())}};
}
int enumeration(const QJsonValue& value,int high) {
    const auto n=number(value); if (n<0 || n>high || n!=std::floor(n)) throw std::invalid_argument("Invalid planter state enum"); return static_cast<int>(n);
}
Plant decode(const QJsonValue& value,int version) {
    auto p=object(value);
    const auto missing=version>=9?p.take("missing"):QJsonValue(false);
    if (!missing.isBool()) throw std::invalid_argument("Invalid missing planter state");
    auto material=version>=6?text(p.take("material")).toStdString():std::string{};
    if(version<=7 && (material=="dapper-sticker" || material=="item:dapper-sticker" || material=="item:stickers")) material.clear();
    std::optional<GrowthSample> growth;
    const auto offline=version>=5?Time(integer<Time::rep>(p.take("offline"))):Time{};
    if (version>=2) {
        if (!p.contains("growth")) throw std::invalid_argument("Missing planter growth field");
        const auto saved=p.take("growth");
        if (!saved.isNull()) {
            auto g=object(saved); const auto inactive=version>=5?Time(integer<Time::rep>(g.take("offline"))):Time{};
            keys(g,{"observed","fraction"});
            growth=GrowthSample{WallTime(Time(integer<Time::rep>(g["observed"]))),number(g["fraction"]),inactive};
        }
    }
    keys(p,{"id","planter","field","nectar","nectarBonus","speedBonus","fullSeconds","phase","placed","due"});
    Plant result{integer<std::uint64_t>(p["id"]),{text(p["planter"]).toStdString(),text(p["field"]).toStdString(),static_cast<Nectar>(enumeration(p["nectar"],4)),
        number(p["nectarBonus"]),number(p["speedBonus"]),number(p["fullSeconds"])},static_cast<Phase>(enumeration(p["phase"],2)),
        WallTime(Time(integer<Time::rep>(p["placed"]))),WallTime(Time(integer<Time::rep>(p["due"]))),growth,offline,material,missing.toBool()};
    if (version==3) {
        const auto entries=catalog();
        const auto base=std::find_if(entries.begin(),entries.end(),[&](const auto& entry) { return identity(entry)==identity(result.stats); });
        if (base!=entries.end()) {
            const auto fraction=harvest_fraction(result);
            result.stats=*base;
            result.due=result.placed+Time(static_cast<Time::rep>(std::ceil(fraction*base->full_seconds))*1000);
        }
    }
    return result;
}
class FileStore final : public Store {
    QString path_;
    std::unique_ptr<QLockFile> lock_;
public:
    explicit FileStore(const std::filesystem::path& path) {
        path_=QFileInfo(path).filePath();
        if (path_.isEmpty()) throw std::invalid_argument("Missing planter state path");
        if (!QDir().mkpath(QFileInfo(path_).absolutePath())) throw std::runtime_error("Cannot open planter state folder");
        lock_=std::make_unique<QLockFile>(QFileInfo(path_).absoluteFilePath()+".lock");
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) throw std::runtime_error("Planter state is already in use or cannot be locked");
    }
    std::optional<Ledger> load() const override {
        QFileInfo info(path_);
        if (info.isSymLink()) throw std::runtime_error("Planter state cannot be a symbolic link");
        if (!info.exists()) return {};
        QFile file(path_);
        if (!file.open(QIODevice::ReadOnly) || file.size()>65536) throw std::runtime_error("Cannot read planter state");
        const auto bytes=file.read(65537);
        if (file.error()!=QFileDevice::NoError || bytes.size()>65536) throw std::runtime_error("Cannot read complete planter state");
        QJsonParseError error; const auto document=QJsonDocument::fromJson(bytes,&error);
        if (error.error!=QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid planter state document");
        const auto root=document.object();
        if (root["version"].toInt()==3) keys(root,{"version","nextTicket","plants","pending","calibration","travel"});
        else if (root["version"].toInt()>=6 && root["version"].toInt()<=9) keys(root,{"version","nextTicket","plants","pending","travel","fields","materials"});
        else if (root["version"].toInt()==5) keys(root,{"version","nextTicket","plants","pending","travel","fields"});
        else if (root["version"].toInt()==4) keys(root,{"version","nextTicket","plants","pending","travel"});
        else keys(root,{"version","nextTicket","plants","pending"});
        const auto version=number(root["version"]);
        if ((version!=1 && version!=2 && version!=3 && version!=4 && version!=5 && version!=6 && version!=7 && version!=8 && version!=9) || !root["plants"].isArray() || root["plants"].toArray().size()>3)
            throw std::runtime_error("Unsupported planter state version or count");
        Ledger result; result.next_ticket=integer<std::uint64_t>(root["nextTicket"]);
        for (const auto& value : root["plants"].toArray()) result.plants.push_back(decode(value,static_cast<int>(version)));
        if (!root["pending"].isNull()) {
            const auto pending=object(root["pending"]); keys(pending,{"ticket","operation","plant"});
            result.pending=Intent{integer<std::uint64_t>(pending["ticket"]),static_cast<Operation>(enumeration(pending["operation"],1)),decode(pending["plant"],static_cast<int>(version))};
        }
        if (version>=3) {
            const auto travel=object(root["travel"]);
            for (auto it=travel.begin();it!=travel.end();++it) {
                if (!it.value().isArray()) throw std::invalid_argument("Invalid travel samples");
                auto& samples=result.travel[it.key().toStdString()]; for(const auto value:it.value().toArray()) samples.push_back(number(value));
            }
        }
        if (version>=5) {
            const auto fields=object(root["fields"]);
            for (auto it=fields.begin();it!=fields.end();++it) result.fields[it.key().toStdString()]=WallTime(Time(integer<Time::rep>(it.value())));
        }
        if (version>=6) {
            const auto materials=object(root["materials"]);
            for (auto it=materials.begin();it!=materials.end();++it) {
                const auto value=object(it.value()); if(version==7) keys(value,{"step","ready","complete"}); else keys(value,{"step","ready"});
                if(version==7 && !value["complete"].isBool()) throw std::invalid_argument("Invalid material completion");
                const auto id=it.key().toStdString();
                if(version<=7 && (id=="dapper-sticker" || id=="item:dapper-sticker" || id=="item:stickers")) continue;
                result.materials[id]={integer<unsigned>(value["step"]),Time(integer<Time::rep>(value["ready"]))};
            }
        }
        validate(result); return result;
    }
    void save(const Ledger& ledger) override {
        validate(ledger);
        QJsonArray plants; for (const auto& plant : ledger.plants) plants.append(encode(plant));
        QJsonValue pending=QJsonValue::Null;
        if (ledger.pending) pending=QJsonObject{{"ticket",QString::number(ledger.pending->ticket)},
            {"operation",static_cast<int>(ledger.pending->operation)},{"plant",encode(ledger.pending->plant)}};
        QJsonObject travel;
        for (const auto& [field,samples]:ledger.travel) { QJsonArray values; for(double sample:samples) values.append(sample); travel[QString::fromStdString(field)]=values; }
        QJsonObject fields;
        for (const auto& [field,used]:ledger.fields) fields[QString::fromStdString(field)]=QString::number(used.time_since_epoch().count());
        QJsonObject materials;
        for (const auto& [id,progress]:ledger.materials) materials[QString::fromStdString(id)]=QJsonObject{{"step",QString::number(progress.step)},{"ready",QString::number(progress.ready.count())}};
        const auto bytes=QJsonDocument(QJsonObject{{"version",9},{"nextTicket",QString::number(ledger.next_ticket)},
            {"plants",plants},{"pending",pending},{"travel",travel},{"fields",fields},{"materials",materials}}).toJson(QJsonDocument::Compact);
        if (bytes.size()>65536 || QFileInfo(path_).isSymLink() || !QDir().mkpath(QFileInfo(path_).absolutePath()))
            throw std::runtime_error("Cannot prepare planter state file");
        QSaveFile file(path_); file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) throw std::runtime_error("Cannot save planter state atomically");
    }
};
}
std::shared_ptr<Store> file_store(std::filesystem::path file) { return std::make_shared<FileStore>(file); }
}
