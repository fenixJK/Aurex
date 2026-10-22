#include "update_install.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QElapsedTimer>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace hive::app::updates {
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
QString executable(const QString& app) {
#ifdef Q_OS_MACOS
    return app+"/Contents/MacOS/Aurex";
#else
    return app;
#endif
}
QString helper_name() {
#ifdef Q_OS_WIN
    return "update-helper.exe";
#else
    return "update-helper";
#endif
}
QJsonObject manifest(const QString& directory) {
    QFile file(directory+"/install.json");
    require(file.open(QIODevice::ReadOnly) && file.size()<16384,"Update details are missing.");
    auto result=QJsonDocument::fromJson(file.readAll()).object();
    const QFileInfo stage(directory),target(result.value("target").toString());
    require(stage.isDir() && !stage.isSymLink() && stage.fileName().startsWith(".aurex-update-")
        && stage.absoluteFilePath()==stage.canonicalFilePath() && !target.isSymLink()
        && target.absolutePath()==stage.absolutePath(),"Invalid update location.");
    return result;
}
void write(const QString& path,const QByteArray& data) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit(),"Could not prepare the update files.");
}
#ifdef Q_OS_MACOS
QByteArray command(const QString& program,const QStringList& arguments) {
    QProcess process; process.start(program,arguments);
    if (!process.waitForFinished(60000)) { process.kill(); process.waitForFinished(); throw std::runtime_error("Preparing the update timed out."); }
    require(process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,"The downloaded app could not be validated.");
    return process.readAllStandardOutput();
}
#endif
bool restart(const QString& target,const QString& stage,const QString& error={}) {
    QStringList args{"--finish-update",stage};
    if (!error.isEmpty()) args<<"--update-error"<<error;
    return QProcess::startDetached(executable(target),args,QFileInfo(target).absolutePath());
}
}
QString installed_app() {
    const auto exe=QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
#ifdef Q_OS_MACOS
    QDir dir(QFileInfo(exe).absolutePath()); dir.cdUp(); dir.cdUp();
    require(dir.dirName().endsWith(".app"),"Run Aurex from its app bundle to install updates.");
    return dir.absolutePath();
#else
    return exe;
#endif
}
void prepare_install(const QString& directory,const QString& target,const Release& release) {
    const auto download=directory+"/download";
    verify_download(download,release);
    const auto staged=directory+"/replacement";
#ifdef Q_OS_MACOS
    const auto entries=command("/usr/bin/unzip",{"-Z1",download}).split('\n');
    require(entries.size()<10000,"The update archive contains too many files.");
    for (const auto& entry:entries) {
        if (entry.isEmpty()) continue;
        const auto path=QString::fromUtf8(entry);
        require((path.startsWith("Aurex.app/") || path.startsWith("__MACOSX/"))
            && !path.split('/').contains("..") && !path.contains('\\') && !path.contains('\r'),"The update archive contains an invalid path.");
    }
    // Portable Aurex bundles contain regular files, never archive symlinks.
    for (const auto& line:command("/usr/bin/zipinfo",{"-l",download}).split('\n'))
        require(!line.startsWith('l'),"The update archive contains an unsupported symbolic link.");
    const auto unpacked=directory+"/unpacked";
    require(QDir().mkdir(unpacked),"Could not create the update folder.");
    command("/usr/bin/ditto",{"-x","-k",download,unpacked});
    const auto app=unpacked+"/Aurex.app",plist=app+"/Contents/Info.plist";
    require(command("/usr/libexec/PlistBuddy",{"-c","Print :CFBundleIdentifier",plist}).trimmed()=="app.renova.desktop"
        && command("/usr/libexec/PlistBuddy",{"-c","Print :CFBundleShortVersionString",plist}).trimmed()==release.version.toUtf8(),"The downloaded app does not match this release.");
    const auto architecture=platform()=="macOS-arm64"?"arm64":"x86_64";
    require(command("/usr/bin/lipo",{"-archs",executable(app)}).trimmed().split(' ').contains(architecture),"The downloaded app is for a different processor.");
    command("/usr/bin/codesign",{"--verify","--deep","--strict",app});
    require(QDir().rename(app,staged),"Could not stage the downloaded app.");
#else
    QFile file(download); require(file.open(QIODevice::ReadOnly),"Could not read the update.");
    const auto header=file.read(64);
    require(header.size()==64 && header.startsWith("MZ"),"The update is not a Windows app.");
    const auto offset=quint32(quint8(header[60])) | (quint32(quint8(header[61]))<<8)
        | (quint32(quint8(header[62]))<<16) | (quint32(quint8(header[63]))<<24);
    require(file.seek(offset),"The Windows app header is invalid.");
    require(file.read(6)==QByteArray("PE\0\0\x64\x86",6),"The update is not a Windows x64 app.");
    file.close(); require(QFile::rename(download,staged),"Could not stage the downloaded app.");
#endif
    const auto helper=directory+"/"+helper_name();
    require(QFile::copy(QCoreApplication::applicationFilePath(),helper),"Could not prepare the update helper.");
    require(QFile::setPermissions(helper,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),"Could not prepare the update helper permissions.");
    write(directory+"/install.json",QJsonDocument(QJsonObject{{"target",target},{"parent",QCoreApplication::applicationPid()}}).toJson());
}
void launch_helper(const QString& directory) {
    require(QProcess::startDetached(directory+"/"+helper_name(),{"--apply-update",directory},directory),"Could not start the update helper. Your app has not been changed.");
}
void replace_app(const QString& staged,const QString& target,const QString& backup) {
    require(QFileInfo::exists(staged) && QFileInfo::exists(target) && !QFileInfo::exists(backup),"The update files are no longer available.");
    require(QDir().rename(target,backup),"Could not replace the app. Close other copies of Aurex and try again.");
    if (!QDir().rename(staged,target)) {
        require(QDir().rename(backup,target),"Could not restore the previous app. It is saved in the update folder.");
        throw std::runtime_error("Could not install the update. Your previous app was restored.");
    }
}
int apply_update(const QString& directory) {
    QString target;
    bool app_closed=false;
    try {
        const auto details=manifest(directory); target=details.value("target").toString();
        require(QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()==directory+"/"+helper_name(),"Invalid update helper location.");
        const auto parent=details.value("parent").toInteger();
        require(parent>0 && parent!=QCoreApplication::applicationPid(),"Invalid update process.");
#ifdef Q_OS_WIN
        const auto process=OpenProcess(SYNCHRONIZE,FALSE,DWORD(parent));
        require(process!=nullptr,"The app closed before the update was ready.");
#endif
        write(directory+"/ready","ready");
        QElapsedTimer timer; timer.start();
        while (!QFileInfo::exists(directory+"/commit") && timer.elapsed()<10000) QThread::msleep(50);
        const bool committed=QFileInfo::exists(directory+"/commit");
        if (committed) write(directory+"/accepted","accepted");
#ifdef Q_OS_WIN
        const auto exited=committed && WaitForSingleObject(process,30000)==WAIT_OBJECT_0;
        CloseHandle(process);
#else
        timer.restart();
        while (committed && kill(pid_t(parent),0)==0 && timer.elapsed()<30000) QThread::msleep(50);
        const bool exited=committed && kill(pid_t(parent),0)!=0 && errno==ESRCH;
#endif
        // Never change the app if the UI did not commit, or it is still running.
        if (!exited) return 1;
        app_closed=true;
        const auto backup=directory+"/previous",staged=directory+"/replacement";
        replace_app(staged,target,backup);
        if (!restart(target,directory)) {
            require(QDir().rename(target,staged) && QDir().rename(backup,target),"The updated app could not start. The previous app remains in the update folder.");
            throw std::runtime_error("The updated app could not start. Your previous app was restored.");
        }
        return 0;
    } catch (const std::exception& error) {
        if (app_closed && !target.isEmpty()) {
            QFile log(directory+"/error.txt"); if (log.open(QIODevice::WriteOnly)) log.write(error.what());
            restart(target,directory,QString::fromUtf8(error.what()));
        }
        return 1;
    }
}
bool cleanup_update(const QString& directory) {
    try {
        const auto details=manifest(directory);
        if (details.value("target").toString()!=installed_app()) return false;
        if (QFileInfo::exists(directory+"/error.txt") && QFileInfo::exists(directory+"/previous")) return false;
        // Leave the backup recoverable if a failed rename prevented restoration.
        if (!QFileInfo::exists(executable(installed_app()))) return false;
        const auto helper=directory+"/"+helper_name();
        if (QFileInfo::exists(helper) && !QFile::remove(helper)) return false;
        return QDir(directory).removeRecursively();
    } catch (...) { return false; }
}
}
