#ifndef SHOPDIALOG_H
#define SHOPDIALOG_H

#include <QDialog>
#include <QMap>

class QLabel;
class QPushButton;

// 糖果商城对话框：用对局货币购买糖果（消耗品，对局中可让 AI 代走一步）
class ShopDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShopDialog(QWidget *parent = nullptr);

private:
    void setupUi();
    void refresh();              // 刷新余额和库存显示
    void buy(int idx);           // 购买糖果（+1）
    int money() const;           // 读取当前余额
    int candyCount(int idx) const; // 读取糖果剩余数量

    QLabel *m_balanceLbl = nullptr;
    QMap<int, QLabel*> m_countLbls;
};

#endif // SHOPDIALOG_H
