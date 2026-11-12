#include "ProfileViewDialog.h"

#include "ServerConfig.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

ProfileViewDialog::ProfileViewDialog(ApiClient *api, const QString &accessToken,
                                     int userId, QWidget *parent)
    : QDialog(parent), m_api(api), m_token(accessToken), m_userId(userId)
{
    Q_UNUSED(m_token);
    setWindowTitle(QStringLiteral("好友资料"));
    resize(400, 420);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 16);
    v->setSpacing(10);
    v->addSpacing(30);   // 顶部留白（背景图由 paintEvent 铺满全窗）

    m_avatarLbl = new QLabel(this);
    m_avatarLbl->setFixedSize(88, 88);
    m_avatarLbl->setAlignment(Qt::AlignCenter);
    m_avatarLbl->setStyleSheet(
        "border:3px solid white;border-radius:44px;background:#dee2e6;color:#868e96;");
    m_avatarLbl->setText(QStringLiteral("头像"));
    v->addWidget(m_avatarLbl, 0, Qt::AlignCenter);

    m_nameLbl = new QLabel(QStringLiteral("加载中…"), this);
    m_nameLbl->setAlignment(Qt::AlignCenter);
    m_nameLbl->setStyleSheet(QStringLiteral(
        "font-size:22px;font-weight:bold;color:#333;"));
    v->addWidget(m_nameLbl);

    m_titleLbl = new QLabel(this);
    m_titleLbl->setAlignment(Qt::AlignCenter);
    m_titleLbl->setStyleSheet(QStringLiteral(
        "font-size:15px;font-weight:bold;color:#9c36b5;"));
    v->addWidget(m_titleLbl);

    m_idLbl = new QLabel(this);
    m_idLbl->setAlignment(Qt::AlignCenter);
    m_idLbl->setStyleSheet(QStringLiteral("font-size:13px;color:#868e96;"));
    v->addWidget(m_idLbl);

    m_stateLbl = new QLabel(this);
    m_stateLbl->setAlignment(Qt::AlignCenter);
    m_stateLbl->setStyleSheet(QStringLiteral("font-size:12px;color:#adb5bd;"));
    v->addWidget(m_stateLbl);

    v->addStretch();

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setMinimumHeight(38);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#9775fa;color:white;border:none;border-radius:8px;"
        "font-size:16px;margin:0 24px;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);

    connect(m_api, &ApiClient::userProfileOk, this, &ProfileViewDialog::onUser);
    connect(m_api, &ApiClient::avatarDownloaded, this, &ProfileViewDialog::onImage);
    connect(m_api, &ApiClient::errorOccurred, this, &ProfileViewDialog::onError);
    m_api->getUserProfile(accessToken, m_userId);
}

// 背景图铺满整个对话框（cover 居中裁剪）+ 白色半透明遮罩保证文字可读
void ProfileViewDialog::paintEvent(QPaintEvent *event)
{
    if (m_bgPix.isNull()) {
        QDialog::paintEvent(event);
        return;
    }
    QPainter p(this);
    const QSize sz = size();
    QPixmap scaled = m_bgPix.scaled(sz, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
    const int x = (scaled.width() - sz.width()) / 2;
    const int y = (scaled.height() - sz.height()) / 2;
    p.drawPixmap(0, 0, scaled,
                 qMax(0, x), qMax(0, y), sz.width(), sz.height());
    p.fillRect(rect(), QColor(255, 255, 255, 215));
}

QPixmap ProfileViewDialog::cropCircle(const QPixmap &src, int size)
{
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    const int x = (scaled.width() - size) / 2;
    const int y = (scaled.height() - size) / 2;
    const QPixmap cropped = scaled.copy(x, y, size, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, cropped);
    p.end();
    return out;
}

void ProfileViewDialog::onUser(const UserInfo &u)
{
    if (u.id != m_userId)
        return;
    m_nameLbl->setText(u.display_name.isEmpty() ? u.username : u.display_name);
    m_titleLbl->setText(u.title.isEmpty()
        ? QString() : QStringLiteral("【 %1 】").arg(u.title));
    m_idLbl->setText(u.user_id.isEmpty()
        ? QString() : QStringLiteral("ID: %1").arg(u.user_id));

    if (!u.avatar_url.isEmpty()) {
        m_api->downloadAvatar(ServerConfig::serverUrl() + u.avatar_url);
    } else {
        const QString name = u.display_name.isEmpty() ? u.username : u.display_name;
        m_avatarLbl->setText(name.left(1).toUpper());
    }
    if (!u.bg_url.isEmpty()) {
        m_bgUrl = ServerConfig::serverUrl() + u.bg_url;
        m_api->downloadAvatar(m_bgUrl);
    }
}

void ProfileViewDialog::onImage(const QString &url, const QPixmap &pm)
{
    if (url == m_bgUrl && !m_bgUrl.isEmpty()) {
        // 存原图，paintEvent 按窗口尺寸实时 cover 铺满
        m_bgPix = pm;
        update();
    } else if (url.contains(QStringLiteral("/avatars/"))) {
        m_avatarLbl->setPixmap(cropCircle(pm, 82));
    }
}

void ProfileViewDialog::onError(const QString &msg)
{
    m_stateLbl->setText(QStringLiteral("资料加载失败：%1").arg(msg));
}
