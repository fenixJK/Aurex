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
#include <QVBoxLayout>
#include <algorithm>

namespace hive {
class OrderedRows final : public QWidget {
    struct Row { QWidget* widget; QSpinBox* priority; };
    std::vector<Row> rows_;
    QVBoxLayout* layout_;
    void move(QWidget* widget,int target) {
        if(target<0 || target>=int(rows_.size())) return;
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget==widget; });
        if(found==rows_.end()) return;
        const auto row=*found; rows_.erase(found); rows_.insert(rows_.begin()+target,row);
        for(std::size_t i=0;i<rows_.size();++i) { layout_->removeWidget(rows_[i].widget); layout_->insertWidget(int(i),rows_[i].widget); rows_[i].priority->setValue(int(i)+1); }
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
            drag->setPixmap(row_->grab()); drag->exec(Qt::MoveAction); drag->deleteLater(); setDown(false);
        }
        void keyPressEvent(QKeyEvent* event) override {
            if(event->modifiers()==Qt::ControlModifier && (event->key()==Qt::Key_Up || event->key()==Qt::Key_Down)) {
                const auto found=std::find_if(owner_->rows_.begin(),owner_->rows_.end(),[&](const auto& row) { return row.widget==row_; });
                owner_->move(row_,int(found-owner_->rows_.begin())+(event->key()==Qt::Key_Up?-1:1)); event->accept(); return;
            }
            QToolButton::keyPressEvent(event);
        }
    public:
        Handle(OrderedRows* owner,QWidget* row):owner_(owner),row_(row) {
            setFocusPolicy(Qt::StrongFocus); setText("≡"); setCursor(Qt::OpenHandCursor); setToolTip("Drag to change priority");
            setAccessibleName("Reorder "+row->accessibleName()); setFixedWidth(24);
        }
    };
    void dragEnterEvent(QDragEnterEvent* event) override {
        if(event->source()==this && event->mimeData()->hasFormat("application/x-aurex-setting-row")) event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* event) override { if(event->source()==this) event->acceptProposedAction(); }
    void dropEvent(QDropEvent* event) override {
        if(event->source()!=this) return;
        const auto name=QString::fromUtf8(event->mimeData()->data("application/x-aurex-setting-row"));
        auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget->objectName()==name; });
        if(found==rows_.end()) return;
        const auto moving=found->widget;
        int target=0;
        for(const auto& row:rows_) if(row.widget!=moving && event->position().y()>=row.widget->geometry().center().y()) ++target;
        move(moving,target);
        event->acceptProposedAction();
    }
public:
    explicit OrderedRows(QWidget* parent=nullptr):QWidget(parent),layout_(new QVBoxLayout(this)) {
        setAcceptDrops(true); setObjectName("ordered-setting-rows"); layout_->setContentsMargins(0,0,0,0); layout_->setSpacing(3);
    }
    QToolButton* handle(QWidget* row) { return new Handle(this,row); }
    void add(QWidget* row,QSpinBox* priority) { rows_.push_back({row,priority}); layout_->addWidget(row); sort(); }
    void sort() {
        std::stable_sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b) { return a.priority->value()<b.priority->value(); });
        for(std::size_t i=0;i<rows_.size();++i) { layout_->removeWidget(rows_[i].widget); layout_->insertWidget(int(i),rows_[i].widget); }
    }
};
}
