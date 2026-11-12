#ifndef MAILDIALOG_H
#define MAILDIALOG_H

#include <QDialog>
#include <QList>
#include "api.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class ApiClient;

// 邮件对话框：
// 1) 本地新手礼包邮件（一次性 10000￥，游客/未登录也能领）
// 2) 服务端系统邮件（管理员经 DataGrip 写 mails 表下发，可带货币/糖果附件）
class MailDialog : public QDialog {
    Q_OBJECT
public:
    explicit MailDialog(QWidget *parent = nullptr,
                        ApiClient *api = nullptr,
                        const QString &accessToken = QString());

    static bool welcomeClaimed();   // 新手礼包是否已领取（主菜单红点用）

private slots:
    void onMails(const QList<MailInfo> &mails);
    void onClaimOk(int mailId, int money, int candy0, int candy1, int candy2);

private:
    void setupUi();
    void refresh();          // 本地新手礼包卡状态
    void claim();            // 领取本地新手礼包
    bool claimed() const;
    void rebuildServerMails();   // 重建服务端邮件卡片区
    static QString attachText(const QString &kind, int amount);

    QLabel *m_stateLbl = nullptr;
    QPushButton *m_claimBtn = nullptr;

    // 服务端邮件
    ApiClient *m_api = nullptr;
    QString m_token;
    QList<MailInfo> m_mails;
    QVBoxLayout *m_serverList = nullptr;   // 卡片容器
    QLabel *m_serverEmpty = nullptr;       // 无邮件占位
};

#endif // MAILDIALOG_H
