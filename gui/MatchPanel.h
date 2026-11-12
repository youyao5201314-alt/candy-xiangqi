#ifndef MATCHPANEL_H
#define MATCHPANEL_H

#include <QWidget>
#include "api.h"

class QLabel;
class QPushButton;
class InternetClient;

// 互联网对战 = 纯随机快速匹配（好友邀请请走独立的「好友」入口）
class MatchPanel : public QWidget {
    Q_OBJECT
public:
    explicit MatchPanel(InternetClient *net, QWidget *parent = nullptr);

    void leavePage();   // 离开页面：若仍在排队则取消

signals:
    void back();
    void matched(const QString &roomId, const QString &side, const UserInfo &opponent);

private slots:
    void toggleQueue();
    void onMatchReady(const QString &roomId, const QString &side, const UserInfo &opponent);
    void onServerError(const QString &code, const QString &msg);

private:
    InternetClient *m_net;
    bool m_queued = false;
    QLabel *m_state = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_actionBtn = nullptr;

    void setQueued(bool q);
};

#endif  // MATCHPANEL_H
