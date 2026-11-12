// 登录/注册对话框实现
#include "LoginDialog.h"
#include "ServerConfig.h"

#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QSettings>
#include <QRegularExpressionValidator>
#include <QApplication>
#include <QStyle>
#include <QMessageBox>

LoginDialog::LoginDialog(ApiClient *api, QWidget *parent)
    : QDialog(parent), m_api(api)
{
    setupUi();
    applyMode();
    setWindowTitle(QStringLiteral("糖果象棋 - 账号"));
    resize(420, 360);

    connect(m_api, &ApiClient::loginOk, this, &LoginDialog::onLoginOk);
    connect(m_api, &ApiClient::registerOk, this, &LoginDialog::onRegisterOk);
    connect(m_api, &ApiClient::errorOccurred, this, &LoginDialog::onApiError);
    connect(m_api, &ApiClient::sendCodeOk, this, &LoginDialog::onSendCodeOk);
    connect(m_api, &ApiClient::resetPasswordOk, this, &LoginDialog::onResetPasswordOk);
}

void LoginDialog::setupUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background: #f8f9fa; }"
        "QTabWidget::pane { border: 1px solid #dee2e6; border-radius: 8px; background: #fff; }"
        "QTabBar::tab { padding: 8px 20px; font-size: 14px; border: 1px solid #dee2e6;"
        "  border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px;"
        "  background: #e9ecef; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #fff; border-bottom: 1px solid #fff; }"
        "QLineEdit { padding: 8px 12px; font-size: 14px; border: 1px solid #ced4da;"
        "  border-radius: 6px; background: #fff; min-height: 20px; }"
        "QLineEdit:focus { border: 2px solid #4a90d9; }"
        "QPushButton { padding: 8px 16px; font-size: 14px; border: none;"
        "  border-radius: 6px; background: #4a90d9; color: white; }"
        "QPushButton:hover { background: #357abd; }"
        "QPushButton:pressed { background: #2c6aa0; }"
        "QPushButton:disabled { background: #adb5bd; }"
        "QPushButton[flat=\"true\"] { background: transparent; color: #4a90d9; }"
        "QPushButton[flat=\"true\"]:hover { color: #357abd; text-decoration: underline; }"
        "QLabel { font-size: 14px; }"
    ));

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(8);

    auto *tabs = new QTabWidget(this);

    // ===== Tab 1: 账号密码 =====
    auto *pwdTab = new QWidget;
    auto *pwdLayout = new QVBoxLayout(pwdTab);
    pwdLayout->setContentsMargins(24, 20, 24, 20);
    pwdLayout->setSpacing(10);

    m_titleLabel = new QLabel(pwdTab);
    QFont f = m_titleLabel->font();
    f.setPointSize(13);
    f.setBold(true);
    m_titleLabel->setFont(f);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("color: #333; margin-bottom: 4px;");
    pwdLayout->addWidget(m_titleLabel);

    // 输入框——无 FormLayout 标签，靠 placeholder 提示
    m_usernameEdit = new QLineEdit(pwdTab);
    m_usernameEdit->setPlaceholderText(QStringLiteral("用户名"));
    pwdLayout->addWidget(m_usernameEdit);

    m_passwordEdit = new QLineEdit(pwdTab);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    pwdLayout->addWidget(m_passwordEdit);

    m_displayNameEdit = new QLineEdit(pwdTab);
    m_displayNameEdit->setPlaceholderText(QStringLiteral("昵称（可选）"));
    pwdLayout->addWidget(m_displayNameEdit);

    // 注册/重置：手机号
    m_regPhoneEdit = new QLineEdit(pwdTab);
    m_regPhoneEdit->setPlaceholderText(QStringLiteral("手机号"));
    m_regPhoneEdit->setMaxLength(11);
    m_regPhoneEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^1\\d{0,10}$")), m_regPhoneEdit));
    pwdLayout->addWidget(m_regPhoneEdit);

    // 注册/重置：验证码 + 获取按钮
    m_regCodeWidget = new QWidget(pwdTab);
    auto *regCodeRow = new QHBoxLayout(m_regCodeWidget);
    regCodeRow->setContentsMargins(0, 0, 0, 0);
    regCodeRow->setSpacing(6);
    m_regCodeEdit = new QLineEdit(m_regCodeWidget);
    m_regCodeEdit->setPlaceholderText(QStringLiteral("验证码"));
    m_regCodeEdit->setMaxLength(6);
    regCodeRow->addWidget(m_regCodeEdit, 1);
    m_regSendCodeBtn = new QPushButton(QStringLiteral("获取验证码"), m_regCodeWidget);
    m_regSendCodeBtn->setFixedWidth(100);
    connect(m_regSendCodeBtn, &QPushButton::clicked, this, &LoginDialog::onRegSendCode);
    regCodeRow->addWidget(m_regSendCodeBtn);
    pwdLayout->addWidget(m_regCodeWidget);

    // 重置：新密码
    m_newPasswordEdit = new QLineEdit(pwdTab);
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setPlaceholderText(QStringLiteral("新密码"));
    pwdLayout->addWidget(m_newPasswordEdit);

    // 开发期验证码回显
    m_regDevCodeLabel = new QLabel(pwdTab);
    m_regDevCodeLabel->setStyleSheet("color: #28a745; font-size: 12px; margin-top: 2px;");
    m_regDevCodeLabel->setAlignment(Qt::AlignCenter);
    pwdLayout->addWidget(m_regDevCodeLabel);

    // 错误提示
    m_errorLabel = new QLabel(pwdTab);
    m_errorLabel->setStyleSheet("color: #dc3545; font-size: 12px;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    pwdLayout->addWidget(m_errorLabel);

    pwdLayout->addSpacing(4);

    // 主按钮
    m_submitBtn = new QPushButton(pwdTab);
    m_submitBtn->setDefault(true);
    m_submitBtn->setMinimumHeight(40);
    connect(m_submitBtn, &QPushButton::clicked, this, &LoginDialog::onSubmit);
    pwdLayout->addWidget(m_submitBtn);

    // 次要按钮（注册/登录切换）
    m_switchBtn = new QPushButton(pwdTab);
    m_switchBtn->setFlat(true);
    m_switchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_switchBtn, &QPushButton::clicked, this, &LoginDialog::onSwitchMode);
    pwdLayout->addWidget(m_switchBtn, 0, Qt::AlignCenter);

    // 忘记密码
    m_forgotBtn = new QPushButton(QStringLiteral("忘记密码？"), pwdTab);
    m_forgotBtn->setFlat(true);
    m_forgotBtn->setCursor(Qt::PointingHandCursor);
    m_forgotBtn->setStyleSheet("color: #6c757d; font-size: 12px;");
    connect(m_forgotBtn, &QPushButton::clicked, this, [this]() {
        m_resetMode = true;
        m_registerMode = false;
        m_regCodeEdit->clear();
        m_regDevCodeLabel->clear();
        m_newPasswordEdit->clear();
        m_errorLabel->clear();
        applyMode();
    });
    pwdLayout->addWidget(m_forgotBtn, 0, Qt::AlignCenter);

    pwdLayout->addStretch();
    tabs->addTab(pwdTab, QStringLiteral("账号密码"));

    // ===== Tab 2: 手机验证码 =====
    auto *phoneTab = new QWidget;
    auto *phoneLayout = new QVBoxLayout(phoneTab);
    phoneLayout->setContentsMargins(24, 20, 24, 20);
    phoneLayout->setSpacing(10);

    auto *phoneTitle = new QLabel(QStringLiteral("手机验证码登录"), phoneTab);
    phoneTitle->setAlignment(Qt::AlignCenter);
    phoneTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #333; margin-bottom: 4px;");
    phoneLayout->addWidget(phoneTitle);

    m_phoneEdit = new QLineEdit(phoneTab);
    m_phoneEdit->setPlaceholderText(QStringLiteral("手机号"));
    m_phoneEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^1\\d{0,10}$")), m_phoneEdit));
    phoneLayout->addWidget(m_phoneEdit);

    auto *codeRow = new QHBoxLayout;
    codeRow->setSpacing(6);
    m_codeEdit = new QLineEdit(phoneTab);
    m_codeEdit->setPlaceholderText(QStringLiteral("验证码"));
    m_codeEdit->setMaxLength(6);
    codeRow->addWidget(m_codeEdit, 1);
    m_sendCodeBtn = new QPushButton(QStringLiteral("获取验证码"), phoneTab);
    m_sendCodeBtn->setFixedWidth(100);
    connect(m_sendCodeBtn, &QPushButton::clicked, this, &LoginDialog::onSendCode);
    codeRow->addWidget(m_sendCodeBtn);
    phoneLayout->addLayout(codeRow);

    m_devCodeLabel = new QLabel(phoneTab);
    m_devCodeLabel->setStyleSheet("color: #28a745; font-size: 12px;");
    m_devCodeLabel->setAlignment(Qt::AlignCenter);
    phoneLayout->addWidget(m_devCodeLabel);

    m_phoneErrorLabel = new QLabel(phoneTab);
    m_phoneErrorLabel->setStyleSheet("color: #dc3545; font-size: 12px;");
    m_phoneErrorLabel->setWordWrap(true);
    m_phoneErrorLabel->setAlignment(Qt::AlignCenter);
    phoneLayout->addWidget(m_phoneErrorLabel);

    phoneLayout->addSpacing(4);

    m_phoneLoginBtn = new QPushButton(QStringLiteral("登录"), phoneTab);
    m_phoneLoginBtn->setDefault(true);
    m_phoneLoginBtn->setMinimumHeight(40);
    connect(m_phoneLoginBtn, &QPushButton::clicked, this, &LoginDialog::onPhoneSubmit);
    phoneLayout->addWidget(m_phoneLoginBtn);

    phoneLayout->addStretch();
    tabs->addTab(phoneTab, QStringLiteral("手机验证码"));

    // ===== Tab 3: 游客登录 =====
    auto *guestTab = new QWidget;
    auto *guestLayout = new QVBoxLayout(guestTab);
    guestLayout->setContentsMargins(24, 20, 24, 20);
    guestLayout->setSpacing(12);

    auto *guestTitle = new QLabel(QStringLiteral("游客登录"), guestTab);
    guestTitle->setAlignment(Qt::AlignCenter);
    guestTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #333;");
    guestLayout->addWidget(guestTitle);

    auto *guestDesc = new QLabel(
        QStringLiteral("无需注册，随机生成身份\n可体验所有游戏模式\n身份自动保存，下次继续使用"), guestTab);
    guestDesc->setAlignment(Qt::AlignCenter);
    guestDesc->setStyleSheet(QStringLiteral("color:#6c757d; font-size: 13px;"));
    guestDesc->setWordWrap(true);
    guestLayout->addWidget(guestDesc);

    m_guestErrorLabel = new QLabel(guestTab);
    m_guestErrorLabel->setStyleSheet("color: #dc3545; font-size: 12px;");
    m_guestErrorLabel->setWordWrap(true);
    m_guestErrorLabel->setAlignment(Qt::AlignCenter);
    guestLayout->addWidget(m_guestErrorLabel);

    guestLayout->addSpacing(8);

    m_guestBtn = new QPushButton(QStringLiteral("进入游戏"), guestTab);
    m_guestBtn->setMinimumHeight(44);
    m_guestBtn->setStyleSheet(QStringLiteral("font-size:15px;"));
    connect(m_guestBtn, &QPushButton::clicked, this, &LoginDialog::onGuestLogin);
    guestLayout->addWidget(m_guestBtn);

    guestLayout->addStretch();
    tabs->addTab(guestTab, QStringLiteral("游客登录"));

    v->addWidget(tabs);

    // 服务器地址：三个 Tab 共用，朋友之间联机前由房主提供
    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText(
        QStringLiteral("服务器地址（向房主索取），如 https://xxxxx.cpolar.top"));
    m_serverEdit->setText(ServerConfig::serverUrl());
    v->addWidget(m_serverEdit);

    m_codeTimer = new QTimer(this);
    connect(m_codeTimer, &QTimer::timeout, this, &LoginDialog::onCodeCountdown);

    m_regCodeTimer = new QTimer(this);
    connect(m_regCodeTimer, &QTimer::timeout, this, &LoginDialog::onRegCodeCountdown);

    // 强制登录模式下退出按钮
    m_quitBtn = new QPushButton(QStringLiteral("退出游戏"), this);
    m_quitBtn->setStyleSheet(QStringLiteral("color:#dc3545; font-size: 12px;"));
    m_quitBtn->setFlat(true);
    connect(m_quitBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    m_quitBtn->setVisible(false);
    v->addWidget(m_quitBtn, 0, Qt::AlignCenter);
}

