#include "update_release.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <stdexcept>
#include <algorithm>

namespace hive::app::updates {
namespace {
struct Version {
    QVersionNumber number;
    QString suffix;
};
Version version(QString value) {
    if (value.startsWith('v')) value.remove(0,1);
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$");
    const auto match=pattern.match(value);
    if (value.size()>120 || !match.hasMatch()) throw std::runtime_error("The release version is not supported.");
    const auto suffix=match.captured(4);
    for (const auto& part:suffix.split('.'))
        if (part.size()>1 && part.front()=='0' && std::all_of(part.begin(),part.end(),[](QChar c) { return c.isDigit(); }))
            throw std::runtime_error("The release version is not supported.");
    return {QVersionNumber::fromString(value),suffix};
}
int compare(const Version& a,const Version& b) {
    if (const auto order=QVersionNumber::compare(a.number,b.number)) return order;
    if (a.suffix.isEmpty() || b.suffix.isEmpty()) return int(a.suffix.isEmpty())-int(b.suffix.isEmpty());
    const auto left=a.suffix.split('.'),right=b.suffix.split('.');
    for (qsizetype i=0;i<std::min(left.size(),right.size());++i) {
        const auto& x=left[i]; const auto& y=right[i];
        const auto numeric=[](const QString& s) { return std::all_of(s.begin(),s.end(),[](QChar c) { return c.isDigit(); }); };
        const bool nx=numeric(x),ny=numeric(y);
        if (nx!=ny) return nx?-1:1;
        if (nx && x.size()!=y.size()) return x.size()<y.size()?-1:1;
        if (const auto order=QString::compare(x,y,Qt::CaseSensitive)) return order;
    }
    return left.size()==right.size()?0:left.size()<right.size()?-1:1;
}
}
QString platform() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return "Windows-x64";
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    return "macOS-arm64";
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_X86_64)
    return "macOS-x64";
#else
    return {};
#endif
}
bool download_url(const QUrl& url) {
    return url.isValid() && url.scheme()=="https" && url.userInfo().isEmpty() && url.port(-1)==-1
        && (url.host()=="github.com" || url.host()=="release-assets.githubusercontent.com"
            || url.host()=="objects.githubusercontent.com");
}
std::optional<Release> newer_release(const QByteArray& json, const QString& current, const QString& system) {
    const auto doc=QJsonDocument::fromJson(json);
    if (!doc.isObject() && !doc.isArray()) throw std::runtime_error("GitHub returned an unreadable release.");
    const auto installed=version(current);
    auto newest=installed;
    QJsonObject root;
    const auto releases=doc.isArray()?doc.array():QJsonArray{doc.object()};
    for (const auto& candidate:releases) {
        const auto release=candidate.toObject();
        if (release.value("draft").toBool()) continue;
        Version available;
        try { available=version(release.value("tag_name").toString()); }
        catch (const std::exception&) { if (doc.isObject()) throw; else continue; }
        if (compare(available,newest)>0) { root=release; newest=available; }
    }
    if (root.isEmpty()) return {};
    if (system.isEmpty()) throw std::runtime_error("Updates are not available for this platform yet.");
    const auto tag=root.value("tag_name").toString();
    const auto number=tag.startsWith('v')?tag.mid(1):tag;
    const bool prerelease=root.value("prerelease").toBool() || !newest.suffix.isEmpty();
    const QStringList names=system=="Windows-x64"?QStringList{"Aurex.exe","Aurex-"+number+"-Windows-x64.exe"}
        :QStringList{"Aurex-"+system+".zip","Aurex-"+number+"-"+system+".zip"};
    std::optional<Release> result;
    for (const auto& value:root.value("assets").toArray()) {
        const auto asset=value.toObject();
        const auto name=asset.value("name").toString();
        if (!names.contains(name)) continue;
        const QUrl url(asset.value("browser_download_url").toString());
        const auto digest=asset.value("digest").toString();
        const auto size=asset.value("size").toInteger();
        if (result || !download_url(url) || url.host()!="github.com" || url.hasQuery() || url.hasFragment()
            || url.path()!=QString("/fenixJK/Aurex/releases/download/%1/%2").arg(tag,name)
            || asset.value("state").toString()!="uploaded" || size<=0 || size>256*1024*1024
            || !QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch())
            throw std::runtime_error("The release download could not be verified. Please use the GitHub release page.");
        result=Release{number,name,url,QByteArray::fromHex(digest.mid(7).toLatin1()),size,prerelease};
    }
    if (!result) throw std::runtime_error("The new release does not have a download for this computer yet.");
    return result;
}
void verify_download(const QString& path,const Release& release) {
    QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size()!=release.size || !hash.addData(&file) || hash.result()!=release.sha256)
        throw std::runtime_error("The update download is incomplete or damaged. Your app has not been changed.");
}
}
