#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPixmap>
#include <QThread>
#include <QJsonArray>
#include <vector>
#include "board.h"

class QPainter;
class QMouseEvent;
class QPaintEvent;
class QPoint;
class QSize;
class QPushButton;
class QComboBox;
class QTimer;
class QElapsedTimer;
class QSoundEffect;
class QLabel;
class QMovie;
class QSlider;
class QMediaPlayer;
class QAudioOutput;
class QTextEdit;
class QLineEdit;
class QFrame;
class QVBoxLayout;
class QNetworkAccessManager;
class NetworkManager;

// 对局名片信息（玩家昵称/头衔/头像；人机模式 isAi=true 表示糖果AI）
struct PlayerCardInfo {
    QString name;
    QString title;
    QString avatarUrl;   // 服务器头像相对路径，空=彩色默认头像
    int avatarSeed = 0;  // 默认头像取色种子（用账号 id）
    bool isAi = false;
    bool isSelf = false; // 是否本机玩家（名片名字后缀"（你）"）
    bool valid = false;  // 局域网对手名片未到时为 false（先显示占位）
};

// 棋盘显示控件：所有 QPainter 绘制和鼠标点击都经由它转发给 MainWindow
class BoardView : public QWidget
{
    Q_OBJECT
public:
    explicit BoardView(class MainWindow *win, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    MainWindow *m_win;
};

// AI 后台搜索线程：在棋盘【副本】上搜索，全程不碰 coreBoard，界面不卡死
class AiThread : public QThread
{
    Q_OBJECT
public:
    int m_board[10][9];   // 启动前在主线程拷贝好的棋盘
    int m_timeMs = 3000;  // 本手思考时长
    bool m_redSide = false; // AI 执哪方（玩家选后手时 AI 执红）
    Move m_result;        // 搜索结果（run 结束后有效）

