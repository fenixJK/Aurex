#pragma once
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSpinBox>
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

namespace hive {
class OrderedRows final : public QWidget {
    struct Row { QWidget* widget; QSpinBox* priority; };
    std::vector<Row> rows_;
    QVBoxLayout* layout_;
    struct Controls { QToolButton *rank{},*up{},*down{}; };
    std::map<QWidget*,Controls> controls_;
    QFrame* insertion_;
    void refresh() {
        for (std::size_t i=0;i<rows_.size();++i) {
            auto& row=rows_[i]; layout_->removeWidget(row.widget); layout_->insertWidget(int(i),row.widget);
            auto& controls=controls_.at(row.widget);
            controls.rank->setText(QString::number(i+1));
            controls.rank->setAccessibleName(QString("Priority %1: %2. Drag to reorder").arg(i+1).arg(row.widget->accessibleName()));
            controls.up->setEnabled(i>0); controls.down->setEnabled(i+1<rows_.size());
        }
    }
    void move(QWidget* widget,int target) {
        if(!isEnabled() || target<0 || target>=int(rows_.size())) return;
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget==widget; });
        if(found==rows_.end() || found-rows_.begin()==target) return;
        const auto row=*found; rows_.erase(found); rows_.insert(rows_.begin()+target,row);
        for(std::size_t i=0;i<rows_.size();++i) rows_[i].priority->setValue(int(i)+1);
        refresh();
    }
    void shift(QWidget* row,int direction) {
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& item) { return item.widget==row; });
        if (found!=rows_.end()) move(row,int(found-rows_.begin())+direction);
    }
    int target(QWidget* moving,qreal y) const {
        int index=0;
        for (const auto& row:rows_) if (row.widget!=moving && y>=row.widget->geometry().center().y()) ++index;
        return index;
    }
    class Handle final : public QToolButton {
        OrderedRows* owner_;
        QWidget* row_;
        QPoint start_;
        void mousePressEvent(QMouseEvent* event) override {
            start_=event->pos(); QToolButton::mousePressEvent(event);
        }
        void mouseMoveEvent(QMouseEvent* event) override {
            if(!(event->buttons()&Qt::LeftButton) || (event->pos()-start_).manhattanLength()<QApplication::startDragDistance()) return;
            auto drag=new QDrag(owner_); auto data=new QMimeData;
            data->setData("application/x-aurex-setting-row",row_->objectName().toUtf8()); drag->setMimeData(data);
            drag->setPixmap(row_->grab()); drag->setHotSpot(mapTo(row_,event->pos()));
            drag->exec(Qt::MoveAction); owner_->insertion_->hide(); drag->deleteLater(); setDown(false);
        }
        void keyPressEvent(QKeyEvent* event) override {
            if(event->modifiers()==Qt::ControlModifier && (event->key()==Qt::Key_Up || event->key()==Qt::Key_Down)) {
                owner_->shift(row_,event->key()==Qt::Key_Up?-1:1); event->accept(); return;
            }
            QToolButton::keyPressEvent(event);
        }
    public:
        Handle(OrderedRows* owner,QWidget* row):owner_(owner),row_(row) {
            setFocusPolicy(Qt::StrongFocus); setObjectName("order-rank"); setCursor(Qt::OpenHandCursor);
            setToolTip("Drag to change priority, or use the arrow buttons. Ctrl+Up/Down also moves this row."); setFixedSize(26,28);
        }
    };
    void dragEnterEvent(QDragEnterEvent* event) override {
        if(event->source()==this && event->mimeData()->hasFormat("application/x-aurex-setting-row")) event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if(event->source()!=this) return;
        const auto name=QString::fromUtf8(event->mimeData()->data("application/x-aurex-setting-row"));
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget->objectName()==name; });
        if (found==rows_.end()) return;
        const auto index=target(found->widget,event->position().y()); int current=0,y=height()-3;
        for (const auto& row:rows_) if (row.widget!=found->widget && current++==index) { y=row.widget->y()-3; break; }
        insertion_->setGeometry(0,std::max(0,y),width(),3); insertion_->raise(); insertion_->show(); event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent* event) override { insertion_->hide(); event->accept(); }
    void dropEvent(QDropEvent* event) override {
        insertion_->hide();
        if(event->source()!=this) return;
        const auto name=QString::fromUtf8(event->mimeData()->data("application/x-aurex-setting-row"));
        auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget->objectName()==name; });
        if(found==rows_.end()) return;
        const auto moving=found->widget;
        move(moving,target(moving,event->position().y()));
        event->acceptProposedAction();
    }
public:
    explicit OrderedRows(QWidget* parent=nullptr):QWidget(parent),layout_(new QVBoxLayout(this)) {
        setAcceptDrops(true); setObjectName("ordered-setting-rows"); layout_->setContentsMargins(0,0,0,0); layout_->setSpacing(6);
        insertion_=new QFrame(this); insertion_->setStyleSheet("background:#b5e6b9;border:none;"); insertion_->setAttribute(Qt::WA_TransparentForMouseEvents); insertion_->hide();
    }
    QToolButton* handle(QWidget* row) { auto rank=new Handle(this,row); controls_[row].rank=rank; return rank; }
    QWidget* buttons(QWidget* row) {
        auto widget=new QWidget; auto layout=new QHBoxLayout(widget); layout->setContentsMargins(0,0,0,0); layout->setSpacing(2);
        auto& controls=controls_.at(row); controls.up=new QToolButton; controls.down=new QToolButton;
        for (const auto direction:{-1,1}) {
            auto button=direction<0?controls.up:controls.down;
            button->setObjectName(direction<0?"order-up":"order-down"); button->setText(direction<0?"↑":"↓"); button->setFixedSize(28,28);
            button->setAccessibleName("Move "+row->accessibleName()+(direction<0?" up":" down")); button->setToolTip(button->accessibleName());
            connect(button,&QToolButton::clicked,this,[this,row,direction] { shift(row,direction); }); layout->addWidget(button);
        }
        return widget;
    }
    void add(QWidget* row,QSpinBox* priority) { rows_.push_back({row,priority}); layout_->addWidget(row); sort(); }
    void sort() {
        std::stable_sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b) { return a.priority->value()<b.priority->value(); });
        refresh();
    }
};
}
