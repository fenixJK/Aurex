#include "discord_packet.hpp"
#include "topics.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <algorithm>
#include <cstring>

namespace hive::coordination {
namespace {
constexpr auto prefix="Aurex Alt Sync v1\n";
void require(bool ok) { if (!ok) throw std::invalid_argument("Invalid Alt Sync message or invite."); }
using Key=std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
Key key(const QByteArray& pem,bool secret) {
    std::unique_ptr<BIO,decltype(&BIO_free)> in(BIO_new_mem_buf(pem.data(),int(pem.size())),BIO_free);require(bool(in));
    if (secret) return {PEM_read_bio_PrivateKey(in.get(),nullptr,nullptr,nullptr),EVP_PKEY_free};
    std::unique_ptr<X509,decltype(&X509_free)> cert(PEM_read_bio_X509(in.get(),nullptr,nullptr,nullptr),X509_free);
    require(bool(cert));return {X509_get_pubkey(cert.get()),EVP_PKEY_free};
}
void validate(const DiscordInvite& invite) {
    static const QRegularExpression secret("^[a-f0-9]{64}$");
    require(discord_id(invite.channel) && secret.match(invite.identity.token).hasMatch() && invite.identity.certificate.size()<=2048);
    const auto public_key=key(invite.identity.certificate,false);
    require(public_key && EVP_PKEY_is_a(public_key.get(),"RSA") && EVP_PKEY_get_bits(public_key.get())==2048);
}
QByteArray aad(const DiscordInvite& invite) {
    return invite.channel.toLatin1()+QCryptographicHash::hash(invite.identity.certificate,QCryptographicHash::Sha256);
}
QByteArray signature(const QByteArray& data,EVP_PKEY* private_key) {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(bool(ctx));
    require(EVP_DigestSignInit(ctx.get(),nullptr,EVP_sha256(),nullptr,private_key)==1);
    size_t size{};require(EVP_DigestSign(ctx.get(),nullptr,&size,reinterpret_cast<const unsigned char*>(data.data()),data.size())==1);
    QByteArray result(qsizetype(size),Qt::Uninitialized);
    require(EVP_DigestSign(ctx.get(),reinterpret_cast<unsigned char*>(result.data()),&size,reinterpret_cast<const unsigned char*>(data.data()),data.size())==1);
    result.resize(qsizetype(size));return result;
}
bool signed_by(const QByteArray& data,const QByteArray& sig,EVP_PKEY* public_key) {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(bool(ctx));
    return EVP_DigestVerifyInit(ctx.get(),nullptr,EVP_sha256(),nullptr,public_key)==1 &&
        EVP_DigestVerify(ctx.get(),reinterpret_cast<const unsigned char*>(sig.data()),sig.size(),reinterpret_cast<const unsigned char*>(data.data()),data.size())==1;
}
bool label(const QString& value,int maximum) {
    return !value.isEmpty() && value.size()<=maximum && std::none_of(value.begin(),value.end(),[](QChar c) { return c.category()==QChar::Other_Control; });
}
QByteArray decode(const QByteArray& value) {
    const auto result=QByteArray::fromBase64Encoding(value,QByteArray::Base64UrlEncoding|QByteArray::AbortOnBase64DecodingErrors);
    require(bool(result));return result.decoded;
}
}
bool discord_id(const QString& value) { static const QRegularExpression pattern("^[0-9]{17,20}$");return pattern.match(value).hasMatch(); }
QString encode_discord_invite(const DiscordInvite& invite) {
    validate(invite);
    const QJsonObject value{{"v",1},{"channel",invite.channel},{"certificate",QString::fromLatin1(invite.identity.certificate)},{"secret",invite.identity.token}};
    return "aurex://discord/"+QString::fromLatin1(qCompress(QJsonDocument(value).toJson(QJsonDocument::Compact)).toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
}
DiscordInvite decode_discord_invite(const QString& value) {
    require(value.startsWith("aurex://discord/") && value.size()<=4096);
    const auto compressed=decode(value.mid(16).toLatin1());
    require(compressed.size()>=4 && qFromBigEndian<quint32>(compressed.constData())<=4096);
    const auto data=QJsonDocument::fromJson(qUncompress(compressed)).object();require(data["v"].toInt()==1);
    DiscordInvite result{data["channel"].toString(),{data["certificate"].toString().toLatin1(),{},data["secret"].toString()}};
    validate(result);return result;
}
QString seal_discord_packet(const DiscordInvite& invite,const DiscordPacket& packet) {
    validate(invite);const auto& peer=packet.peer;
    require(label(QString::fromStdString(peer.id),64) && label(QString::fromStdString(peer.name),32));
    const auto topics=encode_topics(peer.topics);decode_topics(topics);
    const QJsonObject data{{"id",QString::fromStdString(peer.id)},{"name",QString::fromStdString(peer.name)},
        {"host",peer.host},{"topics",topics},{"sent",double(packet.sent_ms)}};
    const auto plain=qCompress(QJsonDocument(data).toJson(QJsonDocument::Compact));
    const auto secret=QByteArray::fromHex(invite.identity.token.toLatin1()),context=aad(invite);
    QByteArray nonce(12,Qt::Uninitialized),tag(16,Qt::Uninitialized),cipher(plain.size()+16,Qt::Uninitialized);
    require(RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()),nonce.size())==1);
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);require(bool(ctx));
    require(EVP_EncryptInit_ex(ctx.get(),EVP_aes_256_gcm(),nullptr,reinterpret_cast<const unsigned char*>(secret.data()),reinterpret_cast<const unsigned char*>(nonce.data()))==1);
    int size{},end{};
    require(EVP_EncryptUpdate(ctx.get(),nullptr,&size,reinterpret_cast<const unsigned char*>(context.data()),int(context.size()))==1);
    require(EVP_EncryptUpdate(ctx.get(),reinterpret_cast<unsigned char*>(cipher.data()),&size,reinterpret_cast<const unsigned char*>(plain.data()),int(plain.size()))==1);
    require(EVP_EncryptFinal_ex(ctx.get(),reinterpret_cast<unsigned char*>(cipher.data())+size,&end)==1);
    cipher.resize(size+end);require(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_GET_TAG,16,tag.data())==1);
    const auto body=nonce+tag+cipher;
    QByteArray sig;
    if (peer.host) { const auto private_key=key(invite.identity.key,true);require(bool(private_key));sig=signature(body,private_key.get()); }
    const auto content=QString::fromLatin1(prefix)+QString::fromLatin1(body.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals))+"."+
        QString::fromLatin1(sig.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
    if (content.size()>2000) throw std::invalid_argument("Alt Sync state is too large for Discord. Use a direct connection for this group.");
    return content;
}
DiscordPacket open_discord_packet(const DiscordInvite& invite,const QString& content) {
    require(content.startsWith(prefix) && content.size()<=2000);
    const auto pieces=content.mid(qsizetype(strlen(prefix))).toLatin1().split('.');require(pieces.size()==2);
    const auto body=decode(pieces[0]),sig=decode(pieces[1]);require(body.size()>=28);
    const auto secret=QByteArray::fromHex(invite.identity.token.toLatin1()),context=aad(invite);require(secret.size()==32);
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);require(bool(ctx));
    require(EVP_DecryptInit_ex(ctx.get(),EVP_aes_256_gcm(),nullptr,reinterpret_cast<const unsigned char*>(secret.data()),reinterpret_cast<const unsigned char*>(body.data()))==1);
    int size{},end{};QByteArray plain(body.size(),Qt::Uninitialized);
    require(EVP_DecryptUpdate(ctx.get(),nullptr,&size,reinterpret_cast<const unsigned char*>(context.data()),int(context.size()))==1);
    require(EVP_DecryptUpdate(ctx.get(),reinterpret_cast<unsigned char*>(plain.data()),&size,reinterpret_cast<const unsigned char*>(body.data()+28),int(body.size()-28))==1);
    auto tag=body.mid(12,16);require(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_SET_TAG,16,tag.data())==1);
    require(EVP_DecryptFinal_ex(ctx.get(),reinterpret_cast<unsigned char*>(plain.data())+size,&end)==1);plain.resize(size+end);
    require(plain.size()>=4 && qFromBigEndian<quint32>(plain.constData())<=10000);
    const auto data=QJsonDocument::fromJson(qUncompress(plain)).object();
    const auto id=data["id"].toString(),name=data["name"].toString();
    require(label(id,64) && label(name,32) && data["host"].isBool() && data["sent"].isDouble());
    const auto sent=data["sent"].toDouble();require(sent>0 && sent<1e14 && sent==qint64(sent));
    if (data["host"].toBool()) { const auto public_key=key(invite.identity.certificate,false);require(public_key && signed_by(body,sig,public_key.get())); }
    else require(sig.isEmpty());
    return {{id.toStdString(),name.toStdString(),data["host"].toBool(),decode_topics(data["topics"])},qint64(sent)};
}
}
