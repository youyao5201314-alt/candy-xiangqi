#ifndef STARTSCREEN_H
#define STARTSCREEN_H

#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QJsonArray>
#include "api.h"
#include "MainWindow.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QListWidget;
class NetworkManager;
class ApiClient;
class InternetClient;
class FriendsPanel;
class QMediaPlayer;
class QAudioOutput;
class ChatDialog;
class QTimer;
class QMessageBox;

// 开始菜单：选"开始游戏"（单机）或"联机模式"（局域网）或"互联网模式"（账号+好友）
// 阶段 3.5：启动强制登录，登录成功才显示主菜单
class StartScreen : public QWidget
{
    Q_OBJECT
public:
    explicit StartScreen(QWidget *parent = nullptr);
    ~StartScreen() override;

    // 机器人模式配置（main.cpp 解析 --bot <user> <pass> <red|black> <oppId> 后设置）：
    // 无人值守的女号对局机器人，红方主动邀请 oppId，黑方等待邀请，进局即挂机代打
    static bool s_botMode;
    static QString s_botUser;
    static QString s_botPass;
    static QString s_botRole;
    static QString s_botOppId;

    // 常驻主菜单实例：MainWindow 关闭时 show() 它，而不是 new 新实例
    // （避免 new StartScreen 触发构造函数 delete 旧实例的野指针崩溃）
    static StartScreen *s_liveMenu;

private slots:
    void onStartSingle();          // 开始游戏 → 单机
    void onPickEndgame();          // 残局挑战 → 选择残局（含说明）
    void onShowOnline();           // 联机模式 → 显示创建/加入
    void onBackToMenu();           // 返回主菜单
    void onCreateRoom();           // 创建房间
    void onJoinPage();             // 进入加入房间页（开始扫描）
    void onJoinRoom();             // 加入房间（列表选择或输入房间号）
    void onSpectateJoin();         // 观战加入房间
    void onRoomFound(const QString &code);  // 扫描到一个房间
    void onRoomsCleared();
    void onConnected();            // 双方连上了 → 进游戏
    void onSideReceived(bool hostIsRed);  // 加入者收到执方信息 → 进游戏
    void onSpectatorConfirmed();         // 观战者收到 SPEC 确认 → 进游戏
    void onNetError(const QString &msg);
    void onInternetMode();         // 互联网模式 → 快速匹配陌生人（纯随机匹配）
    void onFriends();              // 独立好友入口（聊天/送礼/邀请对战）
    void onEditServerUrl();        // 主菜单手动修改服务器地址
    void onInternetClosed();       // 实时通道断开：好友页提示，其余静默重连
    void onInternetLogoutCleanup();// 移除好友页
    void onInternetMatched(const QString &roomId, const QString &side,
                           const struct UserInfo &opponent);  // 匹配成功 → 进对局
    void onSpectateStarted(const QString &roomId, const struct UserInfo &red,
                           const struct UserInfo &black,
                           const QJsonArray &moves);          // 观战就位 → 打开观战窗（带着法历史回放）
    void botTick();               // 机器人红方定时器：没对局就向 oppId 发邀请

    // 全局实时消息（好友页未打开时也能收私信/礼物）
    void onGlobalDm(int fromUserId, const QString &fromName, const QString &fromAvatar,
                    const QString &text);
    void onGlobalGift(int fromUserId, const QString &fromName, const QString &text);
    void onGlobalFriendRequest(int requestId, const struct UserInfo &from,
                               const QString &message);
    // 全局对战邀请：任意页面（含对局中）都能收到，弹窗应答；红点挂「互联网对战」按钮
    void onGlobalInvite(int fromUserId, const QString &fromName, const QString &fromAvatar);
    void onGlobalInviteDeclined(int fromUserId);
    void ensureInternet();        // 登录后创建并常连 WebSocket（回主菜单也不掉线）
    void scheduleWsReconnect();   // 意外断线后自动重连
    PlayerCardInfo selfCard() const;  // 当前账号 → 对局名片
    void refreshBadges();         // 刷新邮件/好友未读红点

