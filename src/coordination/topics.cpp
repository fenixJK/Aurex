#include "topics.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <limits>
namespace hive::coordination {
namespace {
bool label(const QString& value,int maximum=128) {
    return !value.isEmpty() && value.size()<=maximum && std::none_of(value.begin(),value.end(),[](QChar c) { return c.category()==QChar::Other_Control; });
}
void require(bool ok,const char* message) { if (!ok) throw std::invalid_argument(message); }
}
QJsonObject encode_topics(const Topics& topics) {
    QJsonObject result;
    for(const auto& [topic,values]:topics) {
        QJsonObject data;
        for(const auto& [key,value]:values) std::visit([&](const auto& item) {
            using T=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<T,std::string>) data[QString::fromStdString(key)]=QString::fromStdString(item);
            else if constexpr(std::is_same_v<T,SettingSelection>) { QJsonArray array;for(const auto& s:item) array.append(QString::fromStdString(s));data[QString::fromStdString(key)]=array; }
            else data[QString::fromStdString(key)]=item;
        },value);
        result[QString::fromStdString(topic)]=data;
    }
    return result;
}
Topics decode_topics(const QJsonValue& value) {
    require(value.isObject(),"Invalid account state");const auto object=value.toObject();
    require(object.size()<=16 && QJsonDocument(object).toJson(QJsonDocument::Compact).size()<=8192,"Account state is too large");
    Topics result;
    for(auto topic=object.begin();topic!=object.end();++topic) {
        require(label(topic.key(),64) && topic.value().isObject(),"Invalid account topic");
        const auto data=topic.value().toObject();require(data.size()<=16,"Too many account values");Settings values;
        for(auto item=data.begin();item!=data.end();++item) {
            require(label(item.key(),64),"Invalid account key");const auto v=item.value();SettingValue entry;
            if(v.isBool()) entry=v.toBool();
            else if(v.isDouble()) { const auto number=v.toDouble();require(number>=std::numeric_limits<int>::min() && number<=std::numeric_limits<int>::max() && number==int(number),"Invalid account number");entry=int(number); }
            else if(v.isString()) { require(v.toString().isEmpty() || label(v.toString(),2048),"Invalid account text");entry=v.toString().toStdString(); }
            else if(v.isArray()) {
                require(v.toArray().size()<=16,"Too many account choices");SettingSelection choices;
                for(const auto& choice:v.toArray()) { require(choice.isString() && label(choice.toString()),"Invalid account choice");choices.push_back(choice.toString().toStdString()); }entry=std::move(choices);
            } else throw std::invalid_argument("Invalid account value");
            values.emplace(item.key().toStdString(),std::move(entry));
        }
        result.emplace(topic.key().toStdString(),std::move(values));
    }
    return result;
}
}
