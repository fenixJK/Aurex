#pragma once
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace hive::reporting {
struct MessageFile { QString name,type,description; QByteArray bytes; };
inline QNetworkReply* send_message(QNetworkAccessManager& network,QNetworkRequest request,const QByteArray& method,
    QJsonObject body,const QList<MessageFile>& files={}) {
    request.setRawHeader("User-Agent",("Aurex/"+QCoreApplication::applicationVersion()).toUtf8());
    if (files.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        return network.sendCustomRequest(request,method,body.isEmpty()?QByteArray{}:QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    auto multipart=new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QJsonArray attachments;
    for (const auto& file:files) {
        const auto index=attachments.size();
        attachments.append(QJsonObject{{"id",index},{"filename",file.name},{"description",file.description.left(1024)}});
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,QString("form-data; name=\"files[%1]\"; filename=\"%2\"").arg(index).arg(file.name));
        part.setHeader(QNetworkRequest::ContentTypeHeader,file.type); part.setBody(file.bytes); multipart->append(part);
    }
    body["attachments"]=attachments;
    QHttpPart json; json.setHeader(QNetworkRequest::ContentDispositionHeader,"form-data; name=\"payload_json\"");
    json.setHeader(QNetworkRequest::ContentTypeHeader,"application/json"); json.setBody(QJsonDocument(body).toJson(QJsonDocument::Compact)); multipart->append(json);
    auto reply=network.sendCustomRequest(request,method,multipart); multipart->setParent(reply); return reply;
}
}
