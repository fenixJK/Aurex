#pragma once
#include "core/module_host/module.hpp"
#include <QWidget>

namespace hive {
class ChoiceControl : public QWidget {
public:
    std::function<void()> changed;
    virtual std::string value() const=0;
    virtual void setValue(const std::string&)=0;
};
struct SelectionValue {
    std::function<void()> changed;
    virtual ~SelectionValue()=default;
    virtual SettingSelection value() const=0;
    virtual void setValue(const SettingSelection&)=0;
};
class CollectionEditor : public QWidget, public SelectionValue {
public:
    std::function<void(const std::string&)> restore;
    std::function<void()> selected;
    virtual std::string current() const=0;
    virtual void select(const std::string& choice)=0;
    virtual void addSection(const std::string& choice,QWidget* section)=0;
};
CollectionEditor* collection_control(const std::vector<std::string>& choices,const SettingSelection& selected,const std::string& label);
ChoiceControl* choice_control(const extensions::Setting& field,const std::string& selected,bool compact=false);
QWidget* inline_selection(const extensions::Setting& field,const SettingSelection& selected,bool compact,unsigned columns=0);
QWidget* selection_control(const std::vector<std::string>& choices,const SettingSelection& selected,bool ordered);
}
