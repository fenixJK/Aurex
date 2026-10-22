#include "update_release.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <stdexcept>

namespace hive::app::updates {
namespace {
QVersionNumber version(QString value) {
    if (value.startsWith('v')) value.remove(0,1);
    if (!QRegularExpression("^[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}$").match(value).hasMatch())
        throw std::runtime_error("The release version is not supported.");
    return QVersionNumber::fromString(value);
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
    if (!doc.isObject()) throw std::runtime_error("GitHub returned an unreadable release.");
    const auto root=doc.object();
    if (root.value("draft").toBool() || root.value("prerelease").toBool()) return {};
    const auto tag=root.value("tag_name").toString();
    const auto available=version(tag);
    if (available<=version(current)) return {};
    if (system.isEmpty()) throw std::runtime_error("Updates are not available for this platform yet.");
    const auto number=available.toString();
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
        result=Release{number,name,url,QByteArray::fromHex(digest.mid(7).toLatin1()),size};
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
