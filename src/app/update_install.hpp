#pragma once
#include "update_release.hpp"

namespace hive::app::updates {
QString installed_app();
// Work takes place in a private directory beside the app, on the same filesystem.
void prepare_install(const QString& directory, const QString& target, const Release& release);
void launch_helper(const QString& directory);
int apply_update(const QString& directory);
bool cleanup_update(const QString& directory);
// The old app remains available until replacement and process launch succeed.
void replace_app(const QString& staged,const QString& target,const QString& backup);
}
