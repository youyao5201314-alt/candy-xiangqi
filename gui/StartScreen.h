#ifndef STARTSCREEN_H
#define STARTSCREEN_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QListWidget;
class NetworkManager;
class MainWindow;

// 开始菜单：选"开始游戏"（单机）或"联机模式"（创建/加入房间）
class StartScreen : public QWidget
{
    Q_OBJECT
public:
    explicit StartScreen(QWidget *parent = nullptr);

private slots:
    void onStartSingle();          // 开始游戏 → 单机
    void onShowOnline();           // 联机模式 → 显示创建/加入
    void onBackToMenu();           // 返回主菜单
    void onCreateRoom();           // 创建房间
    void onJoinPage();             // 进入加入房间页（开始扫描）
    void onJoinRoom();             // 加入房间（列表选择或输入房间号）
    void onRoomFound(const QString &code);  // 扫描到一个房间
    void onRoomsCleared();
    void onConnected();            // 双方连上了 → 进游戏
    void onNetError(const QString &msg);

private:
    NetworkManager *m_net = nullptr;
    bool m_isHost = false;
    QString m_roomCode;
    MainWindow *m_game = nullptr;
    QStackedWidget *m_stack = nullptr;

    // 主菜单
    QPushButton *m_singleBtn = nullptr;
    QPushButton *m_onlineBtn = nullptr;

    // 联机选择
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_joinBtn = nullptr;
    QPushButton *m_backBtn = nullptr;

    // 加入房间
    QListWidget *m_roomList = nullptr;
    QLineEdit *m_codeInput = nullptr;
    QLabel *m_statusLabel = nullptr;

    static quint16 roomPort(const QString &code);
};

#endif // STARTSCREEN_H