void LoginDialog::setForced(bool f)
{
    m_forced = f;
    if (f) {
        // 隐藏关闭按钮，显示退出游戏按钮
        setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
        if (m_quitBtn) m_quitBtn->setVisible(true);
    }
}

void LoginDialog::reject()
{
    if (m_forced) {
        qApp->quit();
        return;
    }
    QDialog::reject();
}

bool LoginDialog::ensureServerUrl()
{
    const QString url = m_serverEdit->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("服务器地址"),
                             QStringLiteral("请先填写服务器地址（向房主索取）"));
        m_serverEdit->setFocus();
        return false;
    }
    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, QStringLiteral("服务器地址"),
                             QStringLiteral("地址需要以 http:// 或 https:// 开头"));
        m_serverEdit->setFocus();
        return false;
    }
    ServerConfig::setServerUrl(url);
    return true;
}

void LoginDialog::applyMode()
{
    m_errorLabel->clear();

    if (m_resetMode) {
        // 密码重置模式
        m_titleLabel->setText(QStringLiteral("重置密码"));
        m_submitBtn->setText(QStringLiteral("重置密码"));
        m_switchBtn->setText(QStringLiteral("返回登录"));
        m_usernameEdit->setVisible(false);
        m_passwordEdit->setVisible(false);
        m_displayNameEdit->setVisible(false);
        m_regPhoneEdit->setVisible(true);
        m_regCodeWidget->setVisible(true);
        m_newPasswordEdit->setVisible(true);
        m_regDevCodeLabel->setVisible(true);
        m_forgotBtn->setVisible(false);
    } else {
        // 登录/注册模式
        m_usernameEdit->setVisible(true);
        m_passwordEdit->setVisible(true);
        m_displayNameEdit->setVisible(m_registerMode);
        m_regPhoneEdit->setVisible(m_registerMode);
        m_regCodeWidget->setVisible(m_registerMode);
        m_newPasswordEdit->setVisible(false);
        m_regDevCodeLabel->setVisible(m_registerMode);
        m_forgotBtn->setVisible(!m_registerMode);

        if (m_registerMode) {
            m_titleLabel->setText(QStringLiteral("注册新账号"));
            m_submitBtn->setText(QStringLiteral("注册"));
            m_switchBtn->setText(QStringLiteral("已有账号？点这里登录"));
        } else {
            m_titleLabel->setText(QStringLiteral("登录"));
            m_submitBtn->setText(QStringLiteral("登录"));
            m_switchBtn->setText(QStringLiteral("没账号？点这里注册"));
        }
    }
}

