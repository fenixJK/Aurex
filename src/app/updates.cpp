#include "updates.hpp"
#include "update_install.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

namespace hive::app {
namespace {
QNetworkRequest request(const QUrl& url) {
    QNetworkRequest result(url);
    result.setRawHeader("User-Agent",("Aurex/"+QCoreApplication::applicationVersion()).toUtf8());
    result.setTransferTimeout(30000);
    result.setMaximumRedirectsAllowed(5);
    return result;
}
void deadline(QNetworkReply* reply,int milliseconds) {
    QTimer::singleShot(milliseconds,reply,[reply] { if (!reply->isFinished()) reply->abort(); });
}
}
Updates::Updates(QWidget* window,std::function<bool()> busy,QNetworkAccessManager* network)
    :QObject(window),window_(window),busy_(std::move(busy)),network_(network?network:new QNetworkAccessManager(this)) {
    version_=new QToolButton(window); version_->setAutoRaise(true);
    version_->setText("v"+QCoreApplication::applicationVersion()); version_->setToolTip("Check for updates");
    update_=new QPushButton("Update",window); update_->setFlat(true); update_->hide();
    version_->setStyleSheet("padding:3px 2px; font-size:12px;");
    update_->setStyleSheet("padding:3px 4px; font-size:12px;");
    connect(version_,&QToolButton::clicked,this,[this] { if (release_) offer(); else check(true); });
    connect(update_,&QPushButton::clicked,this,[this] { offer(); });
    // Screenshots and UI reviews must not contact GitHub or open an update notice.
    const auto args=QCoreApplication::arguments();
    if (!args.contains("--screenshot") && !args.contains("--update-error")) QTimer::singleShot(0,this,[this] { check(false); });
}
QWidget* Updates::version_row() {
    auto row=new QWidget(window_); auto layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    layout->addWidget(version_); layout->addWidget(update_); layout->addStretch(); return row;
}
void Updates::check(bool manual) {
    if (reply_ || installing_) return;
    version_->setEnabled(false);
    auto query=request(QUrl("https://api.github.com/repos/fenixJK/Aurex/releases?per_page=20"));
    query.setRawHeader("Accept","application/vnd.github+json");
    query.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto reply=network_->get(query); reply_=reply; deadline(reply,15000);
    const auto bytes=std::make_shared<QByteArray>();
    connect(reply,&QNetworkReply::readyRead,this,[reply,bytes] {
        bytes->append(reply->readAll()); if (bytes->size()>1024*1024) reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,manual] {
        reply_=nullptr; reply->deleteLater(); version_->setEnabled(true);
        try {
            if (reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)
                throw std::runtime_error("Could not check for updates. Try again later using the version number.");
            release_=updates::newer_release(*bytes,QCoreApplication::applicationVersion(),updates::platform());
            update_->setText(release_ && release_->prerelease?"Beta":"Update");
            update_->setVisible(release_.has_value());
            version_->setToolTip(release_?"Update to v"+release_->version+(release_->prerelease?" (Beta)":""):"Check for updates");
            if (release_) { if (manual || !busy_()) offer(); }
            else if (manual) QMessageBox::information(window_,"Aurex updates","You're up to date.");
        } catch (const std::exception& error) {
            version_->setToolTip(QString::fromUtf8(error.what()));
            if (manual) QMessageBox::warning(window_,"Aurex updates",QString::fromUtf8(error.what()));
        }
    });
}
void Updates::offer() {
    if (!release_ || installing_) return;
    auto box=new QMessageBox(QMessageBox::Information,release_->prerelease?"Aurex beta update":"Aurex update",
        "Aurex "+release_->version+(release_->prerelease?" (Beta)":"")+" is available.",QMessageBox::NoButton,window_);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText((release_->prerelease?QString("This version is marked as beta.\n\n"):QString{})
        +(busy_()?"Stop the macro before updating. Your settings will be kept.":"Update and restart Aurex? Your settings will be kept."));
    auto install_button=box->addButton(release_->prerelease?"Install beta and restart":"Update and restart",QMessageBox::AcceptRole);
    install_button->setEnabled(!busy_());
    auto later=box->addButton("Later",QMessageBox::RejectRole); box->setDefaultButton(later);
    connect(box,&QMessageBox::buttonClicked,this,[this,install_button](QAbstractButton* button) { if (button==install_button) install(); });
    box->open();
}
void Updates::fail(const QString& error) {
    reply_=nullptr; download_.close(); stage_.reset(); installing_=false;
    if (progress_) { progress_->close(); progress_->deleteLater(); progress_=nullptr; }
    version_->setEnabled(true); update_->setEnabled(true);
    if (!error.isEmpty()) QMessageBox::warning(window_,"Aurex update",error);
}
void Updates::install() {
    if (!release_ || installing_ || busy_()) return;
    try {
        const auto target=updates::installed_app();
        stage_=std::make_shared<QTemporaryDir>(QFileInfo(target).absolutePath()+"/.aurex-update-XXXXXX");
        if (!stage_->isValid()) throw std::runtime_error("Aurex cannot update in this folder. Move it to a writable folder and try again.");
        download_.setFileName(stage_->path()+"/download");
        if (!download_.open(QIODevice::WriteOnly)) throw std::runtime_error("Could not save the update download.");
        installing_=true; canceled_=false; error_.clear(); version_->setEnabled(false); update_->setEnabled(false);
        progress_=new QProgressDialog("Downloading Aurex "+release_->version+"…","Cancel",0,100,window_);
        progress_->setWindowTitle("Aurex update"); progress_->setWindowModality(Qt::WindowModal); progress_->setMinimumDuration(0); progress_->setAutoClose(false);
        auto query=request(release_->download);
        query.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
        auto reply=network_->get(query); reply_=reply; deadline(reply,300000);
        connect(progress_,&QProgressDialog::canceled,this,[this] { canceled_=true; if (reply_) reply_->abort(); });
        connect(reply,&QNetworkReply::redirected,this,[this,reply](const QUrl& url) {
            if (updates::download_url(url)) reply->redirectAllowed();
            else { error_="The update download redirected to an unexpected site."; reply->abort(); }
        });
        connect(reply,&QNetworkReply::readyRead,this,[this,reply] {
            while (reply->bytesAvailable()) {
                const auto bytes=reply->read(65536);
                if (download_.size()+bytes.size()>release_->size || download_.write(bytes)!=bytes.size()) {
                    error_="The update could not be saved or was larger than expected."; reply->abort(); return;
                }
            }
            if (progress_) progress_->setValue(int(download_.size()*100/release_->size));
        });
        connect(reply,&QNetworkReply::finished,this,[this,reply] {
            reply_=nullptr; reply->deleteLater(); download_.close();
            if (reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200) {
                const auto message=canceled_?QString{}:!error_.isEmpty()?error_:"The update could not be downloaded. Your app has not been changed.";
                fail(message); return;
            }
            try { prepare(); }
            catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
        });
    } catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); }
}
void Updates::prepare() {
    progress_->setRange(0,0); progress_->setLabelText("Preparing the update…");
    const auto stage=stage_; const auto release=*release_; const auto target=updates::installed_app();
    auto watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher] {
        const auto error=watcher->result(); watcher->deleteLater();
        if (canceled_) { fail({}); return; }
        if (!error.isEmpty()) { fail(error); return; }
        handoff();
    });
    watcher->setFuture(QtConcurrent::run([stage,release,target] {
        try { updates::prepare_install(stage->path(),target,release); return QString{}; }
        catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
    }));
}
void Updates::handoff() {
    progress_->setCancelButton(nullptr);
    progress_->setWindowFlag(Qt::WindowCloseButtonHint,false); progress_->show();
    disconnect(progress_,nullptr,this,nullptr);
    try { updates::launch_helper(stage_->path()); }
    catch (const std::exception& error) { fail(QString::fromUtf8(error.what())); return; }
    progress_->setLabelText("Restarting Aurex…");
    auto timer=new QTimer(this); timer->setInterval(50);
    connect(timer,&QTimer::timeout,this,[this,timer,attempts=0,requested=false]() mutable {
        if (!QFileInfo::exists(stage_->path()+(requested?"/accepted":"/ready"))) {
            if (++attempts<100) return;
            timer->stop(); timer->deleteLater(); fail("The update helper could not start. Your app has not been changed."); return;
        }
        if (!requested) {
            QSaveFile commit(stage_->path()+"/commit");
            if (!commit.open(QIODevice::WriteOnly) || commit.write("install")!=7 || !commit.commit()) {
                timer->stop(); timer->deleteLater(); fail("Could not start the update. Your app has not been changed."); return;
            }
            requested=true; attempts=0; return;
        }
        timer->stop(); timer->deleteLater();
        stage_->setAutoRemove(false); QCoreApplication::quit();
    }); timer->start();
}
}
