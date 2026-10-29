#pragma once
#include "module.hpp"
#include <QByteArray>

namespace hive::extensions {
QByteArray copy_collection_settings(const Manifest&,const Settings&,const std::string& collection,const std::string& choice);
Settings paste_collection_settings(const Manifest&,const Settings&,const std::string& collection,const std::string& choice,const QByteArray&);
}
