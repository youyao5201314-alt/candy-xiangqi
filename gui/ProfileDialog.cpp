// 个人资料对话框实现
#include "ProfileDialog.h"
#include "ServerConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QStandardPaths>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

ProfileDialog::ProfileDialog(ApiClient *api, const UserInfo &user,
                             const QString &accessToken, QWidget *parent)
    : QDialog(parent), m_api(api), m_token(accessToken), m_user(user)
{
    setWindowTitle(QStringLiteral("个人资料"));
    resize(400, 600);   // 先定尺寸，setupUi 里浮动按钮按 width() 定位
    setupUi();

    // 异步拉头像与背景
    if (!m_user.avatar_url.isEmpty()) {
        m_api->downloadAvatar(ServerConfig::serverUrl() + m_user.avatar_url);
    }
    if (!m_user.bg_url.isEmpty()) {
        m_bgUrlFull = ServerConfig::serverUrl() + m_user.bg_url;
        m_api->downloadAvatar(m_bgUrlFull);
    }

    connect(m_api, &ApiClient::profileUpdated, this, &ProfileDialog::onProfileUpdated);
    connect(m_api, &ApiClient::avatarDownloaded, this, &ProfileDialog::onAvatarDownloaded);
    connect(m_api, &ApiClient::errorOccurred, this, &ProfileDialog::onApiError);
    connect(m_api, &ApiClient::sendCodeOk, this, [this](const QString &devCode) {
        m_errorLabel->setStyleSheet("color: #0a0;");
        m_errorLabel->setText(QStringLiteral("开发期验证码：%1").arg(devCode));
    });
}

void ProfileDialog::setupUi()
{
    auto *v = new QVBoxLayout(this);
    v->setSpacing(12);

    // ===== 右上角浮动「更换背景」按钮（背景图由 paintEvent 铺满整个对话框） =====
    auto *bgBtn = new QPushButton(QStringLiteral("更换背景"), this);
    bgBtn->setFlat(true);
    bgBtn->setStyleSheet(QStringLiteral(
        "QPushButton{color:white;background:rgba(0,0,0,80);border-radius:8px;"
        "padding:5px 10px;font-size:12px;}"
        "QPushButton:hover{background:rgba(0,0,0,130);}"));
#ifndef Q_OS_ANDROID
    connect(bgBtn, &QPushButton::clicked, this, &ProfileDialog::onChangeBg);
#else
    bgBtn->setVisible(false);
#endif
    bgBtn->move(width() - 96, 8);
    bgBtn->raise();

    // ===== 头像区 =====
    auto *avatarArea = new QVBoxLayout;
    m_avatarLbl = new QLabel(this);
    m_avatarLbl->setFixedSize(96, 96);
    m_avatarLbl->setAlignment(Qt::AlignCenter);
    m_avatarLbl->setStyleSheet(
        "border: 2px solid #ddd; border-radius: 48px; background: #f5f5f5;");
    // 默认显示占位文字
    m_avatarLbl->setText(QStringLiteral("头像"));
    avatarArea->addWidget(m_avatarLbl, 0, Qt::AlignCenter);

#ifndef Q_OS_ANDROID
    m_changeAvatarBtn = new QPushButton(QStringLiteral("更换头像"), this);
    m_changeAvatarBtn->setFlat(true);
    m_changeAvatarBtn->setStyleSheet("color: #1976d2; text-decoration: underline;");
    connect(m_changeAvatarBtn, &QPushButton::clicked, this, &ProfileDialog::onChangeAvatar);
    avatarArea->addWidget(m_changeAvatarBtn, 0, Qt::AlignCenter);
#endif

    v->addLayout(avatarArea);

    // ===== 头衔（服务器按注册手机号实时判定） =====
    m_titleLbl = new QLabel(this);
    m_titleLbl->setAlignment(Qt::AlignCenter);
    {
        const QString t = m_user.title.isEmpty()
            ? QStringLiteral("全服最拉") : m_user.title;
        m_titleLbl->setText(QStringLiteral("【 %1 】").arg(t));
        // 三种头衔不同配色：金红 / 蓝灰 / 灰
        QString color;
        if (t == QStringLiteral("全服第一"))
            color = QStringLiteral("#d4a017");
        else if (t == QStringLiteral("全服第二"))
            color = QStringLiteral("#1976d2");
        else
            color = QStringLiteral("#999");
        m_titleLbl->setStyleSheet(
            QStringLiteral("font-size:20px;font-weight:bold;color:%1;").arg(color));
    }
    v->addWidget(m_titleLbl);

    // ===== 用户 ID（只读） =====
    m_userIdLbl = new QLabel(this);
    m_userIdLbl->setAlignment(Qt::AlignCenter);
    m_userIdLbl->setStyleSheet(QStringLiteral("color: #666; font-size: 13px;"));
    if (!m_user.user_id.isEmpty())
        m_userIdLbl->setText(QStringLiteral("ID: %1").arg(m_user.user_id));
    v->addWidget(m_userIdLbl);

    // ===== 昵称区 =====
    auto *nameForm = new QFormLayout;
    m_nameEdit = new QLineEdit(m_user.display_name, this);
    m_nameEdit->setMaxLength(20);
    m_nameEdit->setPlaceholderText(QStringLiteral("1-20 字符"));
    nameForm->addRow(QStringLiteral("昵称："), m_nameEdit);
    v->addLayout(nameForm);

    m_saveNameBtn = new QPushButton(QStringLiteral("保存昵称"), this);
    connect(m_saveNameBtn, &QPushButton::clicked, this, &ProfileDialog::onSaveNickname);
    v->addWidget(m_saveNameBtn);

    // ===== 手机号区 =====
    auto *phoneGroup = new QVBoxLayout;
    m_phoneStatusLbl = new QLabel(this);
    phoneGroup->addWidget(m_phoneStatusLbl);

    if (m_user.phone.isEmpty()) {
        // 未绑定：显示绑定表单
        auto *phoneForm = new QFormLayout;
        m_phoneEdit = new QLineEdit(this);
        m_phoneEdit->setPlaceholderText(QStringLiteral("11 位手机号"));
        m_phoneEdit->setMaxLength(11);
        phoneForm->addRow(QStringLiteral("手机号："), m_phoneEdit);

        auto *codeRow = new QHBoxLayout;
        m_bindCodeEdit = new QLineEdit(this);
        m_bindCodeEdit->setPlaceholderText(QStringLiteral("6 位验证码"));
        m_bindCodeEdit->setMaxLength(6);
        codeRow->addWidget(m_bindCodeEdit);

        m_sendBindBtn = new QPushButton(QStringLiteral("获取验证码"), this);
        m_sendBindBtn->setMinimumWidth(100);
        connect(m_sendBindBtn, &QPushButton::clicked, this, &ProfileDialog::onSendBindCode);
        codeRow->addWidget(m_sendBindBtn);
        phoneForm->addRow(QStringLiteral("验证码："), codeRow);
        phoneGroup->addLayout(phoneForm);

        m_bindBtn = new QPushButton(QStringLiteral("绑定手机号"), this);
        connect(m_bindBtn, &QPushButton::clicked, this, &ProfileDialog::onBindPhone);
        phoneGroup->addWidget(m_bindBtn);
    } else {
        // 已绑定：显示脱敏手机号
        QString masked = m_user.phone;
        if (masked.length() == 11) {
            masked = masked.left(3) + QStringLiteral("****") + masked.right(4);
        }
        m_phoneStatusLbl->setText(QStringLiteral("已绑定：%1").arg(masked));
        m_phoneStatusLbl->setStyleSheet("color: #666;");
    }
    v->addLayout(phoneGroup);

    // ===== 错误提示 =====
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #c00;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    v->addWidget(m_errorLabel);

    // ===== 关闭按钮 =====
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);

    m_bindTimer = new QTimer(this);
    connect(m_bindTimer, &QTimer::timeout, this, &ProfileDialog::onBindCountdown);
}