void LoginDialog::onSubmit()
{
    if (!ensureServerUrl())
        return;
    if (m_resetMode) {
        onResetPassword();
        return;
    }

    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();
    QString display = m_displayNameEdit->text().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("用户名和密码不能为空"));
        return;
    }
    if (password.length() < 6) {
        m_errorLabel->setText(QStringLiteral("密码至少 6 字符"));
        return;
    }

    if (m_registerMode) {
        QString phone = m_regPhoneEdit->text().trimmed();
        QString code = m_regCodeEdit->text().trimmed();
        if (phone.length() != 11) {
            m_errorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
            return;
        }
        if (code.length() != 6) {
            m_errorLabel->setText(QStringLiteral("请输入 6 位验证码"));
            return;
        }
        m_errorLabel->clear();
        m_submitBtn->setEnabled(false);
        m_api->registerUser(username, password, display, phone, code);
    } else {
        m_errorLabel->clear();
        m_submitBtn->setEnabled(false);
        m_api->login(username, password);
    }
}

void LoginDialog::onSwitchMode()
{
    // 如果当前是重置模式，返回登录模式
    if (m_resetMode) {
        m_resetMode = false;
    } else {
        m_registerMode = !m_registerMode;
    }
    m_regCodeEdit->clear();
    m_regDevCodeLabel->clear();
    m_newPasswordEdit->clear();
    m_regPhoneEdit->clear();
    applyMode();
}

