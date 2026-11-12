#ifndef BAGDIALOG_H
#define BAGDIALOG_H

#include <QDialog>

// 背包：糖果余额 / 货币 / 道具（mimimimi）/ 好友送的礼物
class BagDialog : public QDialog {
    Q_OBJECT
public:
    explicit BagDialog(QWidget *parent = nullptr);

private:
    void rebuild();
};

#endif  // BAGDIALOG_H