// ---------- 圆形头像裁剪 ----------
QPixmap ProfileDialog::makeCircular(const QPixmap &src, int size)
{
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    // 居中裁剪
    int x = (scaled.width() - size) / 2;
    int y = (scaled.height() - size) / 2;
    QPixmap cropped = scaled.copy(x, y, size, size);

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

// 背景图铺满整个对话框（cover 居中裁剪）+ 白色半透明遮罩保证文字可读
void ProfileDialog::paintEvent(QPaintEvent *event)
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

void ProfileDialog::refreshAvatar(const QPixmap &pm)
{
    m_avatarLbl->setPixmap(makeCircular(pm, 96));
}

void ProfileDialog::onChangeAvatar()
{
    QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像图片"),
        QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp)"));
    if (file.isEmpty()) return;
    m_errorLabel->clear();
    m_api->uploadAvatar(m_token, file);
}

void ProfileDialog::onChangeBg()
{
    QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择背景图片"),
        QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp)"));
    if (file.isEmpty()) return;
    m_errorLabel->setText(QStringLiteral("背景上传中…"));

    // 上传前压缩：最长边 1600px + JPEG 质量 85（原图几 MB 经免费隧道上传/下载都要十几秒）
    QImage img(file);
    if (img.isNull()) {
        m_errorLabel->setText(QStringLiteral("图片读取失败"));
        return;
    }
    const int maxSide = qMax(img.width(), img.height());
    if (maxSide > 1600) {
        if (img.width() >= img.height())
            img = img.scaledToWidth(1600, Qt::SmoothTransformation);
        else
            img = img.scaledToHeight(1600, Qt::SmoothTransformation);
    }
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/candyboard_bg_upload.jpg");
    if (!img.save(tmp, "JPEG", 85)) {
        m_errorLabel->setText(QStringLiteral("图片处理失败"));
        return;
    }
    m_api->uploadProfileBg(m_token, tmp);
}

