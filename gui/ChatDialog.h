#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include <QHash>

class QTextEdit;
class QLineEdit;
class QPushButton;
class InternetClient;

// 好友私信对话框（与某位好友的一对一聊天，支持奶龙表情）
class ChatDialog : public QDialog {
    Q_OBJECT
public:
    ChatDialog(InternetClient *net, int friendId, const QString &friendName,
               QWidget *parent = nullptr);

    int friendId() const { return m_friendId; }

    // 收到该好友的私信（可能在对话框未激活时由 FriendsPanel 转发）
    void receiveMessage(const QString &fromName, const QString &text);

private:
    void sendText(const QString &text);   // 发送一条消息（文本或表情标记）

    InternetClient *m_net;
    int m_friendId;
    QString m_friendName;
    QTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
};

#endif // CHATDIALOG_H
