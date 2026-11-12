#include <QApplication>
#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QStringList>
#include "MainWindow.h"
#include "StartScreen.h"
#include "ServerConfig.h"

#ifndef Q_OS_ANDROID
// 自绘一枚放大的箭头光标：白色箭头 + 粗黑描边，在任何棋盘背景上都醒目。
// 位图按逻辑像素制作，Qt 会按屏幕 DPI 自动生成高清版本。
// （手机上没有鼠标，Android 分支不使用此光标）
static QCursor makeLargeArrowCursor()
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);

    // 经典箭头轮廓（基准为系统 32px 光标形状），放大 1.4 倍并留 2.5px 边距
    const QPointF m(2.5, 2.5);
    const qreal k = 1.4;
    QPolygonF arrow;
    arrow << m
          << m + QPointF(0.0 * k, 24.0 * k)
          << m + QPointF(7.0 * k, 18.0 * k)
          << m + QPointF(11.0 * k, 28.0 * k)
          << m + QPointF(15.0 * k, 26.5 * k)
          << m + QPointF(11.0 * k, 16.5 * k)
          << m + QPointF(20.0 * k, 16.5 * k);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0, 0, 0, 235), 3.0);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(QColor(255, 255, 255, 250));
    p.drawPolygon(arrow, Qt::WindingFill);
    p.end();

    return QCursor(pm, 3, 3);   // 热点对准箭头尖
}
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // QSettings 默认构造依赖 org/app 名，不设会写到 "Unknown Organization" 导致读不到
    QApplication::setOrganizationName(QStringLiteral("candyboard"));
    QApplication::setApplicationName(QStringLiteral("gui"));
#ifndef Q_OS_ANDROID
    QApplication::setOverrideCursor(makeLargeArrowCursor());  // 全局大光标
#endif
    // 必须堆分配：StartScreen 构造函数会把自身地址记入 s_liveMenu，
    // 之后回主菜单时新实例会 delete 旧实例——栈对象被 delete 会直接堆崩溃
    // 机器人模式：--bot <user> <pass> <red|black> <oppId>
    // （无人值守女号对局：红方定时邀请 oppId，黑方等邀请，进局挂机代打，无窗口运行）
    const QStringList args = app.arguments();
    const int botIdx = args.indexOf(QStringLiteral("--bot"));
    if (botIdx > 0 && botIdx + 4 < args.size()) {
        // 注册表命名空间隔离：机器人配置写到 gui-bot，不污染玩家配置（尤其 server/url）
        QApplication::setApplicationName(QStringLiteral("gui-bot"));
        StartScreen::s_botMode = true;
        StartScreen::s_botUser = args.at(botIdx + 1);
        StartScreen::s_botPass = args.at(botIdx + 2);
        StartScreen::s_botRole = args.at(botIdx + 3).toLower();
        StartScreen::s_botOppId = args.at(botIdx + 4);
    }
    // 机器人服务器地址：--bot-url http://127.0.0.1:8080（写进隔离后的 gui-bot 命名空间）
    const int urlIdx = args.indexOf(QStringLiteral("--bot-url"));
    if (urlIdx > 0 && urlIdx + 1 < args.size())
        ServerConfig::setServerUrl(args.at(urlIdx + 1));
    StartScreen *start = new StartScreen;
    if (StartScreen::s_botMode)
        return app.exec();   // 机器人不显示主菜单窗口，事件循环驱动即可
#ifdef Q_OS_ANDROID
    start->showMaximized();
#else
    start->show();
#endif
    return app.exec();
}
