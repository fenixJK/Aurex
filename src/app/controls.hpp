#pragma once
#include <QCheckBox>
#include <QAbstractSpinBox>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QVariantAnimation>

namespace hive::app {
class ControlsStyle final : public QProxyStyle {
public:
    ControlsStyle():QProxyStyle(QStyleFactory::create("Fusion")) {}
    void polish(QWidget* widget) override {
        QProxyStyle::polish(widget);
        if (qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QComboBox*>(widget)) {
            widget->setFocusPolicy(Qt::StrongFocus);
            widget->installEventFilter(this);
        }
    }
    void unpolish(QWidget* widget) override {
        widget->removeEventFilter(this);
        QProxyStyle::unpolish(widget);
    }
    bool eventFilter(QObject* object,QEvent* event) override {
        if (event->type()==QEvent::Wheel && !static_cast<QWidget*>(object)->hasFocus()) {
            auto* wheel=static_cast<QWheelEvent*>(event);
            for(auto* parent=static_cast<QWidget*>(object)->parentWidget();parent;parent=parent->parentWidget())
                if(auto* area=qobject_cast<QAbstractScrollArea*>(parent)) {
                    auto* viewport=area->viewport();
                    QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition()),wheel->globalPosition(),
                        wheel->pixelDelta(),wheel->angleDelta(),wheel->buttons(),wheel->modifiers(),wheel->phase(),wheel->inverted(),wheel->source(),wheel->pointingDevice());
                    QApplication::sendEvent(viewport,&forwarded);
                    event->setAccepted(forwarded.isAccepted()); return true;
                }
            event->ignore(); return true;
        }
        return QProxyStyle::eventFilter(object,event);
    }
    int pixelMetric(PixelMetric metric,const QStyleOption* option=nullptr,const QWidget* widget=nullptr) const override {
        if (metric==PM_IndicatorWidth || metric==PM_IndicatorHeight) return 18;
        if (metric==PM_TabBarScrollButtonWidth) return 24;
        return QProxyStyle::pixelMetric(metric,option,widget);
    }
    QRect subElementRect(SubElement element,const QStyleOption* option,const QWidget* widget=nullptr) const override {
        if (element==SE_CheckBoxClickRect && widget) return widget->rect();
        return QProxyStyle::subElementRect(element,option,widget);
    }
    void drawPrimitive(PrimitiveElement element,const QStyleOption* option,QPainter* painter,const QWidget* widget=nullptr) const override {
        if (element!=PE_IndicatorCheckBox) { QProxyStyle::drawPrimitive(element,option,painter,widget); return; }
        const bool enabled=option->state&State_Enabled,checked=option->state&State_On,partial=option->state&State_NoChange;
        const bool hover=enabled && (option->state&State_MouseOver),pressed=enabled && (option->state&State_Sunken);
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        const auto box=QRectF(option->rect).adjusted(2,2,-2,-2);
        if (enabled && (option->state&State_HasFocus)) {
            painter->setPen(QPen(QColor("#c5efd1"),1)); painter->setBrush(Qt::NoBrush); painter->drawRoundedRect(box.adjusted(-2,-2,2,2),5,5);
        }
        const QColor fill=!enabled?"#263338":pressed?"#638c74":checked || partial?"#b5e6b9":hover?"#294238":"#152127";
        const QColor border=!enabled?"#405157":hover || pressed?"#dcf5de":checked || partial?"#b5e6b9":"#788e94";
        painter->setPen(QPen(border,1.2)); painter->setBrush(fill); painter->drawRoundedRect(box,3,3);
        painter->setPen(QPen(QColor(enabled?"#173326":"#7c9391"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if (partial) painter->drawLine(box.center()+QPointF(-3,0),box.center()+QPointF(3,0));
        else if (checked) {
            QPainterPath tick; tick.moveTo(box.left()+3,box.center().y()); tick.lineTo(box.left()+5.5,box.bottom()-3.5); tick.lineTo(box.right()-2.5,box.top()+3.5); painter->drawPath(tick);
        }
        painter->restore();
    }
};
class Toggle final : public QCheckBox {
    QVariantAnimation animation_;
    qreal position_{};
public:
    explicit Toggle(const QString& text,QWidget* parent=nullptr):QCheckBox(text,parent) {
        setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus); setAttribute(Qt::WA_Hover);
        animation_.setDuration(120); animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_,&QVariantAnimation::valueChanged,this,[this](const QVariant& value) { position_=value.toReal(); update(); });
        connect(this,&QCheckBox::toggled,this,[this](bool checked) {
            animation_.stop();
            if (!isVisible()) { position_=checked?1:0; update(); return; }
            animation_.setStartValue(position_); animation_.setEndValue(checked?1.0:0.0); animation_.start();
        });
    }
    QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(text())+68,34}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent*) override {
        if (animation_.state()!=QAbstractAnimation::Running || animation_.endValue().toBool()!=isChecked()) {
            animation_.stop(); position_=isChecked()?1:0;
        }
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(isEnabled()?QColor("#e5ecef"):QColor("#718587"));
        painter.drawText(rect().adjusted(0,0,-60,0),Qt::AlignLeft|Qt::AlignVCenter,text());
        const QRectF track(width()-44,(height()-22)/2.0,40,22);
        const QColor color=!isEnabled()?"#34444d":isDown()?"#749c80":isChecked()?"#b5e6b9":underMouse()?"#52696a":"#3a4c53";
        if (hasFocus()) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor("#c5efd1"),1)); painter.drawRoundedRect(track.adjusted(-2,-2,2,2),13,13); }
        painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawRoundedRect(track,11,11);
        painter.setBrush(isChecked()?QColor("#183224"):QColor("#e5ecef")); painter.drawEllipse(QRectF(track.x()+3+18*position_,track.y()+3,16,16));
    }
};
}
