#include "connection.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <stdexcept>
#include <cstring>

namespace hive::coordination {
HostIdentity create_identity() {
    const auto require=[](bool ok) { if(!ok) throw std::runtime_error("Could not create the host's secure identity."); };
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(EVP_PKEY_Q_keygen(nullptr,nullptr,"RSA",2048),EVP_PKEY_free);
    std::unique_ptr<X509,decltype(&X509_free)> cert(X509_new(),X509_free);
    require(key && cert);
    require(X509_set_version(cert.get(),2)==1);
    require(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),QRandomGenerator::system()->bounded(1,2147483647))==1);
    require(X509_gmtime_adj(X509_getm_notBefore(cert.get()),-300) && X509_gmtime_adj(X509_getm_notAfter(cert.get()),86400L*365*5));
    require(X509_set_pubkey(cert.get(),key.get())==1);
    auto name=X509_get_subject_name(cert.get());
    require(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("Aurex host"),-1,-1,0)==1);
    require(X509_set_issuer_name(cert.get(),name)==1 && X509_sign(cert.get(),key.get(),EVP_sha256())>0);
    const auto pem=[&](bool certificate) {
        std::unique_ptr<BIO,decltype(&BIO_free)> out(BIO_new(BIO_s_mem()),BIO_free);require(bool(out));
        require(certificate?PEM_write_bio_X509(out.get(),cert.get())==1:PEM_write_bio_PrivateKey_traditional(out.get(),key.get(),nullptr,nullptr,0,nullptr,nullptr)==1);
        char* bytes{};const auto length=BIO_get_mem_data(out.get(),&bytes);return QByteArray(bytes,length);
    };
    QByteArray token(32,Qt::Uninitialized);
    for(int i=0;i<32;i+=4) { const auto value=QRandomGenerator::system()->generate();memcpy(token.data()+i,&value,4); }
    return {pem(true),pem(false),QString::fromLatin1(token.toHex())};
}
QString save_identity(const HostIdentity& value) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"certificate",QString::fromLatin1(value.certificate)},
        {"key",QString::fromLatin1(value.key)},{"token",value.token}}).toJson(QJsonDocument::Compact));
}
HostIdentity load_identity(const QString& value) {
    const auto object=QJsonDocument::fromJson(value.toUtf8()).object();
    return {object["certificate"].toString().toLatin1(),object["key"].toString().toLatin1(),object["token"].toString()};
}
}
