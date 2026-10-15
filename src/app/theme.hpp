#pragma once
#include <QApplication>
#include <QFontDatabase>
#include "controls.hpp"

namespace hive {
inline void apply_theme(QApplication& app) {
    app.setStyle(new hive::app::ControlsStyle);
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    app.setStyleSheet(R"(
        QWidget { background:#131b20; color:#e5ecef; font-size:13px; }
        QLabel { background:transparent; }
        QWidget#sidebar { background:#10171b; border-right:1px solid #27343b; }
        QWidget#session-bar { background:#18232a; border-top:1px solid #304149; }
        QLabel#brand { font-size:21px; font-weight:700; color:#aee7bb; }
        QLabel#heading { font-size:18px; font-weight:600; }
        QLabel#title { font-size:13px; font-weight:600; }
        QLabel#section-heading { font-size:13px; font-weight:600; color:#a8bcb1; padding-bottom:3px; border-bottom:1px solid #2b3a42; }
        QTabBar#collection-fields::tab { margin:0 3px 0 0; padding:4px 8px; }
        QLabel#navigation-separator { border-top:1px solid #27343b; margin:6px 8px 0 8px; }
        QCheckBox#choice-tile { background:#19252c; border:1px solid #34444d; border-radius:6px; padding:8px; }
        QCheckBox#choice-tile[compact="true"] { padding:4px 6px; }
        QCheckBox#choice-tile:hover { background:#233238; border-color:#6a9874; }
        QCheckBox#choice-tile:checked { background:#263e35; border-color:#6a9874; }
        QLabel#muted { color:#9cacae; line-height:1.5; }
        QProgressBar#module-progress { background:#2b3a42; border:none; border-radius:2px; }
        QProgressBar#module-progress::chunk { background:#8abea1; border-radius:2px; }
        QWidget#feature-card { background:#19252c; border:1px solid #2b3a42; border-radius:9px; }
        QToolButton { background:transparent; border:none; padding:5px; color:#a8bcb1; }
        QToolButton:hover { color:#d6eedc; }
        QWidget#card { background:#19252c; border:1px solid #2b3a42; border-radius:9px; }
        QWidget#card > QWidget { background:transparent; }
        QWidget#card QLabel { background:transparent; border:none; }
        QPushButton { background:#233238; border:1px solid #3a4d52; border-radius:6px; padding:5px 9px; }
        QPushButton:hover { background:#30474c; }
        QPushButton:checked { background:#263e35; border-color:#6a9874; color:#c8f3cd; }
        QPushButton#choice-option { padding:3px 7px; border-radius:4px; }
        QPushButton#choice-option:pressed { padding-top:4px; padding-bottom:2px; }
        QPushButton:pressed { background:#14221d; border-color:#9ccdb0; padding-top:7px; padding-bottom:3px; }
        QToolButton:pressed { background:#263e35; border-radius:4px; padding-top:7px; padding-bottom:3px; }
        QToolButton#image-choice { background:#19252c; border:2px solid #34444d; border-radius:6px; padding:4px; }
        QToolButton#image-choice:hover { background:#283c43; border-color:#6a9874; }
        QToolButton#image-choice:checked { background:#305d45; border-color:#a9e7b4; }
        QToolButton#image-choice:focus { border-color:#dfeddc; }
        QToolButton#image-choice:pressed { background:#142b20; padding-top:6px; padding-bottom:2px; }
        QToolButton#image-choice:disabled { background:#182026; border-color:#283139; }
        QTabWidget::pane { border:none; background:transparent; }
        QTabBar::tab { background:transparent; color:#9cacae; padding:5px 9px; margin:0 4px 5px 0; border-radius:5px; }
        QTabBar::tab:selected { background:#263e35; color:#c8f3cd; }
        QTabBar::tab:hover { background:#263e35; }
        QTabBar QToolButton#ScrollLeftButton,QTabBar QToolButton#ScrollRightButton { background:#19252c; border:1px solid #34444d; border-radius:3px; padding:0; }
        QPushButton#primary { background:#b5e6b9; color:#122219; border-color:#b5e6b9; font-weight:600; }
        QPushButton#primary:hover { background:#cdf2d0; }
        QPushButton#primary:pressed { background:#88ba91; padding-top:7px; padding-bottom:3px; }
        QPushButton#stop { color:#f0b6ab; border-color:#78564e; }
        QPushButton#disclosure { text-align:left; background:transparent; border:none; padding:8px 0; color:#9cacae; }
        QPushButton:focus { border-color:#9ccdb0; }
        QPushButton:disabled { color:#617074; background:#1b2529; border-color:#263338; }
        QPushButton#navigation { text-align:left; background:transparent; border:1px solid transparent; padding:6px 8px; }
        QPushButton#navigation:checked { background:#263e35; color:#c8f3cd; }
        QPushButton#navigation:hover { background:#1b2b29; }
        QPushButton#navigation:focus { border-color:#648779; }
        QPushButton#extension-choice { text-align:left; padding:7px 10px; }
        QPushButton#extension-choice:checked { background:#263e35; border-color:#6a9874; }
        QCheckBox#compact-choice { padding:3px 0; font-size:13px; }
        QCheckBox { background:transparent; spacing:6px; padding:2px 0; min-height:20px; }
        QCheckBox:hover { color:#ffffff; }
        QCheckBox:disabled { color:#718587; }
        QLabel#section-summary { color:#9cacae; font-size:12px; }
        QListWidget { background:#121c20; border:1px solid #34444d; border-radius:5px; padding:2px; }
        QListWidget::item { padding:4px 8px; }
        QListWidget::item:selected { background:#263e35; color:#c8f3cd; }
        QComboBox,QSpinBox,QDoubleSpinBox,QPlainTextEdit,QTreeWidget { background:#121c20; border:1px solid #34444d; border-radius:5px; padding:4px; selection-background-color:#315645; }
        QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border-color:#89b99a; }
        QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled,QLineEdit:disabled { color:#718587; background:#182126; border-color:#28363c; }
        QScrollArea { border:none; background:transparent; }
        QScrollBar:vertical { background:transparent; width:8px; margin:0; }
        QScrollBar::handle:vertical { background:#40535b; min-height:28px; border-radius:4px; }
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }
        QHeaderView::section { background:#1a282e; color:#9fb2b5; padding:8px; border:none; }
        QLabel#preview { background:#0b1013; border:1px solid #28383d; border-radius:8px; color:#809699; }
    )");
}
}
