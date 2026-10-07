#include "hive/module_host/settings_text.hpp"
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <set>

namespace hive::extensions {
QString setting_text(const Setting& field,const Value& value) {
    if (const auto* flag=std::get_if<bool>(&value)) return *flag?"On":"Off";
    if (const auto* number=std::get_if<int>(&value)) return QString::number(*number/std::pow(10.,field.decimals),'f',field.decimals);
    if (const auto* choice=std::get_if<std::string>(&value)) return QString::fromStdString(*choice);
    QStringList values;
    for (const auto& item:std::get<SettingSelection>(value)) values.append(QString::fromStdString(item));
    return values.isEmpty()?QString("[]"):values.join(", ");
}
Value parse_setting(const Setting& field,const QString& input) {
    const auto text=input.trimmed();
    if (std::holds_alternative<bool>(field.initial)) {
        if (text.compare("on",Qt::CaseInsensitive)==0 || text.compare("true",Qt::CaseInsensitive)==0) return true;
        if (text.compare("off",Qt::CaseInsensitive)==0 || text.compare("false",Qt::CaseInsensitive)==0) return false;
        throw std::invalid_argument("Use on or off");
    }
    if (std::holds_alternative<int>(field.initial)) {
        const auto pattern=field.decimals?QString("^-?[0-9]+(?:\\.[0-9]{1,%1})?$").arg(field.decimals):QString("^-?[0-9]+$");
        bool valid=false;const auto number=text.toDouble(&valid)*std::pow(10.,field.decimals);
        if (!QRegularExpression(pattern).match(text).hasMatch() || !valid || !std::isfinite(number)
            || std::round(number)<field.minimum || std::round(number)>field.maximum) throw std::invalid_argument("Value is outside the allowed range or precision");
        return int(std::round(number));
    }
    const auto choice=[&](const QString& name) {
        for (const auto& option:field.choices) if (QString::fromStdString(option).compare(name,Qt::CaseInsensitive)==0) return option;
        throw std::invalid_argument("Choose a value listed for this setting");
    };
    if (std::holds_alternative<std::string>(field.initial)) return choice(text);
    SettingSelection result;std::set<std::string> unique;
    if (text!="[]") for (const auto& part:text.split(',')) {
        auto value=choice(part.trimmed());
        if (!unique.insert(value).second) throw std::invalid_argument("A selection cannot contain duplicates");
        result.push_back(std::move(value));
    }
    return result;
}
}
