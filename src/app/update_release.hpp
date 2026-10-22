#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace hive::app::updates {
struct Release {
    QString version, asset;
    QUrl download;
    QByteArray sha256;
    qint64 size{};
};
QString platform();
std::optional<Release> newer_release(const QByteArray& json, const QString& current, const QString& platform);
bool download_url(const QUrl& url);
void verify_download(const QString& path, const Release& release);
}
