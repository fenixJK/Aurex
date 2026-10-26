#include "servers.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QPointer>
#include <deque>
#include <map>
#include <mutex>
#include <random>

namespace hive::features::vichop {
namespace {
class PublicServers final : public ServerDirectory {
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    std::mutex mutex_;
    std::deque<std::string> queue_;
    std::map<std::string,Time> visited_;
    QString cursor_;
    Time now_{},retry_at_{},page_at_{};
    unsigned failures_{};
    bool active_{},requested_{};
    std::string status_{"Finding a public server"};
    void failed(Time delay) {
        queue_.clear(); cursor_.clear();
        retry_at_=now_+delay; status_="Server list unavailable; retrying shortly";
    }
public:
    ~PublicServers() override { if (reply_) { reply_->disconnect(); reply_->abort(); } }
    void set_active(bool active) override { std::lock_guard lock(mutex_); active_=active; if (!active) requested_=false; }
    ServerChoice next(Time now) override {
        std::lock_guard lock(mutex_);
        if (!active_) return {{},"Stopped"};
        for (auto it=visited_.begin();it!=visited_.end();) {
            if (now-it->second>=std::chrono::minutes(30)) it=visited_.erase(it); else ++it;
        }
        if (now-page_at_>std::chrono::minutes(3)) queue_.clear();
        while (!queue_.empty()) {
            auto id=std::move(queue_.front()); queue_.pop_front();
            if (visited_.contains(id)) continue;
            visited_[id]=now;
            if (visited_.size()>512) visited_.erase(std::min_element(visited_.begin(),visited_.end(),[](const auto& a,const auto& b) { return a.second<b.second; }));
            return {game::JoinTarget::public_server(id),"Joining a public server"};
        }
        requested_=true;
        return {{},now<retry_at_?status_:"Finding a public server"};
    }
    void update(Time now) override {
        std::unique_lock lock(mutex_); now_=now;
        if (!active_) {
            if (reply_) { auto* reply=reply_.data(); reply_=nullptr; reply->disconnect(); reply->abort(); reply->deleteLater(); }
            return;
        }
        if (!requested_ || reply_ || !queue_.empty() || now<retry_at_) return;
        requested_=false; retry_at_=now+Time(5000);
        QUrl url("https://games.roblox.com/v1/games/1537690962/servers/Public");
        QUrlQuery query; query.addQueryItem("sortOrder","Asc"); query.addQueryItem("limit","100"); query.addQueryItem("excludeFullGames","true");
        if (!cursor_.isEmpty()) query.addQueryItem("cursor",cursor_);
        url.setQuery(query);
        QNetworkRequest request(url); request.setTransferTimeout(15000);
        request.setRawHeader("Accept","application/json"); request.setRawHeader("User-Agent","Aurex");
        auto* reply=network_.get(request); reply_=reply;
        QObject::connect(reply,&QNetworkReply::readyRead,&network_,[reply] { if (reply->bytesAvailable()>1024*1024) reply->abort(); });
        QObject::connect(reply,&QNetworkReply::finished,&network_,[this,reply] {
            std::lock_guard guard(mutex_); reply_=nullptr; reply->deleteLater();
            if (!active_) return;
            const auto data=reply->readAll();
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QJsonParseError error; const auto document=QJsonDocument::fromJson(data,&error);
            if (reply->error()!=QNetworkReply::NoError || status!=200 || data.size()>1024*1024
                || error.error!=QJsonParseError::NoError || !document.isObject() || !document.object()["data"].isArray()) {
                const auto backoff=std::min(300,15*(1<<std::min(++failures_,4u)));
                failed(Time(1000*std::clamp(std::max(backoff,reply->rawHeader("Retry-After").toInt()),5,300))); return;
            }
            failures_=0; page_at_=now_; status_="Finding an unvisited public server";
            const auto object=document.object(); const auto rows=object["data"].toArray();
            if (rows.size()>100) { failed(Time(30000)); return; }
            cursor_=object["nextPageCursor"].toString();
            if (cursor_.size()>2048) { failed(Time(30000)); return; }
            std::vector<std::string> candidates;
            for (const auto& row:rows) {
                if (!row.isObject()) continue;
                const auto server=row.toObject(); const int playing=server["playing"].toInt(-1),maximum=server["maxPlayers"].toInt(-1);
                if (playing<1 || maximum<1 || maximum>100 || playing>=maximum) continue;
                const auto id=server["id"].toString().toLower().toStdString();
                try { game::JoinTarget::public_server(id); } catch (const std::invalid_argument&) { continue; }
                if (!visited_.contains(id)) candidates.push_back(id);
            }
            std::shuffle(candidates.begin(),candidates.end(),std::mt19937(std::random_device{}()));
            queue_.assign(candidates.begin(),candidates.end());
            if (queue_.empty()) { requested_=true; if (cursor_.isEmpty()) retry_at_=now_+Time(30000); }
        });
    }
};
}
std::shared_ptr<ServerDirectory> public_servers() { return std::make_shared<PublicServers>(); }
}