    void run() override
    {
        setAiTimeLimitMs(m_timeMs);
        m_result = getAIMove(m_board, MAX_DEPTH, m_redSide);
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum class GameMode { SinglePlayer, Online };

    explicit MainWindow(GameMode mode = GameMode::SinglePlayer,
                        bool isHost = false,
                        NetworkManager *net = nullptr,
                        bool playerRed = true,
                        bool isSpectator = false,
                        int endgameIdx = -1,
                        int levelIdx = 0,
                        const PlayerCardInfo &self = PlayerCardInfo(),
                        const PlayerCardInfo &opponent = PlayerCardInfo(),
                        QWidget *parent = nullptr);
    ~MainWindow();

    // 供 BoardView 调用
    void renderBoard(QPainter &p, const QSize &viewSize);
    void handleBoardPress(const QPoint &pos, const QSize &viewSize, bool rightButton);

    // 本机登录昵称（StartScreen 登录后写入）：观战时只有 @我 的语音才出声，避免一堆背景音
    static QString s_localName;
    // 当前是否有打开的对局/观战窗（构造++析构--）：对局中私信不弹窗贴脸的可靠依据
    // （QApplication::activeWindow() 在安卓/瞬态窗口下不可靠，曾导致对局中私信弹窗贴脸）
    static bool gameWindowOpen() { return s_openGameWindows > 0; }
    // 机器人代打模式（命令行 --bot 启动）：压制终局弹窗，终局后自动关窗重开
    void setBotPlay(bool v) { m_botPlay = v; }
    // 跳过召唤动画直接开启奶娃挂机代打（机器人专用，红黑双方都可用）
    void enableBotAfk();
    // 中途加入观战：按服务器下发的着法历史从初始布局快放到当前局面（静音、不判胜负）
    void replayMoves(const QJsonArray &moves);
    // 程序内部清理窗口（不触发"关窗回主菜单"）：收摊观战窗/旧对局窗时用，
    // 避免 closeEvent 里 new StartScreen 把正在堆栈上执行的旧主菜单 delete 掉（野指针崩溃）
    void setSilentClose(bool v) { m_silentClose = v; }

private:
    static int s_openGameWindows;   // 打开中的对局/观战窗计数（见 gameWindowOpen）
    bool m_openCounted = false;     // 本窗是否已计入 s_openGameWindows（close 时--，析构兜底）

    // 落子滑动动画：走子落定后调用，棋子在棋盘上从起点滑向终点（约 160ms）
    void animateMove(const Move &mv);

private slots:
    void restartGame();   // 重新开局
    void undoMove();      // 悔棋（撤销 AI + 玩家各一手）
    void onAiFinished();  // AI 线程搜索结束，回主线程落子
    void onAiTick();      // 刷新思考计时显示
    void onAvatarTimer(); // 被吃子反应序列：愤怒脸 -> gif 动作 -> 思考/待机
    void resignGame();    // 投降：直接判负，对面大笑
    void onBgChanged(int index);     // 切换棋盘背景
    void onMusicChanged(int index);  // 切换背景音乐
    void onSideChanged(int index);   // 切换先手/后手（重开一局）
    void setSfxMuted(bool muted);    // 音乐开启时静音全部音效

    // ===== 联机模式 =====
    void onNetworkMoveReceived(int x1, int y1, int x2, int y2);  // 对方走了一步
    void onNetworkChatReceived(const QString &text);              // 收到对方聊天
    void onNetworkAvatar(const QString &key);                     // 收到房主端奶娃状态
    void onNetworkVersion(const QString &ver);                    // 收到对方版本号
    void onNetworkUndoRequest();                                  // 对方请求悔棋
    void onNetworkUndoAnswer(bool accepted);                      // 对方应答悔棋
    void onNetworkRestartRequest();                               // 对方请求重新开始
    void onNetworkRestartAnswer(bool accepted);                   // 对方应答重新开始
    void onNetworkBadMove();                                      // 收到非法走子（对方版本过旧？）
    void onNetworkResign();                                        // 对方投降（我方获胜）
    void onPeerDisconnected();                                     // 对方掉线
    void onNetworkProfile(const QString &name, const QString &title,
                          const QString &avatarUrl, bool senderRed); // 收到对方名片
    void onSpecChatReceived(const QString &fromName, const QString &fromAvatar,
                            const QString &text); // 观众/玩家发言（带头像）
    void onGameEndReceived(const QString &result);               // 对局终局结果
    void sendChatMessage();                                        // 发送聊天
    void sendUndoRequest();                                        // 联机：发送悔棋请求
    void sendRestartRequest();                                     // 联机：发送重新开始请求
    void summonDragon();                                           // 房主：召唤奶龙（登场动画+解锁挂机）
    void toggleAfk();          // 奶娃挂机 / 停止挂机 切换
    void onAfkFinished();      // 挂机奶娃搜索结束：替房主自动落子并发给对方
    void useCandy();           // 点击糖果指导按钮：弹菜单选糖果并消耗
    void goBackToMenu();       // 返回主菜单（关窗即回，联机未结束先确认）

protected:
    void closeEvent(QCloseEvent *event) override;   // 关窗：互联网对局通知服务器离开

private:
    // 对面（AI）虚拟形象的状态
    enum class AvatarState {
        Idle,       // 平时
        Selected,   // 玩家选中了棋子（观察你的操作）
        Thinking,   // AI 思考中
        Angry,      // 被玩家吃子：先愤怒脸
        Action,     // 被玩家吃子：再做 gif 动作
        GoodMove,   // 玩家将军/好棋：点赞
        UndoAsk,    // 玩家点悔棋：抱头震惊
        Listening,  // 音乐开启：沉浸听歌，锁定此表情且全部静音
        Lose,       // AI 输了（玩家赢）
        Win         // AI 赢了
    };
    // 一步棋的完整记录（悔棋恢复用）
    struct Step {
        Move move;        // 起止坐标
        int piece;        // 走动的棋子
        int captured;     // 被吃掉的棋子（0=没吃子）
        bool byRed;       // 是否红方（玩家）走的
    };

    BoardView *m_view = nullptr;
    // ===== 双方玩家名片（棋盘上=黑方，棋盘下=红方） =====
    PlayerCardInfo m_selfInfo;
    PlayerCardInfo m_oppInfo;
    QFrame *m_cardTop = nullptr;      // 黑方名片
    QFrame *m_cardBottom = nullptr;   // 红方名片
    QLabel *m_topAvatar = nullptr, *m_topName = nullptr, *m_topTitle = nullptr, *m_topTag = nullptr;
    QLabel *m_botAvatar = nullptr, *m_botName = nullptr, *m_botTitle = nullptr, *m_botTag = nullptr;
    QNetworkAccessManager *m_cardNam = nullptr;
    void buildPlayerCards(QWidget *parent, QVBoxLayout *root);  // 构造时建名片
    void applySideProfile(bool redSide, const PlayerCardInfo &info);  // 按执子方填名片
    void fillCard(bool redSide, const PlayerCardInfo &info);          // 刷新一侧名片
    // 按位置填名片（bottom=true 棋盘下）：本机视角自己永远在下，卡片底色/阵营标按实际执子方
    void fillCardPos(bool bottom, const PlayerCardInfo &info, bool infoRed);
    void loadCardAvatar(QLabel *lbl, const QString &url,
                        const QString &name, int seed);               // 异步下载/默认头像
    static QPixmap makeDefaultAvatar(const QString &name, int seed, int size);
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_restartBtn = nullptr;
    QPushButton *m_undoBtn = nullptr;
    QPushButton *m_resignBtn = nullptr;
    bool m_musicOn = false;                // 背景音乐是否开启（开启时锁定听歌脸+静音）
    QComboBox *m_levelCombo = nullptr;
    QComboBox *m_bgCombo = nullptr;      // 背景选择
    QComboBox *m_musicCombo = nullptr;   // 音乐选择
    QComboBox *m_sideCombo = nullptr;    // 先手/后手选择
    QSlider *m_volSlider = nullptr;      // 音量
    QMediaPlayer *m_musicPlayer = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    bool m_playerRed = true;             // 玩家是否执红（先手）
    bool m_isSpectator = false;          // 观战模式：看棋盘但不能走子
    int m_endgameIdx = -1;               // 残局索引（-1=普通开局，0..6=残局挑战）
    int m_endgameMoves = 0;              // 残局：玩家已走步数（超上限判负）
    // ===== 联机模式 =====
    GameMode m_mode = GameMode::SinglePlayer;
    bool m_isHost = false;               // 联机时是否房主（房主=红方先手）
    bool m_myTurn = true;                // 联机时是否轮到本机走子
    NetworkManager *m_net = nullptr;
    QPushButton *m_summonBtn = nullptr;  // 房主专属：召唤奶龙（首次登场）
    QPushButton *m_afkBtn = nullptr;     // 房主专属：奶娃挂机 / 停止挂机（召唤后出现）
    QPushButton *m_candyBtn = nullptr;   // 糖果指导按钮（消耗品，可让 AI 代走一步）
    bool m_dragonSummoned = false;       // 奶娃是否已登场
    bool m_afk = false;                  // 是否正在挂机（奶娃替房主打）
    QTextEdit *m_chatView = nullptr;     // 聊天消息显示
    QLineEdit *m_chatInput = nullptr;    // 聊天输入框
    bool m_waitingUndo = false;          // 已发悔棋请求，等对方答复
    bool m_waitingRestart = false;       // 已发重开请求，等对方答复
    int m_bgIndex = 0;                   // 背景：0=木质 1=蓝发 2=魔龙 3=自定义
    QPixmap m_bgPixmap;                  // 背景图缓存（避免每次 paint 重新解码）
    QString m_customBgPath;              // 自定义背景图路径（exe 旁 assets/）
    QString m_customMusicPath;           // 自定义音乐路径（exe 旁 assets/）
    int m_musicSel = 0;                  // 音乐下拉框上次确认的选择（取消自定义时回退用）
    QTimer *m_animTimer = nullptr;    // 将帅被将军的呼吸动画
    QTimer *m_thinkTimer = nullptr;   // 思考计时刷新
    QTimer *m_afkTimer = nullptr;     // 挂机走子延时触发（单次，替代 singleShot 防堆叠）
    QElapsedTimer *m_thinkClock = nullptr;
    AiThread *m_aiThread = nullptr;
    qreal m_animPhase = 0.0;          // 将军呼吸动画相位

    int m_selRow = -1;              // 当前选中棋子的行（0=最上方黑方，9=最下方红方）
    int m_selCol = -1;              // 当前选中棋子的列（0..8）
    std::vector<Move> m_hints;      // 选中棋子的全部合法落点（绘制提示点用）

    Move m_last;                    // 最近一步棋
    bool m_hasLast = false;

    // 落子滑动动画状态：paint 时跳过终点静态棋子，改画滑动中的棋子
    Move m_moveAnim;                // 当前动画的着法（起/止）
    int m_moveAnimPiece = 0;        // 滑动中的棋子代码
    int m_moveAnimFrame = 0;        // 当前帧
    bool m_moveAnimActive = false;
    QTimer *m_moveAnimTimer = nullptr;
    static const int kMoveAnimFrames = 10;   // 10 帧 × 16ms ≈ 160ms

    // 棋子字体缓存：renderBoard 60fps 动画时每次创建 QFont 有开销，
    // 按 cell 像素尺寸缓存，窗口 resize 时才重建
    mutable QFont m_pieceFont;
    mutable int m_pieceFontCell = 0;   // 缓存字体对应的 cell 像素宽

    std::vector<Step> m_history;    // 对局走子历史（悔棋用）
    bool m_gameOver = false;        // 对局是否已结束（结束后锁定点击）
    bool m_moneySettled = false;    // 本局货币是否已结算（防重复发放）
    bool m_resultReported = false;  // 互联网终局是否已上报服务器（防重复）
    bool m_aiThinking = false;      // AI 是否正在思考
    bool m_redInCheck = false;      // 红方（玩家）正在被将军
    bool m_blackInCheck = false;    // 黑方（AI）正在被将军

    void selectPiece(int row, int col);
    void clearSelection();
    void refreshHints();
    // 残局挑战：状态栏进度 + 失败后统一询问（再试一次/返回菜单）
    void updateEndgameStatus();
    void endgameMaybeRetry(const QString &reason);
    // 逻辑坐标 -> 显示坐标（玩家执黑时棋盘旋转 180°）
    void displayPt(int row, int col, int &dr, int &dc) const;
    // 显示坐标 -> 逻辑坐标
    void logicalPt(int row, int col, int &lr, int &lc) const;
    void revertStep(const Step &s);     // 在 coreBoard 上撤销一步
    void startAiThinking();             // 拷贝棋盘并启动后台搜索线程
    void startAfkMove();                // 挂机：奶娃替房主想一步并自动落子
    void playAfkCaptureAction(bool useNewGif);  // 挂机吃子：随机播新/旧打人动图
    void updateCheckState();            // 刷新双方被将军状态 + 状态栏 + 将军音效
    void refreshButtons();
    void initSounds();                  // 创建音效对象（构造时）
    void loadSoundSources();            // 绑定音效资源（窗口显示后延迟执行，避免卡点击）
    void ensureMusicPlayer();           // 背景音乐播放器懒加载（创建耗时数秒，延后到窗口显示后）
    void loadBgPixmap();                // 按 m_bgIndex 加载/刷新背景图缓存
    void loadCustomAssets();            // 启动时从 QSettings 恢复自定义背景/音乐
    QString copyToAssets(const QString &src, const QString &tag); // 复制用户文件到 exe 旁 assets/
    void initAvatar();                  // 初始化对面虚拟形象面板
    void setAvatar(AvatarState s);      // 切换形象图片/动图 + 心情文字
    void playCaptureReaction();         // 玩家吃掉 AI 棋子时的愤怒->动作序列
    QString stateKey(AvatarState s) const;  // 形象状态 -> 同步协议关键字
    void syncAvatar(const QString &key);    // 房主：奶娃状态广播给对面（其他情况空操作）
    void setPeerAvatar(const QString &key); // 加入者：按房主同步的状态渲染帮手条
    void performOnlineUndo(bool requesterIsMe);  // 联机悔棋通过后：双方同步回退到请求方上一步之前
    void applyOnlineRestart();                   // 联机重开：双方同步重置棋盘
    // 货币结算（仅单机模式）：outcome 1=赢(+2000) 0=输(+500) 2=平(+1000)，返回实际发放金额
    int settleMoney(int outcome);
    // 胜利成就统计：按模式/残局记账并识别杀法称号，解锁时弹窗（killName 空=非将死杀法）
    void reportWinAchievements(const QString &killName);
    // ===== 机器人代打 =====
    bool m_botPlay = false;              // 机器人模式：压制弹窗，终局自动关窗重开
    bool m_silentClose = false;          // 程序内部清理关窗：closeEvent 不拉起新主菜单
    QTimer *m_botChatTimer = nullptr;    // 机器人随机闲聊定时器
    void botRestartLoop();               // 终局后稍等关窗（进程退出由编排脚本重启）
    static QString botChatLine();        // 随机闲聊语料（表情标记/短语）

    // 女号语音：消息前缀 [voice:xxx] 触发播放（sounds/voice/xxx.wav，懒加载池）
    // ambient=true（观战席/房间闲聊）：只有提到本机昵称才出声，避免一堆背景音
    void maybePlayVoice(QString &text, bool ambient);  // 检测并摘除前缀、播放；无标记不动
    QHash<QString, QSoundEffect*> m_voiceFx;
    // ===== 糖果指导（消耗品）=====
    bool candyAllowed() const;          // 当前模式是否允许用糖果（单机/局域网房客/互联网）
    int candyCount(int idx) const;      // 糖果剩余数量
    void refreshCandyButton();          // 刷新糖果按钮文字和可用状态
    void startCandyMove(int idx);       // 消耗糖果并启动 AI 代走
    void onCandyFinished();             // 糖果 AI 搜索结束：代我落子

    QLabel *m_avatarImg = nullptr;      // 形象图片/动图显示区
    QLabel *m_avatarMood = nullptr;     // 形象心情文字
    QMovie *m_avatarMovie = nullptr;    // 被吃子后的 gif 动作（原版打人）
    QMovie *m_avatarMovie2 = nullptr;   // 挂机奶娃吃子时的 gif 动作（新版出拳）
    QTimer *m_avatarTimer = nullptr;    // 反应序列定时器（单次触发）
    AvatarState m_avatarState = AvatarState::Idle;
    QFrame *m_avatarPanel = nullptr;    // 右侧形象卡片（联机加入者隐藏）

    // 联机加入者：底部“奶娃帮手”条（房主召唤奶娃后出现，状态由房主同步）
    QWidget *m_peerBar = nullptr;
    QLabel *m_peerImg = nullptr;
    QLabel *m_peerMood = nullptr;
    QMovie *m_peerMovie = nullptr;      // 48px 原版打人 gif
    QMovie *m_peerMovie2 = nullptr;     // 48px 新版出拳 gif

    QSoundEffect *m_sndSelect = nullptr;      // 选中棋子（双方共用）
    QSoundEffect *m_sndMove = nullptr;        // 普通落子（双方共用）
    QSoundEffect *m_sndCapture = nullptr;     // 吃子（黑方/对面专用，来自视频）
    QSoundEffect *m_sndCapture2 = nullptr;    // 吃子（挂机奶娃随机音效②，视频2提取）
    QSoundEffect *m_sndCaptureRed = nullptr;  // 吃子（红方/我方专用，原版合成音）
    QSoundEffect *m_sndCheck = nullptr;       // 将军（黑方/对面专用，来自视频）
    QSoundEffect *m_sndCheckRed = nullptr;    // 将军（红方/我方专用，原版合成音）
    QSoundEffect *m_sndWin = nullptr;         // 获胜（红方胜利时放）
    QSoundEffect *m_sndLose = nullptr;        // 落败（黑方胜利时放，来自视频，也是投降时的大笑）
    bool m_wasRedInCheck = false;             // 上一轮红方是否被将军（边沿触发）
    bool m_wasBlackInCheck = false;           // 上一轮黑方是否被将军（边沿触发）
};

#endif // MAINWINDOW_H
