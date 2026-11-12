#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPixmap>
#include <QThread>
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
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 供 BoardView 调用
    void renderBoard(QPainter &p, const QSize &viewSize);
    void handleBoardPress(const QPoint &pos, const QSize &viewSize, bool rightButton);

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
    int m_bgIndex = 0;                   // 背景：0=木质 1=蓝发 2=魔龙 3=自定义
    QPixmap m_bgPixmap;                  // 背景图缓存（避免每次 paint 重新解码）
    QString m_customBgPath;              // 自定义背景图路径（exe 旁 assets/）
    QString m_customMusicPath;           // 自定义音乐路径（exe 旁 assets/）
    int m_musicSel = 0;                  // 音乐下拉框上次确认的选择（取消自定义时回退用）
    QTimer *m_animTimer = nullptr;    // 将帅被将军的呼吸动画
    QTimer *m_thinkTimer = nullptr;   // 思考计时刷新
    QElapsedTimer *m_thinkClock = nullptr;
    AiThread *m_aiThread = nullptr;
    qreal m_animPhase = 0.0;          // 将军呼吸动画相位

    int m_selRow = -1;              // 当前选中棋子的行（0=最上方黑方，9=最下方红方）
    int m_selCol = -1;              // 当前选中棋子的列（0..8）
    std::vector<Move> m_hints;      // 选中棋子的全部合法落点（绘制提示点用）

    Move m_last;                    // 最近一步棋
    bool m_hasLast = false;

    std::vector<Step> m_history;    // 对局走子历史（悔棋用）
    bool m_gameOver = false;        // 对局是否已结束（结束后锁定点击）
    bool m_aiThinking = false;      // AI 是否正在思考
    bool m_redInCheck = false;      // 红方（玩家）正在被将军
    bool m_blackInCheck = false;    // 黑方（AI）正在被将军

    void selectPiece(int row, int col);
    void clearSelection();
    void refreshHints();
    // 逻辑坐标 -> 显示坐标（玩家执黑时棋盘旋转 180°）
    void displayPt(int row, int col, int &dr, int &dc) const;
    // 显示坐标 -> 逻辑坐标
    void logicalPt(int row, int col, int &lr, int &lc) const;
    void revertStep(const Step &s);     // 在 coreBoard 上撤销一步
    void startAiThinking();             // 拷贝棋盘并启动后台搜索线程
    void updateCheckState();            // 刷新双方被将军状态 + 状态栏 + 将军音效
    void refreshButtons();
    void initSounds();                  // 预加载全部音效（构造时一次性完成）
    void loadBgPixmap();                // 按 m_bgIndex 加载/刷新背景图缓存
    void loadCustomAssets();            // 启动时从 QSettings 恢复自定义背景/音乐
    QString copyToAssets(const QString &src, const QString &tag); // 复制用户文件到 exe 旁 assets/
    void initAvatar();                  // 初始化对面虚拟形象面板
    void setAvatar(AvatarState s);      // 切换形象图片/动图 + 心情文字
    void playCaptureReaction();         // 玩家吃掉 AI 棋子时的愤怒->动作序列

    QLabel *m_avatarImg = nullptr;      // 形象图片/动图显示区
    QLabel *m_avatarMood = nullptr;     // 形象心情文字
    QMovie *m_avatarMovie = nullptr;    // 被吃子后的 gif 动作
    QTimer *m_avatarTimer = nullptr;    // 反应序列定时器（单次触发）
    AvatarState m_avatarState = AvatarState::Idle;

    QSoundEffect *m_sndSelect = nullptr;      // 选中棋子（双方共用）
    QSoundEffect *m_sndMove = nullptr;        // 普通落子（双方共用）
    QSoundEffect *m_sndCapture = nullptr;     // 吃子（黑方/对面专用，来自视频）
    QSoundEffect *m_sndCaptureRed = nullptr;  // 吃子（红方/我方专用，原版合成音）
    QSoundEffect *m_sndCheck = nullptr;       // 将军（黑方/对面专用，来自视频）
    QSoundEffect *m_sndCheckRed = nullptr;    // 将军（红方/我方专用，原版合成音）
    QSoundEffect *m_sndWin = nullptr;         // 获胜（红方胜利时放）
    QSoundEffect *m_sndLose = nullptr;        // 落败（黑方胜利时放，来自视频，也是投降时的大笑）
    bool m_wasRedInCheck = false;             // 上一轮红方是否被将军（边沿触发）
    bool m_wasBlackInCheck = false;           // 上一轮黑方是否被将军（边沿触发）
};

#endif // MAINWINDOW_H
