#include "discord.hpp"
#include "message.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>


namespace hive::reporting {
namespace {
QString text(const QString& value,int limit) { return redact(value).left(limit); }
QJsonObject embed(const QString& title,const QString& description,int color) {
    return {{"title",text(title,256)},{"description",text(description,1800)},{"color",color},
        {"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"footer",QJsonObject{{"text","Aurex"}}}};
}
QJsonObject payload(QJsonObject content) {
    if (!content.contains("timestamp")) content["timestamp"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return {{"username","Aurex"},{"embeds",QJsonArray{content}},
        {"allowed_mentions",QJsonObject{{"parse",QJsonArray{}}}}};
}
qint64 seconds(double value) {
    return std::isfinite(value) && value>0 ? qint64(std::ceil(std::min(value,86400.0)*1000)) : 0;
}
bool active(RunState state) { return state==RunState::running || state==RunState::paused; }
}
QUrl webhook_url(const QString& input) {
    const QUrl url(input.trimmed(),QUrl::StrictMode);
    static const QRegularExpression path(R"(^/api(?:/v[0-9]+)?/webhooks/[0-9]{16,22}/[A-Za-z0-9_-]{20,200}$)");
    if (!url.isValid() || url.scheme()!="https" || (url.host()!="discord.com" && url.host()!="discordapp.com")
        || !url.userInfo().isEmpty() || url.port(-1)!=-1 || url.hasFragment() || !path.match(url.path()).hasMatch()) return {};
    QUrl result=url; QUrlQuery query;
    const auto thread=QUrlQuery(url).queryItemValue("thread_id");
    if (!thread.isEmpty()) {
        if (!QRegularExpression("^[0-9]{16,22}$").match(thread).hasMatch()) return {};
        query.addQueryItem("thread_id",thread);
    }
    query.addQueryItem("wait","true"); result.setQuery(query); return result;
}
QDateTime next_report_time(const QDateTime& now,int minutes) {
    if (!now.isValid() || minutes<1 || minutes>720) throw std::invalid_argument("Invalid report schedule");
    auto next=now.addMSecs(60000-now.time().second()*1000-now.time().msec());
    while ((next.time().hour()*60+next.time().minute())%minutes) next=next.addSecs(60);
    return next;
}
QString validate(const DiscordSettings& settings,bool webhook_required) {
    if (webhook_required && (settings.enabled || !settings.webhook.trimmed().isEmpty()) && webhook_url(settings.webhook).isEmpty())
        return "Enter a Discord webhook URL.";
    if (!settings.report_channel.isEmpty() && !QRegularExpression("^[0-9]{16,22}$").match(settings.report_channel).hasMatch())
        return "The report channel ID must contain 16–22 digits.";
    if (!settings.mention.isEmpty() && !QRegularExpression("^[0-9]{16,22}$").match(settings.mention).hasMatch())
        return "The alert user ID must contain 16–22 digits.";
    if (!supported_report_theme(settings.appearance.theme)
        || settings.appearance.chart_height<75 || settings.appearance.chart_height>150
        || settings.appearance.background_fade<0 || settings.appearance.background_fade>100
        || settings.appearance.background_zoom<ReportAppearance::min_background_zoom || settings.appearance.background_zoom>ReportAppearance::max_background_zoom
        || !std::isfinite(settings.appearance.background_x) || settings.appearance.background_x<0 || settings.appearance.background_x>1
        || !std::isfinite(settings.appearance.background_y) || settings.appearance.background_y<0 || settings.appearance.background_y>1) return "Choose a supported report layout.";
    if (settings.report_minutes<1 || settings.report_minutes>720) return "Choose a report interval up to 12 hours.";
    return {};
}
QJsonObject report_payload(const Report& report) {
    const auto& snapshot=report.snapshot;
    const auto elapsed_ms=report.session_ms;
    const auto end=report.created.addMSecs(-report.window_age_ms);
    auto content=embed("Aurex · "+duration(report.window_ms)+" report",end.addMSecs(-report.window_ms).toString("d MMM · HH:mm")+" – "+end.toString("HH:mm"),0x8abea1);
    const auto money=[](auto value) { return value?amount(double(*value)):QString("—"); };
    QJsonArray fields{
        QJsonObject{{"name","Honey · "+duration(report.window_ms)},{"value",money(report.honey.period)},{"inline",true}},
        QJsonObject{{"name","Honey/hour"},{"value",money(report.honey.hourly)},{"inline",true}},
        QJsonObject{{"name",report.honey.current_age_ms>2*sample_interval_ms?"Last known balance":"Balance"},{"value",money(report.honey.current)},{"inline",true}},
        QJsonObject{{"name","Session"},{"value",duration(elapsed_ms)+" · "+money(report.honey.session)+" honey"},{"inline",true}},
        QJsonObject{{"name",snapshot.state.isEmpty()?"Status":text(snapshot.state,80)},
            {"value",snapshot.activity.isEmpty()?"—":text(snapshot.activity,300)},{"inline",false}}};
    qsizetype remaining=6000-content["title"].toString().size()-content["description"].toString().size()-content["footer"].toObject()["text"].toString().size();
    for (const auto& field:fields) remaining-=field.toObject()["name"].toString().size()+field.toObject()["value"].toString().size();
    for (const auto& section:snapshot.sections.first(std::min(qsizetype(12),snapshot.sections.size()))) {
        QStringList lines;
        for (const auto& count:summarize(report,section)) lines.append(text(count.label,60)+": "+amount(double(count.period)));
        for (const auto& metric:section.metrics) if (metric.progress && lines.size()<6) lines.append(text(metric.label,60)+": "+text(metric.value,100));
        if (lines.empty()) continue;
        const auto name=text(section.name,80),value=lines.join(" · ").left(360);
        if (name.size()+value.size()>remaining) break;
        remaining-=name.size()+value.size();
        fields.append(QJsonObject{{"name",name},{"value",value},{"inline",true}});
    }
    content["fields"]=fields; return payload(content);
}
Discord::Discord(SnapshotSource snapshot,ImageSource image,QObject* parent,QNetworkAccessManager* network)
    :QObject(parent),snapshot_(std::move(snapshot)),image_(std::move(image)),network_(network) {
    clock_.start(); retry_.setSingleShot(true); deadline_.setSingleShot(true);
    period_.setSingleShot(true); period_.setTimerType(Qt::PreciseTimer);
    connect(&retry_,&QTimer::timeout,this,[this] { pump(); });
    connect(&period_,&QTimer::timeout,this,[this] {
        if (QDateTime::currentDateTime()>=report_due_) send_report();
        schedule_reports();
    });
    connect(&deadline_,&QTimer::timeout,this,[this] { if (reply_) reply_->abort(); });
}
Discord::~Discord() { on_delivery={}; cancel(); }
void Discord::status(QString message) {
    delivery_status_=std::move(message); if (on_delivery) on_delivery(delivery_status_);
}
void Discord::cancel() {
    ++revision_; preparing_=false; period_.stop(); retry_.stop(); deadline_.stop(); queue_.clear();
    ++live_revision_; live_active_=live_failed_=false; live_message_.clear(); next_live_=0;
    if (reply_) { auto reply=reply_; reply_.clear(); reply->abort(); reply->deleteLater(); }
}
bool Discord::configure(const DiscordSettings& value) {
    if (const auto issue=validate(value,!bot_);!issue.isEmpty()) { status(issue); return false; }
    if (settings_==value && !blocked_) return true;
    auto delivery=settings_;delivery.buff_ids=value.buff_ids;delivery.report_minutes=value.report_minutes;delivery.appearance=value.appearance;
    if (delivery==value && !blocked_) {
        const bool reschedule=settings_.report_minutes!=value.report_minutes;
        settings_=value;if(reschedule) schedule_reports();return true;
    }
    const bool was_tracking=settings_.enabled || history_requested_;
    const bool destination_changed=bot_?settings_.report_channel!=value.report_channel:webhook_url(settings_.webhook)!=webhook_url(value.webhook);
    cancel(); settings_=value; blocked_=false; errors_.clear(); last_activity_={};
    if (was_tracking!=(settings_.enabled || history_requested_)) { history_.reset(clock_.elapsed()); next_sample_=0; }
    if (destination_changed) next_send_=0;
    const auto issue=destination_issue();blocked_=settings_.enabled && !issue.isEmpty();
    status(!settings_.enabled?"Off":blocked_?issue:"Ready to send"); schedule_reports(); return true;
}
QString Discord::destination_issue() const {
    if(!bot_)return webhook_url(settings_.webhook).isEmpty()?"Enter a Discord webhook URL.":QString{};
    if(!QRegularExpression("^[A-Za-z0-9._-]{20,256}$").match(bot_->token).hasMatch())return "Reports are waiting for a valid bot token.";
    if(!QRegularExpression("^[0-9]{16,22}$").match(bot_->channel).hasMatch())return "Reports are waiting for a valid bot channel ID.";
    return {};
}
void Discord::set_bot(std::optional<BotDestination> destination) {
    if(bot_==destination && !blocked_)return;
    cancel();bot_=std::move(destination);next_send_=0;errors_.clear();last_activity_={};
    const auto issue=destination_issue();blocked_=settings_.enabled && !issue.isEmpty();
    status(!settings_.enabled?"Off":blocked_?issue:"Ready to send");schedule_reports();
}
void Discord::schedule_reports() {
    period_.stop();
    if (settings_.enabled && !blocked_ && active(state_)) {
        const auto now=QDateTime::currentDateTime();
        report_due_=next_report_time(now,settings_.report_minutes);
        period_.start(int(now.msecsTo(report_due_)));
    }
}
void Discord::set_history_requested(bool requested) {
    if (history_requested_==requested) return;
    history_requested_=requested;
    if (!settings_.enabled) { history_.reset(clock_.elapsed()); next_sample_=0; }
}
void Discord::update(RunState state,std::uint64_t session_id,const QString& reason,const QString& activity) {
    const bool new_session=session_id && session_id!=session_id_;
    const auto now=clock_.elapsed();
    if (new_session) update_live(false);
    update_live(settings_.enabled && settings_.live_honey && !blocked_ && state==RunState::running
        && (activity=="Gathering" || activity=="Converting"));
    if (new_session && active(state)) { history_.reset(now); next_sample_=0; last_activity_={}; }
    if (settings_.enabled || history_requested_) {
        history_.observe(now,state==RunState::paused?"Paused":activity.isEmpty()?"Session setup":activity,active(state));
        if ((active(state) && now>=next_sample_) || (active(state_) && !active(state))) {
            next_sample_=now+sample_interval_ms;
            try { if (snapshot_) history_.sample(now,snapshot_()); } catch (...) {}
        }
    }
    if (state==state_ && !new_session) return;
    const auto previous=state_; state_=state; session_id_=session_id;
    if (active(state) && (new_session || !active(previous))) { session_clock_.restart(); elapsed_=0; errors_.clear(); }
    if (!active(state) && active(previous) && session_clock_.isValid()) elapsed_=session_clock_.elapsed();
    if (new_session || active(previous)!=active(state)) schedule_reports();
    if (state==RunState::idle) return;
    if (state==RunState::faulted) error(reason.isEmpty()?"The macro stopped after an error.":reason);
}
void Discord::update_live(bool active) {
    if (live_active_!=active) {
        live_active_=active; ++live_revision_; live_message_.clear(); next_live_=0; live_failed_=false;
        const auto first=queue_.begin()+((reply_ || preparing_) && !queue_.empty()?1:0);
        queue_.erase(std::remove_if(first,queue_.end(),[](const auto& message) { return message.live_revision!=0; }),queue_.end());
    }
    if (!active || live_failed_ || !live_image_ || clock_.elapsed()<next_live_) return;
    next_live_=clock_.elapsed()+5000;
    QImage image; try { image=live_image_(); } catch (...) {}
    if (image.isNull()) return;
    Message message{payload({{"description","Honey & backpack"},{"color",0x8abea1}}),"live-honey"};
    message.game=std::move(image); message.live_revision=live_revision_;
    enqueue(std::move(message));
}
void Discord::activity(const QString& message) {
    activity_status({message.toStdString(),{}});
}
void Discord::activity_status(const ActivityStatus& activity) {
    if (!settings_.enabled || activity.empty() || activity.kind==ActivityKind::diagnostic || activity==last_activity_) return;
    if (activity.is_alert()) { activity_event(activity); return; }
    last_activity_=activity;
    QJsonObject content{{"color",0x8abea1}};
    if (activity.detail.empty()) content["description"]=text(QString::fromStdString(activity.title),256);
    else {
        content["title"]=text(QString::fromStdString(activity.title),256);
        content["description"]=text(QString::fromStdString(activity.detail),1800);
    }
    Message message{payload(content),"activity"};
    enqueue(std::move(message));
}
void Discord::activity_event(const ActivityStatus& activity,const QImage& screenshot) {
    if (!settings_.enabled || activity.empty() || activity.kind==ActivityKind::diagnostic) return;
    if (activity.kind==ActivityKind::failure) { error(QString::fromStdString(activity.text()),screenshot); return; }
    const bool recovering=activity.kind==ActivityKind::recovery;
    const auto key=recovering?"recovery:"+text(QString::fromStdString(activity.text()),1800):QString{};
    if (recovering && (!settings_.errors || !record_alert(key))) return;
    Message message{payload({{"title",text(QString::fromStdString(activity.title),256)},
        {"description",text(QString::fromStdString(activity.detail),1800)},{"color",recovering?0xe0b86e:0x8abea1}}),key};
    if (!recovering || settings_.error_screenshots) {
        message.game=screenshot;
        if (recovering && screenshot.isNull() && image_) try { message.game=image_(); } catch (...) {}
    }
    enqueue(std::move(message));
}
void Discord::notice(const QString& message) {
    if (!settings_.enabled || !settings_.errors || message.isEmpty()) return;
    const auto clean=text(message,1800),key="notice:"+clean;
    if (!record_alert(key)) return;
    enqueue({payload({{"description",clean},{"color",0xe0b86e}}),key});
}
bool Discord::record_alert(const QString& key) {
    const auto now=clock_.elapsed();
    std::erase_if(errors_,[now](const auto& item) { return now-item.second>=600000; });
    if (errors_.contains(key)) return false;
    if (errors_.size()>=32) errors_.erase(std::min_element(errors_.begin(),errors_.end(),[](const auto& a,const auto& b) { return a.second<b.second; }));
    errors_[key]=now;history_.alert(now);return true;
}
void Discord::error(const QString& message,std::optional<QImage> screenshot) {
    if (!settings_.enabled || !settings_.errors || message.isEmpty()) return;
    const auto clean=text(message,1800);
    if (!record_alert("error:"+clean)) return;
    auto body=payload(embed("Action failed",clean,0xe39b88));
    if (!settings_.mention.isEmpty()) {
        body["content"]="<@"+settings_.mention+">";
        body["allowed_mentions"]=QJsonObject{{"parse",QJsonArray{}},{"users",QJsonArray{settings_.mention}}};
    }
    Message alert{std::move(body),"error:"+clean};
    if (settings_.error_screenshots) {
        if (screenshot) alert.game=std::move(*screenshot);
        else if (image_) try { alert.game=image_(); } catch (...) {}
    }
    enqueue(std::move(alert));
}
void Discord::send_report() {
    if (!settings_.enabled) { status("Enable Discord reporting first."); return; }
    try {
        auto report=preview_report();
        auto body=report_payload(report);
        enqueue({std::move(body),"report",settings_.screenshots,0,0,std::move(report)});
    } catch (...) { status("Could not prepare the report. The macro is still running."); }
}
Report Discord::preview_report() {
    const auto elapsed=active(state_) && session_clock_.isValid()?session_clock_.elapsed():elapsed_;
    auto snapshot=snapshot_?snapshot_():Snapshot{}; const auto now=clock_.elapsed();
    if ((settings_.enabled || history_requested_) && active(state_) && now>=next_sample_) { history_.sample(now,snapshot); next_sample_=now+sample_interval_ms; }
    auto report=history_.report(std::move(snapshot),elapsed,qint64(settings_.report_minutes)*60000,now);
    report.appearance=settings_.appearance;
    report.buffs.removeIf([this](const auto& trace) {
        return !settings_.buff_ids.contains(trace.current.id.isEmpty()?trace.current.name:trace.current.id);
    });
    return report;
}
QList<Gauge> Discord::available_buffs() const {
    auto buffs=snapshot_?snapshot_().buffs:QList<Gauge>{};
    buffs.removeIf([](const auto& buff) { return !buff.level; });
    return buffs;
}
void Discord::enqueue(Message message) {
    if (!settings_.enabled || blocked_) return;
    if (message.game.width()>1280 || message.game.height()>720)
        message.game=message.game.scaled(1280,720,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    message.expires=clock_.elapsed()+600000;
    const auto first=queue_.begin()+((reply_ || preparing_) && !queue_.empty()?1:0);
    if (const auto duplicate=std::find_if(first,queue_.end(),[&](const auto& old) { return !message.key.isEmpty() && old.key==message.key; });duplicate!=queue_.end())
        *duplicate=std::move(message);
    else {
        if (queue_.size()>=16) queue_.erase(first);
        queue_.push_back(std::move(message));
    }
    pump();
}
void Discord::pump() {
    if (reply_ || preparing_ || blocked_ || !settings_.enabled) return;
    const auto now=clock_.elapsed();
    while (!queue_.empty() && queue_.front().expires<=now) queue_.pop_front();
    if (queue_.empty()) return;
    if (now<next_send_) { retry_.start(int(next_send_-now)); return; }
    auto& message=queue_.front(); ++message.attempts;
    if (message.images) { post(*message.images,revision_); return; }
    if (message.report || !message.game.isNull()) {
        if (message.game.isNull()) try { if (message.screenshot && image_) message.game=image_(); } catch (...) {}
        preparing_=true; status(message.report?"Preparing report…":"Preparing screenshot…"); const auto revision=revision_;
        auto watcher=new QFutureWatcher<Images>(this);
        connect(watcher,&QFutureWatcher<Images>::finished,this,[this,watcher,revision] {
            const auto data=watcher->result(); watcher->deleteLater();
            if (revision!=revision_) return;
            preparing_=false; post(data,revision);
        });
        watcher->setFuture(QtConcurrent::run([image=std::exchange(message.game,{}),report=std::exchange(message.report,std::nullopt),live=message.live_revision!=0] {
            Images result;
            try {
                if (report) {
                    QBuffer buffer(&result.report); buffer.open(QIODevice::WriteOnly);
                    if (!render_report(*report).save(&buffer,"PNG") || result.report.size()>4*1024*1024) result.report.clear();
                }
                if (!image.isNull()) {
                    QBuffer game(&result.game); game.open(QIODevice::WriteOnly);
                    const auto resized=image.width()>1280 || image.height()>720?image.scaled(1280,720,Qt::KeepAspectRatio,Qt::SmoothTransformation):image;
                    if (!resized.save(&game,live?"PNG":"JPG",live?-1:80)
                        || result.game.size()>1024*1024) result.game.clear();
                }
            } catch (...) { result={}; }
            return result;
        })); return;
    }
    post({},revision_);
}
void Discord::post(Images images,std::uint64_t revision) {
    if (revision!=revision_ || queue_.empty() || !settings_.enabled) return;
    queue_.front().images=images;
    const auto live=queue_.front().live_revision;
    if (live && (live!=live_revision_ || !live_active_ || images.game.isEmpty())) { queue_.pop_front(); pump(); return; }
    auto body=queue_.front().body;
    const auto channel=queue_.front().key=="report" && !settings_.report_channel.isEmpty()?settings_.report_channel:bot_?bot_->channel:QString{};
    auto url=bot_?QUrl("https://discord.com/api/v10/channels/"+channel+"/messages"):webhook_url(settings_.webhook);
    const bool edit=live && !live_message_.isEmpty();
    if (edit) url.setPath(url.path()+(bot_?"/":"/messages/")+live_message_);
    QNetworkRequest request(url);
    if(bot_) {request.setRawHeader("Authorization","Bot "+bot_->token.toUtf8());body.remove("username");}
    if (!network_) network_=new QNetworkAccessManager(this);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    if (queue_.front().screenshot && images.game.isEmpty()) {
        auto embeds=body["embeds"].toArray(); auto content=embeds[0].toObject();
        content["footer"]=QJsonObject{{"text","Aurex · Game screenshot unavailable"}}; embeds[0]=content; body["embeds"]=embeds;
    }
    QList<MessageFile> files;
    if (!images.report.isEmpty() || !images.game.isEmpty()) {
        auto embeds=body["embeds"].toArray();
        const auto attach=[&](const QByteArray& data,const QString& name,const QString& type,QJsonObject content,const QString& description) {
            const int index=files.size();
            files.append({name,type,text(description,1024),data});
            content["image"]=QJsonObject{{"url","attachment://"+name}};
            if (!index) embeds[0]=content; else embeds.append(content);
        };
        if (!images.report.isEmpty()) {
            auto content=embeds[0].toObject(); QString description=content["description"].toString();
            for (const auto& field:content["fields"].toArray()) { const auto value=field.toObject(); description+="; "+value["name"].toString()+": "+value["value"].toString(); }
            content.remove("fields"); content.remove("description");
            attach(images.report,"report.png","image/png",content,description);
        }
        if (!images.game.isEmpty()) attach(images.game,live?"counters.png":"game.jpg",live?"image/png":"image/jpeg",
            images.report.isEmpty()?embeds[0].toObject():embed("Roblox",{},0x8abea1),live?"In-game honey and backpack counters":"Current Roblox game window");
        body["embeds"]=embeds;
    }
    reply_=send_message(*network_,request,edit?"PATCH":"POST",body,files);
    deadline_.start(20000);
    connect(reply_,&QNetworkReply::finished,this,[this,reply=reply_.data(),revision] { completed(reply,revision); });
    status("Sending…");
}
void Discord::completed(QNetworkReply* reply,std::uint64_t revision) {
    reply->deleteLater();
    if (revision!=revision_ || queue_.empty()) return;
    deadline_.stop(); reply_.clear();
    const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto now=clock_.elapsed();
    if (reply->rawHeader("X-RateLimit-Remaining")=="0")
        next_send_=std::max(next_send_,now+seconds(reply->rawHeader("X-RateLimit-Reset-After").toDouble()));
    if (http>=200 && http<300 && reply->error()==QNetworkReply::NoError) {
        if (queue_.front().live_revision==live_revision_ && live_active_ && live_message_.isEmpty()) {
            const auto id=QJsonDocument::fromJson(reply->readAll()).object()["id"].toString();
            if (QRegularExpression("^[0-9]{16,22}$").match(id).hasMatch()) live_message_=id;
            else live_failed_=true;
        }
        queue_.pop_front(); status("Sent at "+QTime::currentTime().toString("HH:mm:ss"));
    } else if (http==404 && queue_.front().live_revision && reply->request().url().path().contains("/messages/")) {
        if (queue_.front().live_revision==live_revision_) live_message_.clear();
        queue_.pop_front(); status("Live counter message removed. It will be recreated.");
    } else if (http==429) {
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();
        const auto wait=std::max(seconds(body["retry_after"].toDouble()),seconds(reply->rawHeader("Retry-After").toDouble()));
        next_send_=std::max(next_send_,now+(wait?wait:5000));
        status("Discord is busy. Waiting to retry.");
    } else if ((http>=500 || http==0) && queue_.front().attempts<3) {
        next_send_=std::max(next_send_,now+(qint64(1)<<queue_.front().attempts)*1000);
        status("Connection failed. Retrying in the background.");
    } else {
        const bool separate_report=bot_ && queue_.front().key=="report" && !settings_.report_channel.isEmpty() && settings_.report_channel!=bot_->channel;
        queue_.pop_front();
        if (separate_report && (http==403 || http==404)) {
            status("Report channel unavailable. Check its ID and the bot's Send Messages, Embed Links and Attach Files permissions.");
        } else if (http==401 || http==403 || http==404 || (http>=300 && http<400)) {
            blocked_=true; queue_.clear(); period_.stop();
            status(bot_?"Bot delivery unavailable. Check the token, channel ID and Send Messages permission, then reconnect."
                :"Webhook unavailable. Check its URL and permissions, then re-enable reporting.");
        } else status("Report could not be delivered. The macro is unaffected.");
    }
    pump();
}
}
