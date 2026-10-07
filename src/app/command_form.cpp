#include "command_form.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace hive::app {
std::optional<extensions::CommandArguments> command_arguments(QWidget* parent,const extensions::Command& command) {
    using namespace extensions;
    if(command.arguments.empty())return CommandArguments{};
    QDialog dialog(parent);dialog.setWindowTitle(QString::fromStdString(command.label));dialog.setWindowModality(Qt::WindowModal);
    auto layout=new QVBoxLayout(&dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto content=new QWidget;auto form=new QFormLayout(content);scroll->setWidget(content);layout->addWidget(scroll);
    std::vector<std::function<void(CommandArguments&)>> readers;
    for(const auto& field:command.arguments) {
        QWidget* editor{};std::function<CommandValue()> read;
        if(field.type==ArgumentType::boolean) {
            auto box=new QCheckBox;box->setChecked(field.initial && std::get<bool>(*field.initial));editor=box;read=[box]{return box->isChecked();};
        } else if(field.type==ArgumentType::integer) {
            auto number=new QSpinBox;number->setRange(int(std::ceil(field.minimum)),int(std::floor(field.maximum)));
            if(field.initial)number->setValue(std::get<int>(*field.initial));editor=number;read=[number]{return number->value();};
        } else if(field.type==ArgumentType::number) {
            auto number=new QDoubleSpinBox;number->setDecimals(6);number->setRange(field.minimum,field.maximum);
            if(field.initial)number->setValue(std::visit([](const auto& v)->double {if constexpr(std::is_arithmetic_v<std::decay_t<decltype(v)>>)return v;else return 0;},*field.initial));
            editor=number;read=[number]{return number->value();};
        } else if(!field.choices.empty()) {
            auto choice=new QComboBox;for(const auto& value:field.choices)choice->addItem(QString::fromStdString(value));
            if(field.initial)choice->setCurrentText(QString::fromStdString(std::get<std::string>(*field.initial)));editor=choice;read=[choice]{return choice->currentText().toStdString();};
        } else {
            auto text=new QLineEdit;text->setMaxLength(int(field.maximum));if(field.initial)text->setText(QString::fromStdString(std::get<std::string>(*field.initial)));
            editor=text;read=[text]{return text->text().toStdString();};
        }
        editor->setObjectName(QString::fromStdString(field.id));editor->setToolTip(QString::fromStdString(field.description));
        QCheckBox* included{};
        if(field.required) form->addRow(QString::fromStdString(field.label),editor);
        else {
            included=new QCheckBox(QString::fromStdString(field.label));included->setChecked(field.initial.has_value());editor->setEnabled(included->isChecked());
            QObject::connect(included,&QCheckBox::toggled,editor,&QWidget::setEnabled);form->addRow(included,editor);
        }
        readers.push_back([id=field.id,included,read](CommandArguments& args){if(!included || included->isChecked())args.emplace(id,read());});
    }
    auto error=new QLabel;error->setWordWrap(true);error->setTextFormat(Qt::PlainText);layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    std::optional<CommandArguments> values;
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        CommandArguments args;for(const auto& read:readers)read(args);
        try { auto shape=command;shape.target_prompt.clear();values=validate_input(shape,{{},std::move(args)}).arguments;dialog.accept(); }
        catch(const std::exception& e){error->setText(QString::fromUtf8(e.what()));}
    });
    dialog.setFixedSize(420,std::min(560,110+int(command.arguments.size())*42));if(dialog.exec()!=QDialog::Accepted)return {};return values;
}
void command_result(QWidget* parent,const extensions::CommandResult& result) {
    if(result.fields.empty() && result.timers.empty() && !result.image && !result.progress)return;
    auto dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QString::fromStdString(result.title));auto layout=new QVBoxLayout(dialog);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);auto content=new QWidget;auto rows=new QFormLayout(content);
    const auto add=[&](const std::string& label,const QString& value){auto text=new QLabel(value);text->setTextFormat(Qt::PlainText);text->setWordWrap(true);rows->addRow(QString::fromStdString(label),text);};
    if(!result.message.empty())add({},QString::fromStdString(result.message));
    for(const auto& [label,value]:result.fields)add(label,QString::fromStdString(value));
    for(const auto& timer:result.timers) {
        QString value=timer.remaining?*timer.remaining>Time{}?QString::number((timer.remaining->count()+59999)/60000)+" min":"Ready":"Not confirmed";
        if(timer.progress)value+=" · "+QString::number(*timer.progress,'f',0)+"%";
        if(!timer.detail.empty())value+="\n"+QString::fromStdString(timer.detail);add(timer.label,value);
    }
    if(result.progress)add("Progress",QString::number(*result.progress,'f',0)+"%");
    if(result.image && result.image->valid()) {
        const auto& frame=*result.image;auto image=new QLabel;
        image->setPixmap(QPixmap::fromImage(QImage(frame.rgba.data(),frame.width,frame.height,frame.width*4,QImage::Format_RGBA8888)).scaledToWidth(460,Qt::SmoothTransformation));rows->addRow(image);
    }
    scroll->setWidget(content);layout->addWidget(scroll);auto close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);
    QObject::connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::reject);dialog->setFixedSize(500,360);dialog->show();
}
}
