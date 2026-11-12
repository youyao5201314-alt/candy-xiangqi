#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QTimer>
#include "api.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class ApiClient;

// 登录/注册对话框（双 Tab：账号密码 / 手机验证码）
// 阶段 1：登录成功后关闭对话框，StartScreen 拿到 token/userInfo 进入主流程
// 阶段 3.5：启动强制登录 + 手机验证码登录
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(ApiClient *api, QWidget *parent = nullptr);

    // 登录/注册成功后可取出来
    UserInfo user() const { return m_user; }
    QString accessToken() const { return m_accessToken; }
    QString refreshToken() const { return m_refreshToken; }

    // 强制模式：隐藏关闭按钮，不登录就退出程序
    void setForced(bool f);

protected:
    void reject() override;

private slots:
    void onSubmit();         // 账号密码 Tab：点"登录"或"注册"
    void onSwitchMode();     // 切换登录/注册模式
    void onPhoneSubmit();    // 手机验证码 Tab：点"登录"
    void onSendCode();       // 手机验证码 Tab：点"获取验证码"
    void onCodeCountdown();  // 验证码倒计时
    void onGuestLogin();     // 游客 Tab：点"进入游戏"
    void onApiError(const QString &msg);

    void onLoginOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);
    void onRegisterOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);
    void onSendCodeOk(const QString &devCode);

    // 注册/重置模式下发验证码
    void onRegSendCode();
    void onRegCodeCountdown();
    void onResetPassword();
    void onResetPasswordOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);

private:
    ApiClient *m_api;
    bool m_registerMode = false;  // false=登录模式, true=注册模式
    bool m_forced = false;        // 强制登录模式
    bool m_resetMode = false;     // 密码重置模式
    bool m_codeForReg = false;    // 标记验证码是发给注册/重置还是手机登录 Tab

    // ===== Tab 1: 账号密码 =====
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_displayNameEdit = nullptr;  // 仅注册模式显示
    QLineEdit *m_regPhoneEdit = nullptr;     // 注册/重置：手机号
    QLineEdit *m_regCodeEdit = nullptr;     // 注册/重置：验证码
    QWidget *m_regCodeWidget = nullptr;     // 验证码行容器（含输入框+按钮）
    QPushButton *m_regSendCodeBtn = nullptr; // 注册/重置：获取验证码
    QLabel *m_regDevCodeLabel = nullptr;     // 开发期回显验证码
    QLineEdit *m_newPasswordEdit = nullptr;  // 仅重置模式：新密码
    QPushButton *m_submitBtn = nullptr;
    QPushButton *m_switchBtn = nullptr;
    QPushButton *m_forgotBtn = nullptr;      // "忘记密码？"链接
    QLabel *m_titleLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QTimer *m_regCodeTimer = nullptr;
    int m_regCodeCountdown = 0;

    // ===== Tab 2: 手机验证码 =====
    QLineEdit *m_phoneEdit = nullptr;
    QLineEdit *m_codeEdit = nullptr;
    QPushButton *m_sendCodeBtn = nullptr;
    QPushButton *m_phoneLoginBtn = nullptr;
    QLabel *m_devCodeLabel = nullptr;  // 开发期回显验证码
    QLabel *m_phoneErrorLabel = nullptr;
    QTimer *m_codeTimer = nullptr;
    int m_codeCountdown = 0;

    // ===== Tab 3: 游客登录 =====
    QPushButton *m_guestBtn = nullptr;
    QLabel *m_guestErrorLabel = nullptr;
    QPushButton *m_quitBtn = nullptr;  // 强制模式下显示在底部

    // 服务器地址（三个 Tab 共用，登录前必须填写，房主提供）
    QLineEdit *m_serverEdit = nullptr;
    bool ensureServerUrl();  // 校验并保存地址，空/非法返回 false

    UserInfo m_user;
    QString m_accessToken;
    QString m_refreshToken;

    void setupUi();
    void applyMode();  // 根据 m_registerMode/m_resetMode 切换 UI
};

#endif  // LOGINDIALOG_H