void LoginDialog::onSendCode()
{
    if (!ensureServerUrl())
        return;
    QString phone = m_phoneEdit->text().trimmed();
    if (phone.length() != 11 || !phone.startsWith('1')) {
        m_phoneErrorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    m_phoneErrorLabel->clear();
    m_devCodeLabel->clear();
    m_sendCodeBtn->setEnabled(false);
    m_codeForReg = false;
    m_api->sendCode(phone, QStringLiteral("login"));
}

void LoginDialog::onSendCodeOk(const QString &devCode)
{
    if (m_codeForReg) {
        // 注册/重置模式的验证码
        m_regDevCodeLabel->setText(QStringLiteral("开发期验证码：%1").arg(devCode));
        m_regCodeCountdown = 60;
        m_regSendCodeBtn->setText(QStringLiteral("重新发送(%1)").arg(m_regCodeCountdown));
        m_regCodeTimer->start(1000);
    } else {
        // 手机验证码 Tab 的验证码
        m_devCodeLabel->setText(QStringLiteral("开发期验证码：%1").arg(devCode));
        m_codeCountdown = 60;
        m_sendCodeBtn->setText(QStringLiteral("重新发送(%1)").arg(m_codeCountdown));
        m_codeTimer->start(1000);
    }
}

void LoginDialog::onCodeCountdown()
{
    --m_codeCountdown;
    if (m_codeCountdown <= 0) {
        m_codeTimer->stop();
        m_sendCodeBtn->setText(QStringLiteral("获取验证码"));
        m_sendCodeBtn->setEnabled(true);
    } else {
        m_sendCodeBtn->setText(QStringLiteral("重新发送(%1)").arg(m_codeCountdown));
    }
}

void LoginDialog::onPhoneSubmit()
{
    if (!ensureServerUrl())
        return;
    QString phone = m_phoneEdit->text().trimmed();
    QString code = m_codeEdit->text().trimmed();
    if (phone.length() != 11) {
        m_phoneErrorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (code.length() != 6) {
        m_phoneErrorLabel->setText(QStringLiteral("请输入 6 位验证码"));
        return;
    }
    m_phoneErrorLabel->clear();
    m_phoneLoginBtn->setEnabled(false);
    m_api->phoneLogin(phone, code);
}

void LoginDialog::onGuestLogin()
{
    if (!ensureServerUrl())
        return;
    m_guestErrorLabel->clear();
    m_guestBtn->setEnabled(false);
    m_api->guestLogin();
}

void LoginDialog::onApiError(const QString &msg)
{
    m_submitBtn->setEnabled(true);
    m_phoneLoginBtn->setEnabled(true);
    m_guestBtn->setEnabled(true);
    m_regSendCodeBtn->setEnabled(true);
    // 判断错误显示在哪个 Tab
    if (m_phoneEdit && m_phoneEdit->hasFocus()) {
        m_phoneErrorLabel->setText(msg);
    } else {
        m_errorLabel->setText(msg);
    }
}

void LoginDialog::onLoginOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken)
{
    m_user = user;
    m_accessToken = accessToken;
    m_refreshToken = refreshToken;
    ApiClient::saveRefreshToken(refreshToken);
    accept();
}

void LoginDialog::onRegisterOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken)
{
    m_user = user;
    m_accessToken = accessToken;
    m_refreshToken = refreshToken;
    ApiClient::saveRefreshToken(refreshToken);
    accept();
}

