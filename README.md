# 🍬 糖果象棋 · Candy Xiangqi

> 中国象棋 Qt6/C++ 图形对战游戏 —— 从控制台到互联网上云的 5 个版本完整演进

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Android-lightgrey.svg)](#运行)
[![Qt](https://img.shields.io/badge/Qt-6.x%20%7C%205.x-green.svg)](#编译)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](#编译)

---

## ✨ 功能一览

| 功能 | 说明 |
|------|------|
| ♟️ **完整象棋引擎** | 车/马/炮/相/仕/帅/兵 七类棋子走法，将军检测，将死判定，棋谱记录 |
| 🎮 **三种对战模式** | 人机 AI（博弈树 + 置换表）、局域网联机、互联网实时对战 |
| 🤖 **女号氛围机器人** | 观战聊天里的 AI 小伙伴，让房间不冷场 |
| 🏆 **成就系统** | 击败奶娃类、杀法类、残局类、互联网对战类成就 |
| 🎒 **背包 & 商城** | 糖果消耗品（笨/一般/聪明三档 AI 思考时间）、神秘商品 |
| 👥 **好友 & 私信** | 搜索加好友、邀请对战、送礼（祝福/鲜花/巧克力/奖杯）、对局中免打扰红点 |
| 📺 **观战模式** | 点击在线对局进入只读观战，实时同步落子，支持弹幕聊天 |
| 😆 **奶龙表情包** | 11 张奶龙 + 130+ emoji 选择器，聊天传 `[sticker:sNN]` 本地渲染 |
| 🎨 **奶娃虚拟形象** | idle/thinking/action/angry/win/lose/music/undo/selected 九种状态表情 |
| 🔊 **音效 & BGM** | 走子/吃子/将军/胜负全链路音效，三首背景音乐循环 |
| 🧩 **残局挑战** | 单车擒王、马炮胜双士、双车错 — 固定开局红方必胜 |
| 📱 **跨平台** | Windows 安装包 + Android APK（`androiddeployqt` 导出） |

---

## 🏗️ 技术栈

```
┌─────────────────────────────────────────────────────────┐
│                    客户端 (Qt Widgets)                    │
│  C++17 · Qt6 (或 Qt5) · CMake · QPainter · WebSockets   │
├─────────────────────────────────────────────────────────┤
│                    服务端 (云部署)                        │
│  Node.js · Express · ws (WebSocket) · SQLite · cpolar/ECS│
├─────────────────────────────────────────────────────────┤
│                    服务器环境                             │
│  阿里云 ECS Ubuntu 22.04 · 2核2G · 3000 端口 · 公网固定IP  │
└─────────────────────────────────────────────────────────┘
```

---

## 📂 仓库结构

```
candy-xiangqi/
├── gui/                        # 全部源码 (V3.3 最终版)
│   ├── CMakeLists.txt          # Qt6/Qt5 双版本 CMake
│   ├── gui.pro                 # qmake 构建脚本
│   ├── main.cpp                # QApplication 入口
│   ├── board.h / board.cpp     # 棋盘引擎 (7 类棋子规则 + 将军判定)
│   ├── MainWindow.h/.cpp/.ui   # 主窗口 + QPainter 棋盘 (~86KB)
│   ├── StartScreen.h/.cpp      # 开始界面 (单机/局域网/互联网三路菜单)
│   ├── InternetClient.h/.cpp   # 互联网对战客户端 (REST + WebSocket)
│   ├── api.h/.cpp              # HTTP API 封装
│   ├── ServerConfig.h          # 服务端地址统一配置 (公网/本地切换)
│   ├── FriendsPanel.h/.cpp     # 好友面板 + 邀请 + 送礼 + 私信
│   ├── ChatDialog.h/.cpp       # 聊天对话框 + 表情包渲染
│   ├── LoginDialog.h/.cpp      # 登录注册 + 搜索式加好友
│   ├── Achievements*.h/.cpp    # 成就数据 + 成就殿堂 UI
│   ├── Inventory.h/.cpp        # 背包数据 (货币/糖果/道具)
│   ├── BagDialog / ShopDialog  # 背包 + 商城对话框
│   ├── StickerPicker.h/.cpp    # 奶龙 11 张 + 130+ emoji 选择器
│   ├── ProfileDialog / MailDialog / MatchPanel
│   ├── avatar/                 # 奶娃 9 种状态图 (idle/thinking/angry/win/lose/...)
│   ├── bg/                     # 棋盘背景 (blue.jpg / dragon.jpg)
│   ├── stickers/               # 奶龙表情包 s01~s11.jpg
│   ├── sounds/                 # 音效 (move/capture/check/win/lose/...)
│   │   └── voice/              # 角色语音 (lihai/kewu/welcome/...)
│   ├── music/                  # 三首背景音乐
│   ├── videos/                 # 开场动画
│   └── android/AndroidManifest.xml
├── docs/                       # 开发日志 + 学习笔记
│   ├── 糖果象棋3.3开发日志.md
│   ├── 糖果象棋3.2开发日志.md
│   └── 糖果象棋开发全程学习笔记.md
├── .github/ISSUE_TEMPLATE/     # Issue 模板 (bug + 功能请求)
├── .github/workflows/          # GitHub Actions CI
├── setup.iss                   # Inno Setup 安装脚本
└── .gitignore
```

---

## 🚀 运行

### 编译客户端

```bash
# 依赖: Qt6 (或 Qt5) Widgets + Network + WebSockets + Multimedia
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build -j
```

### 启动服务端

```bash
cd server/
npm install
node app.js          # 本地 http://localhost:8080
# 或部署到云服务器, 修改 gui/ServerConfig.h 里的地址即可
```

### 公网服务端

糖果象棋 V3.3 已部署在阿里云 ECS:
- **公网地址**: `http://118.31.237.140:3000`
- **客户端连接**: 确保 `gui/ServerConfig.h` 里写的是这个地址

---

## 📜 版本演进 (5 个 commit, 12,000+ 行净增代码)

```
V1.1  2026 暑假      控制台象棋 (纯 C++ 标准库, std::cin/cout 走子)
 │  1f21924  5 文件, 1631 行
 │
 ▼
V2.1  2026 秋初      Qt6 GUI + 奶娃形象 + 残局 + 音效 + 局域网联机骨架
 │  1b7056a  37 文件, +1775 行
 │
 ▼
V3.1  2026 秋中      StartScreen + NetworkManager + 观战 + 女号机器人 + AndroidManifest
 │  b5c542e  60 文件, +1896 行
 │
 ▼
V3.2  2026-10-10     成就/背包/商城/表情包/好友/邮件/送礼 + 安卓 APK 发布
 │  325bf90  90 文件, +8456 行  ⚡ 全天冲刺日
 │
 ▼
V3.3  2026-11        阿里云 ECS 上云 + 私信免打扰红点 + 版本握手校验 + 软著材料
    2f1fcff  ~90 文件, 小幅改进
```

每个 commit message 都包含：**功能清单、技术栈变化、踩坑记录、工程铁律**。

### 演进亮点

| 版本 | 关键决策 |
|------|----------|
| V1.1 → V2.1 | 从 std::cin 到 QPainter — 第一次架构质变 |
| V2.1 → V3.1 | 从局域网 UDP 到 WebSocket 实时同步 + cpolar 内网穿透 |
| V3.1 → V3.2 | 25+ 新文件一天写完（成就/背包/商城/表情包/好友系统） |
| V3.2 → V3.3 | 免费内网穿透不稳定 → 上阿里云 ECS 固定 IP |

---

## 🛠️ 工程铁律（来自 5 个版本的撞墙经验）

- **耗时初始化不能放构造函数** — QMediaPlayer 构造卡 4 秒 → showEvent 后起播
- **置换表懒分配** — 进程启动白占 64MB → 首次进入 AI 才分配
- **换过 .exe 必重跑 windeployqt** — 裸环境启动验证再出包
- **免费内网穿透 URL 会漂移** — 正式交付必须上云固定 IP
- **C++ 里中文标点是隐形炸弹** — 编译报错先查标点
- **用户审美 > 开发者审美** — 糖果渐变按钮被嫌丑 → 回退朴素样式

---

## 📦 发布方式

糖果象棋 V3.3 发布三件套:
1. **Windows 安装包** — Inno Setup + windeployqt
2. **Android APK** — androiddeployqt 导出
3. **源码包** — GitHub Releases 附 zip/tar.gz

---

## 👤 作者

- **youyao5201314** — youyao5201314@gmail.com
- 著作权人：安徽理工大学 · 合作开发

---

## 📄 许可证

MIT License
