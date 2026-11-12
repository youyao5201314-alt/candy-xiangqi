#ifndef MAILDIALOG_H
#define MAILDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;

// 邮件对话框：新手礼包邮件，可领取 10000￥ 启动资金（一次性）
class MailDialog : public QDialog {
    Q_OBJECT
public:
    explicit MailDialog(QWidget *parent = nullptr);

    static bool welcomeClaimed();   // 新手礼包是否已领取（主菜单红点用）

private:
    void setupUi();
    void refresh();
    void claim();        // 领取金币
    bool claimed() const;

    QLabel *m_stateLbl = nullptr;
    QPushButton *m_claimBtn = nullptr;
};

#endif // MAILDIALOG_H
