#include <QApplication>
#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include "MainWindow.h"
#include "StartScreen.h"

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
#ifndef Q_OS_ANDROID
    QApplication::setOverrideCursor(makeLargeArrowCursor());  // 全局大光标
#endif
    StartScreen start;
#ifdef Q_OS_ANDROID
    start.showMaximized();
#else
    start.show();
#endif
    return app.exec();
}
