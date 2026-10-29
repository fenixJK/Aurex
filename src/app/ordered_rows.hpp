#pragma once
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <vector>

namespace hive {
class OrderedRows final : public QWidget {
    struct Row { QWidget* widget; QSpinBox* priority; };
    std::vector<Row> rows_, original_;
    QVBoxLayout* layout_;
    std::map<QObject*,QWidget*> grips_;
    QWidget *pressed_{}, *grip_{}, *placeholder_{};
    QPoint start_, offset_;

    void refresh() {
        while (auto item=layout_->takeAt(0)) delete item;
        for (const auto& row:rows_) layout_->addWidget(placeholder_ && row.widget==pressed_ ? placeholder_ : row.widget);
        layout_->activate();
    }
    void save_order() {
        for (std::size_t i=0;i<rows_.size();++i) rows_[i].priority->setValue(int(i)+1);
    }
    void shift(QWidget* widget,int direction) {
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget==widget; });
        if (!isEnabled() || placeholder_ || found==rows_.end()) return;
        const auto target=found-rows_.begin()+direction;
        if (target<0 || target>=int(rows_.size())) return;
        std::iter_swap(found,rows_.begin()+target); refresh(); save_order();
    }
    void begin_drag() {
        original_=rows_;
        offset_=pressed_->mapFromGlobal(start_);
        placeholder_=new QWidget(this); placeholder_->setObjectName("order-placeholder");
        placeholder_->setAttribute(Qt::WA_StyledBackground); placeholder_->setFixedSize(pressed_->size());
        placeholder_->show();
        pressed_->setAttribute(Qt::WA_StyledBackground); pressed_->setProperty("dragging-row",true);
        pressed_->style()->unpolish(pressed_); pressed_->style()->polish(pressed_);
        grip_->setCursor(Qt::ClosedHandCursor);
        refresh(); pressed_->raise();
        // Escape cancels the drag before the application's global stop shortcut.
        qApp->installEventFilter(this);
    }
    void drag_to(const QPoint& global) {
        const auto y=mapFromGlobal(global).y()-offset_.y();
        const auto center=y+pressed_->height()/2;
        int target=0;
        for (const auto& row:rows_) if (row.widget!=pressed_ && center>row.widget->geometry().center().y()) ++target;
        const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row) { return row.widget==pressed_; });
        if (target!=found-rows_.begin()) {
            const auto row=*found; rows_.erase(found); rows_.insert(rows_.begin()+target,row); refresh();
        }
        pressed_->move(0,y); pressed_->raise();
    }
    void finish_drag(bool commit) {
        if (placeholder_) {
            qApp->removeEventFilter(this);
            const bool changed=!std::equal(rows_.begin(),rows_.end(),original_.begin(),[](const auto& a,const auto& b) { return a.widget==b.widget; });
            if (!commit) rows_=original_;
            delete placeholder_; placeholder_=nullptr;
            pressed_->setProperty("dragging-row",false);
            pressed_->style()->unpolish(pressed_); pressed_->style()->polish(pressed_);
            refresh();
            if (commit && changed) save_order();
            original_.clear();
        }
        if (grip_) grip_->setCursor(Qt::OpenHandCursor);
        pressed_=nullptr; grip_=nullptr;
    }
    bool eventFilter(QObject* watched,QEvent* event) override {
        if (placeholder_) {
            if ((event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress) && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
                if (event->type()==QEvent::KeyPress) finish_drag(false);
                event->accept(); return true;
            }
            if (watched==window() && event->type()==QEvent::WindowDeactivate) finish_drag(false);
        }
        const auto found=grips_.find(watched);
        if (found==grips_.end() || !isEnabled()) return QWidget::eventFilter(watched,event);
        if (event->type()==QEvent::KeyPress) {
            const auto key=static_cast<QKeyEvent*>(event);
            if (key->modifiers()==Qt::ControlModifier && (key->key()==Qt::Key_Up || key->key()==Qt::Key_Down)) {
                shift(found->second,key->key()==Qt::Key_Up?-1:1); return true;
            }
        } else if (event->type()==QEvent::MouseButtonPress) {
            const auto mouse=static_cast<QMouseEvent*>(event);
            if (mouse->button()==Qt::LeftButton) {
                pressed_=found->second; grip_=static_cast<QWidget*>(watched); start_=mouse->globalPosition().toPoint();
                grip_->setFocus(Qt::MouseFocusReason); return true;
            }
        } else if (pressed_ && event->type()==QEvent::MouseMove) {
            const auto mouse=static_cast<QMouseEvent*>(event);
            if (!(mouse->buttons()&Qt::LeftButton)) { finish_drag(false); return true; }
            const auto global=mouse->globalPosition().toPoint();
            if (!placeholder_ && (global-start_).manhattanLength()>=QApplication::startDragDistance()) begin_drag();
            if (placeholder_) drag_to(global);
            return true;
        } else if (pressed_ && event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
            finish_drag(rect().contains(mapFromGlobal(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()))); return true;
        }
        return QWidget::eventFilter(watched,event);
    }
    bool event(QEvent* event) override {
        if (pressed_ && (event->type()==QEvent::Hide || (event->type()==QEvent::EnabledChange && !isEnabled()))) finish_drag(false);
        return QWidget::event(event);
    }
public:
    explicit OrderedRows(QWidget* parent=nullptr):QWidget(parent),layout_(new QVBoxLayout(this)) {
        setObjectName("ordered-setting-rows"); layout_->setContentsMargins(0,0,0,0); layout_->setSpacing(3);
    }
    void bind(QWidget* grip,QWidget* row) {
        grips_[grip]=row; grip->installEventFilter(this); grip->setCursor(Qt::OpenHandCursor); grip->setFocusPolicy(Qt::StrongFocus);
        grip->setAccessibleName("Reorder "+row->accessibleName());
        grip->setToolTip("Drag to change priority. Ctrl+Up/Down also moves this row.");
    }
    QToolButton* handle(QWidget* row) {
        auto grip=new QToolButton; grip->setObjectName("order-handle"); grip->setText("≡"); grip->setFixedSize(24,26); bind(grip,row); return grip;
    }
    void add(QWidget* row,QSpinBox* priority) { rows_.push_back({row,priority}); sort(); }
    void sort() {
        if (pressed_) finish_drag(false);
        std::stable_sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b) { return a.priority->value()<b.priority->value(); }); refresh();
    }
};
}
