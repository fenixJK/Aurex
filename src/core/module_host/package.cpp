#include "host.hpp"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace hive::extensions {
std::string Host::install(const QString& source,const QString& destination) {
    require_stopped();
    const auto manifest=read_manifest(source+"/extension.json");
    if (manifest.api!=1 || !installable_.contains(manifest.provider))
        throw std::runtime_error("Package provider is not registered for installation");
    const auto& factory=factories_.at(manifest.provider);
    auto verify=[&](const Manifest& candidate) {
        auto probe=factory(candidate);
        if (!probe) throw std::runtime_error("Provider returned no module");
        try {
            probe->start(defaults(candidate));
            if (!probe->executable() && !candidate.permissions.empty())
                throw std::runtime_error("Read-only modules cannot request input permissions");
        } catch (...) { probe->stop(); throw; }
        probe->stop();
    };
    verify(manifest);
    if (!QDir().mkpath(destination)) throw std::runtime_error("Cannot create extension folder");
    QDir root(destination); const auto id=QString::fromStdString(manifest.id);
    if (root.exists(id)) throw std::runtime_error("Extension folder already exists");
    QTemporaryDir stage(root.filePath(".install-XXXXXX"));
    if (!stage.isValid()) throw std::runtime_error("Cannot stage extension package");
    QFile input(source+"/extension.json");
    if (!input.open(QIODevice::ReadOnly) || input.size()>65536) throw std::runtime_error("Cannot read package manifest");
    const auto data=input.readAll();
    auto write=[&](const QString& name,const QByteArray& bytes) {
        QSaveFile output(stage.filePath(name));
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) throw std::runtime_error("Could not install extension");
    };
    write("extension.json",data);
    for (const auto& [name,file] : manifest.scripts) write(QString::fromStdString(file),QByteArray::fromStdString(manifest.sources.at(name)));
    const auto installed=read_manifest(stage.filePath("extension.json"));
    if (installed.id!=manifest.id || installed.api!=1 || installed.provider!=manifest.provider || installed.sources!=manifest.sources)
        throw std::runtime_error("Package changed during installation");
    verify(installed);
    if (!root.rename(stage.path(),root.filePath(id))) throw std::runtime_error("Could not finish package installation");
    stage.setAutoRemove(false);
    return manifest.id;
}
}