    // 强制登录流程
    void bootstrapAuth();          // 构造后 singleShot 调用：尝试自动登录
    void showForcedLogin();        // 无登录态：弹强制登录框
    void enterMenu();              // 登录成功：进入主菜单
    void onLogout();               // 退出登录 → 清空状态 → 回登录框
    void onOpenProfile();          // 点"个人资料"按钮

protected:
    void showEvent(QShowEvent *) override;   // 窗口显示：开则播背景音乐
    void hideEvent(QHideEvent *) override;   // 窗口隐藏（进对局/被替换）：停音乐
    void resizeEvent(QResizeEvent *) override;  // 重排红点位置

private:
    NetworkManager *m_net = nullptr;
    ApiClient *m_api = nullptr;     // REST 客户端（互联网账号用）
    InternetClient *m_internet = nullptr;  // WebSocket 实时通道
    FriendsPanel *m_friendsPage = nullptr; // stack 中的好友页
    bool m_isHost = false;
    bool m_isSpectator = false;     // 观战模式
    bool m_hostIsRed = true;       // 房主随机执方（true=红，false=黑）
    QString m_roomCode;
    MainWindow *m_game = nullptr;
    MainWindow *m_spectateGame = nullptr;  // 互联网实时观战窗（进对局前先关闭）
    QTimer *m_botTimer = nullptr;          // 机器人红方邀请定时器
    bool m_botTargetFetching = false;      // 邀约机器人动态目标请求进行中（防止并发重入）
    QStackedWidget *m_stack = nullptr;

    // 登录状态
    UserInfo m_currentUser;
    QString m_accessToken;
    QString m_refreshToken;

    // 主菜单用户条
    QWidget *m_userBar = nullptr;
    QLabel *m_userAvatar = nullptr;
    QLabel *m_userName = nullptr;
    QLabel *m_userTitle = nullptr;   // 头衔（全服第一/第二/最拉）
    QPushButton *m_profileBtn = nullptr;
    QPushButton *m_logoutBtn = nullptr;

    // 主菜单
    QPushButton *m_singleBtn = nullptr;
    QPushButton *m_onlineBtn = nullptr;
    QPushButton *m_internetBtn = nullptr;  // 互联网模式按钮
    QPushButton *m_friendsBtn = nullptr;   // 独立好友入口
    QPushButton *m_mailBtn = nullptr;      // 邮件按钮
    QLabel *m_mailBadge = nullptr;         // 未领邮件红点
    int m_serverMailUnread = 0;            // 服务端未领系统邮件数
    QLabel *m_friendsBadge = nullptr;      // 好友未读消息红点
    QLabel *m_inviteBadge = nullptr;       // 待回应对战邀请红点（挂互联网对战按钮）
    QSet<int> m_pendingInvites;            // 待回应邀请的发起者 id
    QHash<int, QString> m_pendingInviteNames;
    QHash<int, QString> m_pendingInviteAvatars;              // 邀请者头像 URL（弹窗显示）
    QHash<QString, QPixmap> m_inviteAvatarCache;             // 弹窗头像缓存（完整URL→原图）
    QHash<QString, QPointer<QMessageBox>> m_inviteBoxByUrl;  // 下载完成后待热更新的邀请框

    // 联机选择
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_joinBtn = nullptr;
    QPushButton *m_backBtn = nullptr;

    // 加入房间
    QListWidget *m_roomList = nullptr;
    QLineEdit *m_codeInput = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_spectateBtn = nullptr;  // 观战加入按钮

    // 开始界面背景音乐（qrc 内嵌 wav，循环播放，可关闭）
    void ensureBgmPlayer();        // 懒创建播放器（性能铁律：不在构造里 new）
    void startBgm();
    void stopBgm();
    void updateBgmBtnText();

    // 打开好友页（allowInvite=true 互联网模式可邀请对战；false 独立好友简洁模式）
    void openFriendsPage(bool allowInvite);
    void promptInvite(int fromUserId);   // 弹出对战邀请应答框（收到邀请与红点重提共用）
    QMediaPlayer *m_bgmPlayer = nullptr;
    QAudioOutput *m_bgmAudio = nullptr;
    QPushButton *m_bgmBtn = nullptr;
    bool m_bgmOn = true;

    // WS 常连状态：登录后一直保持（含进对局期间），退出登录才关闭
    bool m_wsWanted = false;
    QTimer *m_wsRetry = nullptr;                 // 断线自动重连
    QHash<int, ChatDialog*> m_globalChats;       // 主菜单全局私信窗口

    static quint16 roomPort(const QString &code);
};

#endif // STARTSCREEN_H
