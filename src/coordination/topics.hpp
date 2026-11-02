#pragma once
#include "state.hpp"
#include <QJsonObject>
namespace hive::coordination {
QJsonObject encode_topics(const Topics& topics);
Topics decode_topics(const QJsonValue& value);
}
