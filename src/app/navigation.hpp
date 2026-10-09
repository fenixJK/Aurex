#pragma once
#include <QWidget>
#include <functional>
#include <vector>

class QLabel;
class QVBoxLayout;
class QPushButton;
class QScrollArea;
namespace hive {
class Navigation final : public QWidget {
public:
    explicit Navigation(QWidget* parent=nullptr);
    void addItem(const QString& title);
    void insertItem(int index,const QString& title);
    void setModuleStart(int index);
    void addItems(const QStringList& titles);
    void removeItem(int index);
    void setItemVisible(int index,bool visible);
    void setCurrentRow(int index);
    int currentRow() const { return current_; }
    int count() const { return static_cast<int>(buttons_.size()); }
    QString title(int index) const;
    std::function<void(int)> on_selected;
private:
    QVBoxLayout* layout_;
    QScrollArea* scroll_;
    std::vector<QPushButton*> buttons_;
    int current_{-1};
    int module_start_{-1};
    QLabel* separator_{};
    void updateSeparator();
};
}
