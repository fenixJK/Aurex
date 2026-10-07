#include "navigation.hpp"
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QScrollArea>
#include <algorithm>

namespace hive {
Navigation::Navigation(QWidget* parent): QWidget(parent) {
    auto shell=new QVBoxLayout(this); shell->setContentsMargins(0,0,0,0);
    scroll_=new QScrollArea; scroll_->setWidgetResizable(true); scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto contents=new QWidget; layout_=new QVBoxLayout(contents); scroll_->setWidget(contents); shell->addWidget(scroll_);
    layout_->setContentsMargins(0,0,0,0); layout_->setSpacing(3); layout_->setSizeConstraint(QLayout::SetMinAndMaxSize); layout_->addStretch();
}
void Navigation::addItem(const QString& title) { insertItem(count(),title); }
void Navigation::insertItem(int index,const QString& title) {
    if (index<0 || index>count()) return;
    auto button=new QPushButton(title); button->setObjectName("navigation"); button->setCheckable(true); button->setAutoExclusive(true);
    layout_->insertWidget(index+(module_start_>=0 && index>=module_start_?1:0),button); buttons_.insert(buttons_.begin()+index,button);
    if (current_>=index) ++current_;
    connect(button,&QPushButton::toggled,this,[this,button](bool checked) { if (checked) setCurrentRow(static_cast<int>(std::find(buttons_.begin(),buttons_.end(),button)-buttons_.begin())); });
}
void Navigation::setModuleStart(int index) {
    if (separator_) { layout_->removeWidget(separator_); delete separator_; }
    module_start_=index;
    separator_=new QLabel("MODULES"); separator_->setObjectName("muted");
    separator_->setContentsMargins(8,12,0,4); layout_->insertWidget(index,separator_);
}
void Navigation::addItems(const QStringList& titles) { for (const auto& title : titles) addItem(title); }
QString Navigation::title(int index) const { return buttons_.at(index)->text(); }
void Navigation::setCurrentRow(int index) {
    if (index<0 || index>=count()) return;
    current_=index; for (int i=0;i<count();++i) { QSignalBlocker block(buttons_[i]); buttons_[i]->setChecked(i==index); }
    layout_->activate(); scroll_->ensureWidgetVisible(buttons_[index]);
    if (on_selected) on_selected(index);
}
void Navigation::removeItem(int index) {
    if (index<0 || index>=count()) return;
    delete buttons_[index]; buttons_.erase(buttons_.begin()+index);
    if (current_>index) --current_;
    else if (current_==index) current_=std::min(index,count()-1);
    setCurrentRow(current_);
}
}
