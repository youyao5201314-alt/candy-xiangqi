#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include <QHash>

class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class InternetClient;
class ApiClient;

// 好友私信对话框（与某位好友的一对一聊天，支持奶龙表情）
// 聊天记录按（我的id, 好友id）持久化到 QSettings，重开窗口可见历史
// 顶部显示对方圆形头像 + 昵称名片条
class ChatDialog : public QDialog {
    Q_OBJECT
public:
    ChatDialog(InternetClient *net, int myId, int friendId,
               const QString &friendName, QWidget *parent = nullptr,
               const QString &avatarUrl = QString(), ApiClient *api = nullptr);

    int friendId() const { return m_friendId; }

    // 收到该好友的私信（可能在对话框未激活时由 FriendsPanel 转发）
    void receiveMessage(const QString &fromName, const QString &text);

private:
    void sendText(const QString &text);   // 发送一条消息（文本或表情标记）
    void loadHistory();                   // 启动时回放历史记录
    void saveMessage(bool mine, const QString &name, const QString &text);

    InternetClient *m_net;
    int m_myId;
    int m_friendId;
    QString m_friendName;
    QString m_avatarUrl;                  // 对方头像完整 URL（空=无自定义头像）
    QLabel *m_avatarLbl = nullptr;
    QTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
};

#endif // CHATDIALOG_H