void ProfileDialog::onSaveNickname()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || name.length() > 20) {
        m_errorLabel->setText(QStringLiteral("昵称 1-20 字符"));
        return;
    }
    m_errorLabel->clear();
    m_saveNameBtn->setEnabled(false);
    m_api->updateProfile(m_token, name);
}

void ProfileDialog::onSendBindCode()
{
    QString phone = m_phoneEdit->text().trimmed();
    if (phone.length() != 11 || !phone.startsWith('1')) {
        m_errorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    m_errorLabel->clear();
    m_sendBindBtn->setEnabled(false);
    m_api->sendCode(phone, QStringLiteral("bind"));
    // 倒计时（sendCodeOk 信号在构造时连接，这里直接启动）
    m_bindCountdown = 60;
    m_sendBindBtn->setText(QStringLiteral("重新发送(%1)").arg(m_bindCountdown));
    m_bindTimer->start(1000);
}

void ProfileDialog::onBindCountdown()
{
    --m_bindCountdown;
    if (m_bindCountdown <= 0) {
        m_bindTimer->stop();
        m_sendBindBtn->setText(QStringLiteral("获取验证码"));
        m_sendBindBtn->setEnabled(true);
    } else {
        m_sendBindBtn->setText(QStringLiteral("重新发送(%1)").arg(m_bindCountdown));
    }
}

void ProfileDialog::onBindPhone()
{
    QString phone = m_phoneEdit->text().trimmed();
    QString code = m_bindCodeEdit->text().trimmed();
    if (phone.length() != 11) {
        m_errorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (code.length() != 6) {
        m_errorLabel->setText(QStringLiteral("请输入 6 位验证码"));
        return;
    }
    m_errorLabel->clear();
    m_bindBtn->setEnabled(false);
    m_api->bindPhone(m_token, phone, code);
}

void ProfileDialog::onProfileUpdated(const UserInfo &user)
{
    m_user = user;
    m_saveNameBtn->setEnabled(true);
    if (m_bindBtn) m_bindBtn->setEnabled(true);

    // 刷新用户 ID
    if (m_userIdLbl && !user.user_id.isEmpty())
        m_userIdLbl->setText(QStringLiteral("ID: %1").arg(user.user_id));

    // 刷新昵称
    m_nameEdit->setText(user.display_name);

    // 刷新头衔（绑定/更换手机号后头衔会变）
    if (m_titleLbl) {
        const QString t = user.title.isEmpty() ? QStringLiteral("全服最拉") : user.title;
        m_titleLbl->setText(QStringLiteral("【 %1 】").arg(t));
        QString color;
        if (t == QStringLiteral("全服第一"))
            color = QStringLiteral("#d4a017");
        else if (t == QStringLiteral("全服第二"))
            color = QStringLiteral("#1976d2");
        else
            color = QStringLiteral("#999");
        m_titleLbl->setStyleSheet(
            QStringLiteral("font-size:20px;font-weight:bold;color:%1;").arg(color));
    }

    // 刷新手机号区
    if (!user.phone.isEmpty() && m_phoneEdit) {
        // 绑定成功：切换为已绑定状态（简化处理：提示并关闭重开）
        QMessageBox::information(this, QStringLiteral("绑定成功"),
            QStringLiteral("手机号 %1 绑定成功！").arg(user.phone));
        emit profileUpdated(user);
        accept();  // 关闭对话框，主界面刷新
        return;
    }

    // 刷新头像
    if (!user.avatar_url.isEmpty()) {
        m_api->downloadAvatar(ServerConfig::serverUrl() + user.avatar_url);
    }

    // 刷新背景图
    m_user.bg_url = user.bg_url;
    if (!user.bg_url.isEmpty()) {
        m_bgUrlFull = ServerConfig::serverUrl() + user.bg_url;
        m_api->downloadAvatar(m_bgUrlFull);
    }

    emit profileUpdated(user);
}

void ProfileDialog::onAvatarDownloaded(const QString &url, const QPixmap &pm)
{
    if (url == m_bgUrlFull && !m_bgUrlFull.isEmpty()) {
        // 存原图，paintEvent 按窗口尺寸实时 cover 铺满
        m_bgPix = pm;
        update();
        m_errorLabel->clear();
        return;
    }
    refreshAvatar(pm);
}

void ProfileDialog::onApiError(const QString &msg)
{
    m_saveNameBtn->setEnabled(true);
    if (m_bindBtn) m_bindBtn->setEnabled(true);
    m_errorLabel->setStyleSheet("color: #c00;");
    m_errorLabel->setText(msg);
}