void LoginDialog::onRegSendCode()
{
    if (!ensureServerUrl())
        return;
    QString phone = m_regPhoneEdit->text().trimmed();
    if (phone.length() != 11 || !phone.startsWith('1')) {
        m_errorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    m_errorLabel->clear();
    m_regDevCodeLabel->clear();
    m_regSendCodeBtn->setEnabled(false);
    m_codeForReg = true;
    m_api->sendCode(phone, QStringLiteral("bind"));
}

void LoginDialog::onRegCodeCountdown()
{
    --m_regCodeCountdown;
    if (m_regCodeCountdown <= 0) {
        m_regCodeTimer->stop();
        m_regSendCodeBtn->setText(QStringLiteral("获取验证码"));
        m_regSendCodeBtn->setEnabled(true);
    } else {
        m_regSendCodeBtn->setText(QStringLiteral("重新发送(%1)").arg(m_regCodeCountdown));
    }
}

void LoginDialog::onResetPassword()
{
    QString phone = m_regPhoneEdit->text().trimmed();
    QString code = m_regCodeEdit->text().trimmed();
    QString newPassword = m_newPasswordEdit->text();

    if (phone.length() != 11) {
        m_errorLabel->setText(QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (code.length() != 6) {
        m_errorLabel->setText(QStringLiteral("请输入 6 位验证码"));
        return;
    }
    if (newPassword.length() < 6) {
        m_errorLabel->setText(QStringLiteral("新密码至少 6 字符"));
        return;
    }

    m_errorLabel->clear();
    m_submitBtn->setEnabled(false);
    m_api->resetPassword(phone, code, newPassword);
}

void LoginDialog::onResetPasswordOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken)
{
    m_user = user;
    m_accessToken = accessToken;
    m_refreshToken = refreshToken;
    ApiClient::saveRefreshToken(refreshToken);
    accept();
}
