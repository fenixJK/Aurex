#pragma once
#include <QWidget>
#include <functional>
#include <string>

namespace hive {
// Custom module pages edit the same control used by schema-driven settings.
class ChoiceControl : public QWidget {
public:
    std::function<void()> changed;
    virtual std::string value() const=0;
    virtual void setValue(const std::string&)=0;
};
}
