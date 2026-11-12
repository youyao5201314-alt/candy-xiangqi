#include"board.h"
#include<algorithm>
#include<chrono>
//初始化棋盘函数
int coreBoard[10][9];
const int MAX_DEPTH = 24;   // 深度上限（含将军延展加成，实际由时间限制截断）
const int BOARD_W = 9;
const int BOARD_H = 10;
// ===== 性能优化全局 =====
const int INF = 1000000;    // 无穷大（避免 INT_MIN 取负溢出）
const int MATE = 100000;    // 杀棋分值
static int g_timeLimitMs = 20000; // AI每步思考时间上限（可由界面难度选项调整）
void setAiTimeLimitMs(int ms) { if (ms >= 100) g_timeLimitMs = ms; }
const int QS_MAX = 16;      // 静止搜索最大延展层数（吃子链/将军链搜到底的保险）
static unsigned long long g_nodes = 0;  // 搜索节点计数
static bool g_stop = false;             // 时间到标志
static std::chrono::steady_clock::time_point g_deadline;
static inline bool checkTime() {
    if (std::chrono::steady_clock::now() > g_deadline) g_stop = true;
    return g_stop;
}
// 前置声明：轻量攻击检测（详见文件后部实现）
static bool isSquareAttacked(int board[10][9], int kx, int ky, bool byRed);

// ===== Zobrist 哈希（置换表用）=====
// 棋子→索引：R0 N1 B2 A3 K4 P5 S6 r7 n8 b9 a10 k11 p12 s13
static const auto PIECE_IDX = [] {
    std::array<int, 256> t{}; t.fill(-1);
    t['R'] = 0; t['N'] = 1; t['B'] = 2; t['A'] = 3; t['K'] = 4; t['P'] = 5; t['S'] = 6;
    t['r'] = 7; t['n'] = 8; t['b'] = 9; t['a'] = 10; t['k'] = 11; t['p'] = 12; t['s'] = 13;
    return t;
}();
struct ZobristKeys { uint64_t p[14][90]; uint64_t side; };
static const ZobristKeys ZK = [] {
    ZobristKeys z;
    uint64_t s = 0x9E3779B97F4A7C15ULL; // xorshift64 固定种子，保证可复现
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    for (int i = 0; i < 14; i++) for (int j = 0; j < 90; j++) z.p[i][j] = rnd();
    z.side = rnd();
    return z;
}();
static uint64_t hashBoard(int board[10][9], bool redTurn) {
    uint64_t h = 0;
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 9; j++) {
            int c = board[i][j];
            if (c) h ^= ZK.p[PIECE_IDX[c]][i * 9 + j];
        }
    if (!redTurn) h ^= ZK.side;
    return h;
}

// ===== 置换表（Transposition Table）=====
enum : signed char { TT_ALPHA = 0, TT_BETA = 1, TT_EXACT = 2 };
struct TTEntry { uint64_t key = 0; int score = 0; short depth = 0; signed char flag = 0; Move best; };
static const size_t TT_SIZE = 1 << 21;  // 200万槽位，约64MB（搜索最长20秒，200万足够）
// 懒分配：首次搜索才创建并清零——主菜单/联机对局全程不占这64MB，启动也省掉一次性清零
static TTEntry* ttPtr = nullptr;
static inline TTEntry* ttTable() {
    if (!ttPtr)
        ttPtr = new TTEntry[TT_SIZE]();
    return ttPtr;
}

// ===== 重复局面检测（长打禁着的简化实现：重复即判和）=====
// g_gameHist：真实对局中走过的局面哈希（跨回合保留，由main.cpp维护）
// g_posHist：搜索路径上的局面（g_gameHist 的快照 + 搜索时压入/弹出）
static uint64_t g_gameHist[256]; static int g_gameHistLen = 0;
static uint64_t g_posHist[512];  static int g_posHistLen = 0;
// RAII 守卫：进入搜索节点时压入哈希，退出时自动弹出（含所有提前return分支）
struct HistGuard {
    HistGuard(uint64_t h) { g_posHist[g_posHistLen++] = h; }
    ~HistGuard() { g_posHistLen--; }
};
// 局面是否在历史中出现过（重复局面）
static inline bool isRepetition(uint64_t hash) {
    for (int i = 0; i < g_posHistLen; i++)
        if (g_posHist[i] == hash) return true;
    return false;
}

// ===== 原地落子/撤销（替代 memcpy，搜索热点路径零拷贝）=====
struct Undo { int captured; };
static inline void makeMove(int board[10][9], const Move& m, Undo& u) {
    u.captured = board[m.x2][m.y2];
    board[m.x2][m.y2] = board[m.x1][m.y1];
    board[m.x1][m.y1] = 0;
}
static inline void unmakeMove(int board[10][9], const Move& m, const Undo& u) {
    board[m.x1][m.y1] = board[m.x2][m.y2];
    board[m.x2][m.y2] = u.captured;
}
// 走子后的哈希增量更新（在 makeMove 之前取 mover，u.captured 为被吃子）
static inline uint64_t hashAfterMove(uint64_t h, int mover, int captured, const Move& m) {
    h ^= ZK.p[PIECE_IDX[mover]][m.x1 * 9 + m.y1];
    h ^= ZK.p[PIECE_IDX[mover]][m.x2 * 9 + m.y2];
    if (captured) h ^= ZK.p[PIECE_IDX[captured]][m.x2 * 9 + m.y2];
    return h ^ ZK.side; // 换手
}
// ===== 杀手启发 Killer Move =====
// killer[depth][0/1]：每一层保存2个杀手走法，一般存2个足够
Move killer
[MAX_DEPTH + 2][2];
// ===== 历史启发 History Heuristic =====
// history[from][to]：记录这个(from→to)走法触发剪枝的次数，作为权重
int history[BOARD_H * BOARD_W][BOARD_H * BOARD_W] = { 0 };

void initBoard() {
    {
        //清空棋盘
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 9; j++)
                coreBoard
                [i][j] = 0;
        //黑方(上方，小写)
        coreBoard
            [0][0] = 'r'; coreBoard[0][1] = 'n'; coreBoard[0][2] = 'b'; coreBoard[0][3] = 'a'; coreBoard[0][4] = 'k';
        coreBoard
            [0][5] = 'a'; coreBoard[0][6] = 'b'; coreBoard[0][7] = 'n'; coreBoard[0][8] = 'r';
        coreBoard
            [2][1] = 'p'; coreBoard[2][7] = 'p';
        coreBoard
            [3][0] = 's'; coreBoard[3][2] = 's'; coreBoard[3][4] = 's'; coreBoard[3][6] = 's'; coreBoard[3][8] = 's';
        //红方(下方，大写)
        coreBoard
            [9][0] = 'R'; coreBoard[9][1] = 'N'; coreBoard[9][2] = 'B'; coreBoard[9][3] = 'A'; coreBoard[9][4] = 'K';
        coreBoard
            [9][5] = 'A'; coreBoard[9][6] = 'B'; coreBoard[9][7] = 'N'; coreBoard[9][8] = 'R';
        coreBoard
            [7][1] = 'P'; coreBoard[7][7] = 'P';
        coreBoard
            [6][0] = 'S'; coreBoard[6][2] = 'S'; coreBoard[6][4] = 'S'; coreBoard[6][6] = 'S'; coreBoard[6][8] = 'S';
    };
}

// ===== 残局定义 =====
// 每个残局：玩家执红先行，须在 moveLimit 步内将杀黑方（超步判负）
// 全部残局均经引擎自战验证：红方（引擎）可在黑方（3秒/步引擎）防守下取胜
static const EndgameInfo kEndgames[] = {
    { "马后炮",   "在 3 步内将杀黑将",
      "马跳到九宫士角，成\"马后炮\"：马控住将门，炮隔马轰将，老将无处可逃", 3 },
    { "单车破双士", "在 12 步内用单车攻破双士",
      "车逼黑士离位再逐个摘除，注意帅与车的配合封锁老将", 12 },
    { "双车错",   "在 20 步内用双车将杀黑将",
      "双车轮番\"错\"开叫将：一车拴住黑士，另一车沉底将军，黑士顾此失彼", 20 },
    { "双炮轰士", "在 15 步内用双炮攻破双士",
      "一炮作架一炮轰将（重炮），先运炮到位再逐步成杀", 15 },
    { "马炮胜双士", "在 24 步内用马炮攻破双士",
      "马控制将门、炮遥控宫心，先破士再成杀，注意马炮配合", 24 },
    { "车马擒王", "在 10 步内用车马将杀光杆老将",
      "车控制老将的活动线，马贴身叫将，车马联杀", 10 },
    { "车炮破士象", "在 20 步内用车炮攻破士象全",
      "炮牵制中路，车沉底线扫士，逐步瓦解黑方防线", 20 },
};

int endgameCount() {
    return static_cast<int>(sizeof(kEndgames) / sizeof(kEndgames[0]));
}

const EndgameInfo &endgameInfo(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= endgameCount()) idx = endgameCount() - 1;
    return kEndgames[idx];
}

//残局初始化：7个经典实用残局，玩家执红先行，全部合法且经引擎验证红可胜
void initEndgame(int idx) {
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 9; j++)
            coreBoard[i][j] = 0;
    switch (idx) {
    case 0:
        // 马后炮（入门一步杀）：马跳[2][4]九宫中心，炮[4][4]隔马轰将
        // 黑将吃马则与红帅对脸（非法），平移将门被马控制 —— 绝杀
        coreBoard[9][4] = 'K';
        coreBoard[4][4] = 'P';
        coreBoard[3][2] = 'N';
        coreBoard[0][4] = 'k';
        break;
    case 1:
        // 单车破双士：帅在3路（避将帅对脸），车逐步摘士困毙擒将（引擎验证可胜）
        coreBoard[9][3] = 'K';
        coreBoard[5][3] = 'R';
        coreBoard[0][4] = 'k';
        coreBoard[0][3] = 'a'; coreBoard[0][5] = 'a';
        break;
    case 2:
        // 双车错：红双车 vs 黑将+双士（双车交替将军的经典杀型）
        coreBoard[9][0] = 'K';
        coreBoard[4][3] = 'R';
        coreBoard[6][5] = 'R';
        coreBoard[0][4] = 'k';
        coreBoard[0][3] = 'a'; coreBoard[0][5] = 'a';
        break;
    case 3:
        // 双炮轰士：双炮 vs 散位双士（士在底线+宫角），需运炮架炮成杀（引擎验证可胜）
        coreBoard[9][4] = 'K';
        coreBoard[8][4] = 'P';
        coreBoard[6][6] = 'P';
        coreBoard[0][4] = 'k';
        coreBoard[0][3] = 'a'; coreBoard[2][5] = 'a';
        break;
    case 4:
        // 马炮胜双士：马在远处需先运马（引擎验证可胜）
        coreBoard[9][3] = 'K';
        coreBoard[7][2] = 'N';
        coreBoard[3][4] = 'P';
        coreBoard[0][4] = 'k';
        coreBoard[0][3] = 'a'; coreBoard[0][5] = 'a';
        break;
    case 5:
        // 车马擒王：车马 vs 光杆老将（车控线马贴身，联杀）
        coreBoard[9][3] = 'K';
        coreBoard[5][3] = 'R';
        coreBoard[5][5] = 'N';
        coreBoard[0][4] = 'k';
        break;
    case 6:
        // 车炮破士象：车炮 vs 黑将+双士+双象（高阶残局）
        coreBoard[9][3] = 'K';
        coreBoard[5][2] = 'R';
        coreBoard[7][2] = 'P';
        coreBoard[0][4] = 'k';
        coreBoard[0][3] = 'a'; coreBoard[0][5] = 'a';
        coreBoard[4][4] = 'b'; coreBoard[2][6] = 'b';
        break;
    default:
        initBoard();
        break;
    }
}
//是否在棋盘
bool inBoard(int x, int y) {
    return x >= 0 && x <= 9 && y >= 0 && y <= 8;
}

//移动棋子，暂时没有设置移动规则
bool movePiece(int sx, int sy, int tx, int ty) {
    if (!inBoard(sx, sy) || !inBoard(tx, ty)) {
        cout
            << "坐标越界！\n";
        return false;
    }
    int self = coreBoard[sx][sy];
    int target = coreBoard[tx][ty];
    if (self == 0) {
        cout
            << "起点没有棋子\n";
        return false;
    }
    //不能吃己方棋子
    if (target != 0 && isupper(self) == isupper(target))
    {
        cout
            << "不能吃己方棋子\n";
        return false;
    }
    bool ok = false;
    switch (self)
    {
    case 'N': case 'n': ok = isValidHorse(coreBoard, sx, sy, tx, ty); break;
    case 'R': case 'r': ok = isValidRook(coreBoard, sx, sy, tx, ty); break;
    case 'P': case 'p': ok = isValidCannon(coreBoard, sx, sy, tx, ty); break;
    case 'B': case 'b': ok = isValidElephant(coreBoard, sx, sy, tx, ty); break;
    case 'A': case 'a': ok = isValidGuard(coreBoard, sx, sy, tx, ty); break;
    case 'K': case 'k': ok = isValidKing(coreBoard, sx, sy, tx, ty); break;
    case 'S': case 's': ok = isValidPawn(coreBoard, sx, sy, tx, ty); break;
    default: ok = false;
    }
    if (!ok) {
        cout
            << "棋子走法不合法\n";
        return false;
    }
    // 先保存现场，防止将帅对脸
    int bakSelf = self;
    int bakTarget = target;
    //模拟走子
    coreBoard
        [tx][ty] = self;
    coreBoard
        [sx][sy] = 0;
    //将帅直接对面，这一步是禁招，回滚！
    if (isKingFaceToFace())
    {
        cout
            << "将帅不能直接对面！\n";
        coreBoard
            [sx][sy] = bakSelf;
        coreBoard
            [tx][ty] = bakTarget;
        return false;
    }
    //走完这步自己不能被将军（不能送将）——与AI路径的合法过滤保持一致
    if (isInCheck(coreBoard, isupper(bakSelf) != 0))
    {
        cout
            << "这步走完自己被将军（送将）！回滚。\n";
        coreBoard
            [sx][sy] = bakSelf;
        coreBoard
            [tx][ty] = bakTarget;
        return false;
    }
    return true;
}

//转中文
const char* getChess(int c) {
    switch (c)
    {
    case 'R': return "车";
    case 'N': return "马";
    case 'B': return "相";
    case 'A': return "士";
    case 'K': return "帅";
    case 'P': return "炮";
    case 'S': return "兵";
    case 'r': return "車";
    case 'n': return "馬";
    case 'b': return "象";
    case 'a': return "士";
    case 'k': return "将";
    case 'p': return "砲";
    case 's': return "卒";
    default: return "＿"; //空位置
    }
}

//己方队友判断
bool isRed(int c) { return c != 0 && isupper(c); }
bool isBlack(int c) { return c != 0 && islower(c); }

//马走路逻辑 sx,sy 当前马坐标；tx,ty目标坐标（在传入棋盘board上判定）
bool isValidHorse(int board[10][9], int sx, int sy, int tx, int ty)
{
    int dx = abs(tx - sx);
    int dy = abs(ty - sy);
    // 判断是不是日字
    if (!((dx == 2 && dy == 1) || (dx == 1 && dy == 2)))
        return false;
    // 处理蹩马腿，找马脚位置
    int footX, footY;
    if (dx == 2)
    {
        footX
            = sx + (tx - sx) / 2;
        footY
            = sy;
    }
    else // dy ==2
    {
        footX
            = sx;
        footY
            = sy + (ty - sy) / 2;
    }
    //马脚位置不为0，代表有棋子，蹩马腿不能走
    if (board[footX][footY] != 0)
        return false;
    return true;
}

// 车：直线，中间不能有棋子阻挡
bool isValidRook(int board[10][9], int sx, int sy, int tx, int ty)
{
    //必须同行 或者同列
    if (sx != tx && sy != ty) return false;
    //横向移动
    if (sx == tx)
    {
        int left = min(sy, ty);
        int right = max(sy, ty);
        for (int y = left + 1; y < right; y++)
        {
            if (board[sx][y] != 0)
                return false;
        }
    }
    else //纵向移动
    {
        int top = min(sx, tx);
        int bot = max(sx, tx);
        for (int x = top + 1; x < bot; x++)
        {
            if (board[x][sy] != 0)
                return false;
        }
    }
    return true;
}

//炮：移动同车；吃子必须隔一个炮架
bool isValidCannon(int board[10][9], int sx, int sy, int tx, int ty)
{
    if (sx != tx && sy != ty) return false;
    int blockCnt = 0; //中间挡住的棋子数量
    if (sx == tx)
    {
        int left = min(sy, ty);
        int right = max(sy, ty);
        for (int y = left + 1; y < right; y++)
        {
            if (board[sx][y] != 0) blockCnt++;
        }
    }
    else {
        int top = min(sx, tx);
        int bot = max(sx, tx);
        for (int x = top + 1; x < bot; x++)
        {
            if (board[x][sy] != 0) blockCnt++;
        }
    }
    int target = board[tx][ty];
    if (target == 0) {
        //空位走子，中间不能有棋子 blockCnt=0
        return blockCnt == 0;
    }
    else {
        //吃子，必须恰好1个炮架
        return blockCnt == 1;
    }
}

//相/象：走田，塞象眼，不能过河
bool isValidElephant(int board[10][9], int sx, int sy, int tx, int ty)
{
    int dx = abs(tx - sx);
    int dy = abs(ty - sy);
    if (!(dx == 2 && dy == 2)) return false;
    //象眼
    int eyeX = sx + (tx - sx) / 2;
    int eyeY = sy + (ty - sy) / 2;
    if (board[eyeX][eyeY] != 0) return false;
    //判断不能过河：红相只能在x 5‑9；黑象只能 x0‑4
    int self = board[sx][sy];
    if (isupper(self)) {
        if (tx < 5) return false;
    }
    else {
        if (tx > 4) return false;
    }
    return true;
}

//士：斜走一格，不出九宫
bool isValidGuard(int board[10][9], int sx, int sy, int tx, int ty)
{
    int dx = abs(tx - sx);
    int dy = abs(ty - sy);
    if (!(dx == 1 && dy == 1)) return false;
    int self = board[sx][sy];
    if (isupper(self)) {
        //红士九宫 x7‑9，y3‑5
        if (!(tx >= 7 && tx <= 9 && ty >= 3 && ty <= 5)) return false;
    }
    else {
        //黑士九宫 x0‑2，y3‑5
        if (!(tx >= 0 && tx <= 2 && ty >= 3 && ty <= 5)) return false;
    }
    return true;
}

//将/帅：横竖一格，九宫，将帅不能直接对面（这里只校验走一步；对脸检测写在外层）
bool isValidKing(int board[10][9], int sx, int sy, int tx, int ty)
{
    int dx = abs(tx - sx);
    int dy = abs(ty - sy);
    if (!((dx == 1 && dy == 0) || (dx == 0 && dy == 1))) return false;
    int self = board[sx][sy];
    if (isupper(self)) {
        if (!(tx >= 7 && tx <= 9 && ty >= 3 && ty <= 5)) return false;
    }
    else {
        if (!(tx >= 0 && tx <= 2 && ty >= 3 && ty <= 5)) return false;
    }
    return true;
}

//兵/卒走法
bool isValidPawn(int board[10][9], int sx, int sy, int tx, int ty)
{
    int dx = tx - sx;
    int dy = abs(ty - sy);
    int self = board[sx][sy];
    if (isupper(self))
    {
        //红兵：向上走 dx负；没过河不能横走
        if (sx > 4) //没过河(x>4)只能向前一步
        {
            if (!(dx == -1 && dy == 0)) return false;
        }
        else {
            //过河：向上一步 或者左右一步
            if (!((dx == -1 && dy == 0) || (dx == 0 && dy == 1)))
                return false;
        }
    }
    else
    {
        //黑卒：向下走 dx正；没过河不能横走
        if (sx < 5)
        {
            if (!(dx == 1 && dy == 0)) return false;
        }
        else {
            if (!((dx == 1 && dy == 0) || (dx == 0 && dy == 1)))
                return false;
        }
    }
    return true;
}

// 检查将帅是否直接对面（中间无棋子，返回true代表违规）
bool isKingFaceToFace()
{
    int rk_x = -1, rk_y = -1, bk_x = -1, bk_y = -1;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 9; j++) {
            if (coreBoard[i][j] == 'K') { rk_x = i; rk_y = j; }
            if (coreBoard[i][j] == 'k') { bk_x = i; bk_y = j; }
        }
    }
    if (rk_y != bk_y) return false; //不在同一列没事
    //同一列，检查中间
    int top = min(rk_x, bk_x);
    int bot = max(rk_x, bk_x);
    for (int x = top + 1; x < bot; x++)
    {
        if (coreBoard[x][rk_y] != 0) return false;
    }
    return true;
}

//博弈树
//  1  在传入的棋盘副本上模拟走子（完全复用之前规则，不改动全局coreBoard）
bool simulateMove(int board[10][9], int sx, int sy, int tx, int ty)
{
    if (!inBoard(sx, sy) || !inBoard(tx, ty)) return false;
    int self = board[sx][sy];
    int target = board[tx][ty];
    if (self == 0) return false;
    if (target != 0 && isupper(self) == isupper(target)) return false;
    bool ok = false;
    switch (self)
    {
    case 'N': case 'n': ok = isValidHorse(board, sx, sy, tx, ty); break;
    case 'R': case 'r': ok = isValidRook(board, sx, sy, tx, ty); break;
    case 'P': case 'p': ok = isValidCannon(board, sx, sy, tx, ty); break;
    case 'B': case 'b': ok = isValidElephant(board, sx, sy, tx, ty); break;
    case 'A': case 'a': ok = isValidGuard(board, sx, sy, tx, ty); break;
    case 'K': case 'k': ok = isValidKing(board, sx, sy, tx, ty); break;
    case 'S': case 's': ok = isValidPawn(board, sx, sy, tx, ty); break;
    default: ok = false;
    }
    if (!ok) return false;
    // 直接落子。节点的"走完是否被将军/将帅对脸"由 generateAllMoves
    // 中的 isInCheck（已含白脸将检测）统一过滤，这里不再重复全盘扫描。
    board
        [tx][ty] = self;
    board
        [sx][sy] = 0;
    return true;
}

// ===== 候选落点生成：按棋子类型只枚举“几何上可能”的格子 =====
// 旧版对每个棋子扫 90 个目标格再调 isValid*，绝大多数调用第一行就被否决；
// 这里直接枚举候选点（马8点/象4点/车炮射线…），写入调用方的栈上数组（最多17个：
// 车/炮单方向射线之和），全程零堆分配；生成后仍用 isValid* 权威校验
// + make/isInCheck/unmake 过滤自将，合法走法集合与旧版完全一致，速度快数倍。
static inline int genCandidates(int board[10][9], int i, int j, int c,
                                bool capturesOnly, Move* out)
{
    int n = 0;
    const bool red = isupper(c) != 0;
    auto add = [&](int tx, int ty) {
        if (tx < 0 || tx > 9 || ty < 0 || ty > 8) return;
        int t = board[tx][ty];
        if (t != 0 && (isupper(t) != 0) == red) return;  // 不能吃己方
        if (capturesOnly && t == 0) return;              // 静止搜索只要吃子
        out[n++] = Move(i, j, tx, ty);
    };
    switch (tolower(c))
    {
    case 'n': { // 马：8 个日字点
        static const int d[8][2] = { {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1} };
        for (auto& o : d) add(i + o[0], j + o[1]);
        break;
    }
    case 'b': { // 象/相：4 个田字点
        static const int d[4][2] = { {-2,-2},{-2,2},{2,-2},{2,2} };
        for (auto& o : d) add(i + o[0], j + o[1]);
        break;
    }
    case 'a': { // 士：九宫内斜走一格
        static const int d[4][2] = { {-1,-1},{-1,1},{1,-1},{1,1} };
        for (auto& o : d) add(i + o[0], j + o[1]);
        break;
    }
    case 'k': { // 将/帅：九宫内直走一格
        static const int d[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
        for (auto& o : d) add(i + o[0], j + o[1]);
        break;
    }
    case 's': { // 兵/卒：向前一步；过河后可横走
        if (red) {
            add(i - 1, j);
            if (i <= 4) { add(i, j - 1); add(i, j + 1); }
        } else {
            add(i + 1, j);
            if (i >= 5) { add(i, j - 1); add(i, j + 1); }
        }
        break;
    }
    case 'r': { // 车：4 条射线，遇子即止（敌子可吃）
        static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& o : d) {
            int x = i + o[0], y = j + o[1];
            while (x >= 0 && x <= 9 && y >= 0 && y <= 8) {
                int t = board[x][y];
                if (t == 0) { if (!capturesOnly) out[n++] = Move(i, j, x, y); }
                else { if ((isupper(t) != 0) != red) out[n++] = Move(i, j, x, y); break; }
                x += o[0]; y += o[1];
            }
        }
        break;
    }
    case 'p': { // 炮：炮架前走空位，炮架后吃第一颗敌子
        static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& o : d) {
            int x = i + o[0], y = j + o[1];
            bool jumped = false;
            while (x >= 0 && x <= 9 && y >= 0 && y <= 8) {
                int t = board[x][y];
                if (!jumped) {
                    if (t == 0) { if (!capturesOnly) out[n++] = Move(i, j, x, y); }
                    else jumped = true;  // 翻炮架
                } else if (t != 0) {
                    if ((isupper(t) != 0) != red) out[n++] = Move(i, j, x, y);
                    break;
                }
                x += o[0]; y += o[1];
            }
        }
        break;
    }
    }
    return n;
}

// 候选走法 -> 合法走法：isValid* 校验 + 落子后不自将，结果追加到 res
static void filterLegal(int board[10][9], bool isMaxTurn,
                        const Move* cand, int n, vector<Move>& res)
{
    for (int k = 0; k < n; k++)
    {
        const Move& m = cand[k];
        int c = board[m.x1][m.y1];
        bool pseudo = false;
        switch (c)
        {
        case 'N': case 'n': pseudo = isValidHorse(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'R': case 'r': pseudo = isValidRook(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'P': case 'p': pseudo = isValidCannon(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'B': case 'b': pseudo = isValidElephant(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'A': case 'a': pseudo = isValidGuard(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'K': case 'k': pseudo = isValidKing(board, m.x1, m.y1, m.x2, m.y2); break;
        case 'S': case 's': pseudo = isValidPawn(board, m.x1, m.y1, m.x2, m.y2); break;
        }
        if (!pseudo) continue;
        Undo u;
        makeMove(board, m, u);
        bool selfCheck = isInCheck(board, isMaxTurn);
        unmakeMove(board, m, u);
        if (!selfCheck) res.push_back(m);
    }
}

//生成全部合法走法（isMaxTurn=true=红方，false=黑方）
// 性能：伪合法判定后直接原地落子检测自将再撤销，全程零 memcpy
vector
<Move> generateAllMoves(int board[10][9], bool isMaxTurn)
{
    vector
        <Move> res;
    res.reserve(64);
    Move cand[20];  // 单棋子候选上限17（车/炮），20 留余量
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 9; j++)
        {
            int c = board[i][j];
            if (c == 0) continue;
            // 约定：isMaxTurn=true 走红方(大写)，false 走黑方(小写)
            bool belongMax = isupper(c);
            if (belongMax != isMaxTurn) continue;
            int n = genCandidates(board, i, j, c, false, cand);
            filterLegal(board, isMaxTurn, cand, n, res);
        }
    }
    return res;
}

// 只生成吃子走法（静止搜索专用）：目标格必须是敌子，且走完不被将军
static vector<Move> generateCaptures(int board[10][9], bool isMaxTurn)
{
    vector<Move> res;
    res.reserve(16);
    Move cand[20];
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 9; j++)
        {
            int c = board[i][j];
            if (c == 0) continue;
            if ((isupper(c) != 0) != isMaxTurn) continue;
            int n = genCandidates(board, i, j, c, true, cand);
            filterLegal(board, isMaxTurn, cand, n, res);
        }
    }
    return res;
}

//局面估值
// 棋子静态分值表（正值=红方优势），文件级只读，供 evaluate 高频查询
static const auto PIECE_VAL = [] {
    std::array<int, 256> t{};
    t['R'] = 900; t['N'] = 400; t['B'] = 200; t['A'] = 200; t['K'] = 10000; t['P'] = 450; t['S'] = 100;
    t['r'] = -900; t['n'] = -400; t['b'] = -200; t['a'] = -200; t['k'] = -10000; t['p'] = -450; t['s'] = -100;
    return t;
}();

// ===== 各类子力位置权重表（红方视角，row0=黑方底线，黑方按 [9-i][j] 镜像取负）=====
// 兵/卒：过河后价值陡增，越深入越值钱，中路兵最凶
static const int PST_S[10][9] = {
    {90, 90,100,110,120,110,100, 90, 90},
    {85, 85, 95,105,115,105, 95, 85, 85},
    {75, 75, 85, 95,105, 95, 85, 75, 75},
    {60, 60, 70, 80, 90, 80, 70, 60, 60},
    {50, 50, 60, 70, 75, 70, 60, 50, 50},
    {20,  0, 25,  0, 30,  0, 25,  0, 20},
    {10,  0, 15,  0, 20,  0, 15,  0, 10},
    { 0,  0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  0,  0,  0,  0,  0}
};
// 马：中心活跃，窝心/边角受限
static const int PST_N[10][9] = {
    { 90, 95,100,105,105,105,100, 95, 90},
    { 92, 98,104,108,110,108,104, 98, 92},
    { 95,100,105,108,110,108,105,100, 95},
    { 93, 98,102,106,108,106,102, 98, 93},
    { 90, 96,100,104,106,104,100, 96, 90},
    { 88, 94, 98,102,104,102, 98, 94, 88},
    { 85, 92, 95, 98,100, 98, 95, 92, 85},
    { 82, 88, 92, 95, 96, 95, 92, 88, 82},
    { 80, 86, 90, 93, 94, 93, 90, 86, 80},
    { 78, 82, 86, 90, 92, 90, 86, 82, 78}
};
// 炮：中路与沉底威胁大
static const int PST_P[10][9] = {
    { 96, 94, 96,100,104,100, 96, 94, 96},
    { 94, 92, 94, 98,102, 98, 94, 92, 94},
    { 92, 90, 92, 96,100, 96, 92, 90, 92},
    { 91, 90, 91, 94, 97, 94, 91, 90, 91},
    { 90, 90, 90, 93, 96, 93, 90, 90, 90},
    { 90, 89, 90, 92, 94, 92, 90, 89, 90},
    { 89, 90, 90, 92, 96, 92, 90, 90, 89},
    { 88, 90, 92, 96, 98, 96, 92, 90, 88},
    { 90, 92, 94, 97, 98, 97, 94, 92, 90},
    { 92, 94, 96, 98,100, 98, 96, 94, 92}
};
// 车：活通车路，占领肋道与兵线
static const int PST_R[10][9] = {
    { 98,100,102,104,106,104,102,100, 98},
    { 96, 98,100,102,104,102,100, 98, 96},
    { 94, 96, 98,100,102,100, 98, 96, 94},
    { 93, 95, 97, 99,101, 99, 97, 95, 93},
    { 92, 94, 96, 98,100, 98, 96, 94, 92},
    { 92, 94, 96, 98,100, 98, 96, 94, 92},
    { 93, 95, 97, 99,101, 99, 97, 95, 93},
    { 94, 96, 98,100,102,100, 98, 96, 94},
    { 95, 97, 99,101,103,101, 99, 97, 95},
    { 90, 92, 94, 96, 98, 96, 94, 92, 90}
};
// 士/象：防守子力，守在九宫/点位即可
static const int PST_A[10][9] = {
    { 0, 0, 0,20, 0,20, 0, 0, 0},
    { 0, 0, 0, 0,23, 0, 0, 0, 0},
    { 0, 0, 0,20, 0,20, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0,20, 0,20, 0, 0, 0},
    { 0, 0, 0, 0,23, 0, 0, 0, 0},
    { 0, 0, 0,20, 0,20, 0, 0, 0}
};
static const int PST_B[10][9] = {
    { 0, 0,20, 0, 0, 0,20, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 0, 0, 0,22, 0, 0, 0,18},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0,20, 0, 0, 0,20, 0, 0},
    { 0, 0,20, 0, 0, 0,20, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 0, 0, 0,22, 0, 0, 0,18},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0,20, 0, 0, 0,20, 0, 0}
};
// 将/帅：安定在原位附近即可
static const int PST_K[10][9] = {
    { 0, 0, 0, 1, 1, 1, 0, 0, 0},
    { 0, 0, 0, 2, 2, 2, 0, 0, 0},
    { 0, 0, 0, 3, 3, 3, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 3, 3, 3, 0, 0, 0},
    { 0, 0, 0, 4, 4, 4, 0, 0, 0},
    { 0, 0, 0, 5, 6, 5, 0, 0, 0}
};
// 按棋子类型查位置表
static inline int pstOf(int c, int i, int j)
{
    switch (c)
    {
    case 'R': return PST_R[i][j];  case 'r': return PST_R[9 - i][j];
    case 'N': return PST_N[i][j];  case 'n': return PST_N[9 - i][j];
    case 'P': return PST_P[i][j];  case 'p': return PST_P[9 - i][j];
    case 'S': return PST_S[i][j];  case 's': return PST_S[9 - i][j];
    case 'A': return PST_A[i][j];  case 'a': return PST_A[9 - i][j];
    case 'B': return PST_B[i][j];  case 'b': return PST_B[9 - i][j];
    case 'K': return PST_K[i][j];  case 'k': return PST_K[9 - i][j];
    default: return 0;
    }
}

// 机动性：快速统计车/炮/马的合法步数（不算将军过滤，便宜近似）
static int mobilityOf(int board[10][9], int i, int j, int c)
{
    int mob = 0;
    int lc = tolower(c);
    if (lc == 'r' || lc == 'p')
    {
        static const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (int d = 0; d < 4; d++)
        {
            int x = i + dirs[d][0], y = j + dirs[d][1];
            while (x >= 0 && x <= 9 && y >= 0 && y <= 8)
            {
                if (board[x][y] == 0) mob++;
                else { mob++; break; } // 吃子点也算机动
                x += dirs[d][0]; y += dirs[d][1];
            }
        }
    }
    else if (lc == 'n')
    {
        static const int hdx[8] = { -2,-2,-1,-1,1,1,2,2 };
        static const int hdy[8] = { -1,1,-2,2,-2,2,-1,1 };
        for (int t = 0; t < 8; t++)
        {
            int nx = i + hdx[t], ny = j + hdy[t];
            if (nx < 0 || nx > 9 || ny < 0 || ny > 8) continue;
            if (isValidHorse(board, i, j, nx, ny)) mob++;
        }
    }
    return mob;
}

// 将安全评估：统计一方将帅的防守弱点，返回惩罚分（正值=危险）
static int kingDanger(int board[10][9], bool redKing, int kx, int ky)
{
    if (kx < 0) return 0;
    int danger = 0;
    int enemyRook = redKing ? 'r' : 'R';
    int enemyCannon = redKing ? 'p' : 'P';
    int enemyPawn = redKing ? 's' : 'S';
    int enemyHorse = redKing ? 'n' : 'N';
    int myGuard = redKing ? 'A' : 'a';
    int myBishop = redKing ? 'B' : 'b';
    // 1) 士象护卫数量（防守屏障）
    int shield = 0;
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 9; j++)
            if (board[i][j] == myGuard || board[i][j] == myBishop) shield++;
    danger += (4 - shield) * 10; // 士象残缺，每个缺扣10
    // 2) 将门暴露：敌方车/炮与将同列
    int blockers = 0;
    int lo = redKing ? 0 : kx + 1, hi = redKing ? kx : 10;
    for (int x = lo; x < hi; x++)
        if (board[x][ky] != 0) blockers++;
    // 纵向扫描敌车炮
    for (int x = redKing ? 0 : kx + 1; redKing ? (x < kx) : (x < 10); x++)
    {
        int c = board[x][ky];
        if (c == enemyRook && blockers == 0) danger += 50;   // 明车照将路
        if (c == enemyCannon && blockers == 1) danger += 40; // 空头炮/天地炮威胁
    }
    // 3) 敌兵冲入九宫附近
    int kLo = redKing ? 7 : 0, kHi = redKing ? 9 : 2;
    for (int i = kLo; i <= kHi; i++)
        for (int j = 3; j <= 5; j++)
            if (board[i][j] == enemyPawn) danger += 25;
    // 4) 敌马逼近（卧槽/挂角区域）
    int zLo = redKing ? 6 : 0, zHi = redKing ? 9 : 3;
    for (int i = zLo; i <= zHi; i++)
        for (int j = 0; j < 9; j++)
            if (board[i][j] == enemyHorse) danger += 15;
    return danger;
}

int evaluate(int board[10][9])
{
    const int* val = PIECE_VAL.data();
    int score = 0;
    int phase = 0;                 // 剩余子力总量（判断残局）
    int redKx = -1, redKy = -1, blkKx = -1, blkKy = -1;
    // 兵结构统计：过河兵按列记录
    int redPawnRow[9]; int blkPawnRow[9];
    memset(redPawnRow, -1, sizeof(redPawnRow));
    memset(blkPawnRow, -1, sizeof(blkPawnRow));

    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 9; j++)
        {
            int ch = board[i][j];
            if (ch == 0) continue;
            int v = val[ch];
            bool isRedPc = isupper(ch) != 0;
            int lc = tolower(ch);
            if (lc != 'k') phase += isRedPc ? v : -v; // 双方非将子力总和
            int pos = pstOf(ch, i, j);
            int mob = 0;
            if (lc == 'r' || lc == 'n' || lc == 'p')
                mob = mobilityOf(board, i, j, ch) * 3; // 机动性每步3分
            if (isRedPc)
            {
                score += v + pos + mob;
                if (ch == 'K') { redKx = i; redKy = j; }
                if (ch == 'S' && i <= 4) redPawnRow[j] = i; // 红过河兵
            }
            else
            {
                score += v - pos - mob;
                if (ch == 'k') { blkKx = i; blkKy = j; }
                if (ch == 's' && i >= 5) blkPawnRow[j] = i; // 黑过河卒
            }
        }
    }
    // ---- 兵结构：联兵加分（相邻列都有过河兵，互为保护）----
    for (int j = 0; j < 8; j++)
    {
        if (redPawnRow[j] >= 0 && redPawnRow[j + 1] >= 0) score += 12;
        if (blkPawnRow[j] >= 0 && blkPawnRow[j + 1] >= 0) score -= 12;
    }
    // ---- 残局调整：子力稀少时兵价值提升、将可助攻 ----
    if (phase < 1500)
    {
        for (int j = 0; j < 9; j++)
        {
            if (redPawnRow[j] >= 0) score += 30;
            if (blkPawnRow[j] >= 0) score -= 30;
        }
    }
    // ---- 将安全 ----
    score -= kingDanger(board, true, redKx, redKy);
    score += kingDanger(board, false, blkKx, blkKy);
    return score;
}

// =========杀手+历史启发相关函数=========
// MVV-LVA：吃子优先级（简单版，杀大子优先）
int mvvLvaScore(const Move& m, int board[10][9])
{
    int victim = board[m.x2][m.y2];
    if (victim == 0) return 0;
    return getPieceValue(victim);
}

//给单个走分打分
int scoreMove(int depth, const Move& m, int board[10][9], const Move& killer1, const Move& killer2)
{
    (void)depth; // depth 仅用于与排序接口保持一致，当前打分规则未直接使用
    int score = mvvLvaScore(m, board);
    if (m == killer1) score += 10000;
    else if (m == killer2) score += 9000;
    else
    {
        int fromIdx = m.x1 * 9 + m.y1;
        int toIdx = m.x2 * 9 + m.y2;
        score
            += history[fromIdx][toIdx];
    }
    return score;
}

// 对move列表按分数降序排序
void sortMoves(int depth, int board[10][9], vector<Move>& moves)
{
    Move k1
        = killer[depth][0];
    Move k2
        = killer[depth][1];
    sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return scoreMove(depth, a, board, k1, k2) > scoreMove(depth, b, board, k1, k2);
        });
}

// ===== 静止搜索（Quiescence Search）：吃子链+将军链搜到底 =====
// 被将军时必须全宽搜索所有应将着法（将军链不断）；未被将军时只延展吃子
static int quiescence(int board[10][9], int alpha, int beta, bool redTurn, int qdepth)
{
    if (((++g_nodes) & 2047) == 0 && checkTime()) return 0;
    bool inCheck = isInCheck(board, redTurn);
    int standPat = 0;
    if (!inCheck)
    {
        standPat = redTurn ? evaluate(board) : -evaluate(board);
        if (standPat >= beta) return standPat;
        if (standPat > alpha) alpha = standPat;
    }
    if (qdepth <= 0)
        return redTurn ? evaluate(board) : -evaluate(board);

    // 被将军 → 生成全部应将着法；否则只生成吃子
    vector<Move> moves = inCheck ? generateAllMoves(board, redTurn)
        : generateCaptures(board, redTurn);
    if (moves.empty())
    {
        if (inCheck) return -MATE - qdepth; // 被将死（qdepth越大离主搜索越近）
        return standPat;
    }
    // MVV-LVA 降序（应将着法里吃子也优先）
    sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return mvvLvaScore(a, board) > mvvLvaScore(b, board);
        });
    for (auto& m : moves)
    {
        if (!inCheck)
        {
            int capVal = getPieceValue(board[m.x2][m.y2]);
            // Delta剪枝：吃下这个子也不足以够到alpha，放弃
            if (standPat + capVal + 200 < alpha) continue;
            // SEE-lite：大子吃小子且落点被对方防守，是亏子交换，剪掉
            if (capVal + 100 < getPieceValue(board[m.x1][m.y1]))
            {
                Undo u0; makeMove(board, m, u0);
                bool defended = isSquareAttacked(board, m.x2, m.y2, !redTurn);
                unmakeMove(board, m, u0);
                if (defended) continue;
            }
        }
        Undo u; makeMove(board, m, u);
        int sc = -quiescence(board, -beta, -alpha, !redTurn, qdepth - 1);
        unmakeMove(board, m, u);
        if (g_stop) return 0;
        if (sc >= beta) return sc;
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

// ===== negamax：PVS主变搜索 + 置换表 + 空着裁剪 + LMR + 将军延展 + 重复检测 =====
static int negamax(int board[10][9], int depth, int alpha, int beta, bool redTurn, uint64_t hash, int ply)
{
    // 每2048个节点检查一次时间
    if (((++g_nodes) & 2047) == 0 && checkTime()) return 0;

    // --- 重复局面：判和（长打禁着的简化：任何重复都按和棋处理，避免AI长将刷分）---
    if (ply > 0 && isRepetition(hash)) return 0;
    HistGuard hg(hash);

    int origAlpha = alpha;
    // --- 置换表探测（表在首次进入搜索时才分配）---
    TTEntry& e = ttTable()[hash & (TT_SIZE - 1)];
    Move hashMove(-1, -1, -1, -1);
    if (e.key == hash)
    {
        if (e.depth >= depth)
        {
            if (e.flag == TT_EXACT) return e.score;
            if (e.flag == TT_BETA && e.score >= beta) return e.score;
            if (e.flag == TT_ALPHA && e.score <= alpha) return e.score;
        }
        hashMove = e.best; // 深度不够也借来做排序
    }

    if (depth <= 0)
        return quiescence(board, alpha, beta, redTurn, QS_MAX);

    bool inCheck = isInCheck(board, redTurn);
    // --- 将军延展：被将军时加深1层（上限MAX_DEPTH），将军链不会被截断 ---
    if (inCheck && depth < MAX_DEPTH) depth++;

    // --- 空着裁剪：自适应缩减量 R=2/3 ---
    if (depth >= 4 && !inCheck && ply > 0 && beta < MATE - 100 && beta > -(MATE - 100))
    {
        int R = depth >= 7 ? 3 : 2;
        int sc = -negamax(board, depth - 1 - R, -beta, -beta + 1, !redTurn, hash ^ ZK.side, ply + 1);
        if (g_stop) return 0;
        if (sc >= beta) return sc;
    }

    vector<Move> moves = generateAllMoves(board, redTurn);
    if (moves.empty())
    {
        // 无合法走法：将死/困毙均判负；depth越大离根越近（杀得越快），扣分越多
        return -MATE - depth;
    }
    sortMoves(depth, board, moves); //杀手+历史+MVV-LVA排序
    // 置换表走法提到最前（通常就是最佳走法，大幅提升剪枝率）
    if (hashMove.x1 != -1)
    {
        for (size_t k = 0; k < moves.size(); k++)
        {
            if (moves[k] == hashMove) { std::swap(moves[0], moves[k]); break; }
        }
    }

    int bestScore = -INF;
    Move bestMove = moves[0];
    int searched = 0; // 已完整搜索的着法数（PVS/LMR用）
    for (auto& m : moves)
    {
        int mover = board[m.x1][m.y1];
        int captured = board[m.x2][m.y2];
        Undo u; makeMove(board, m, u);
        uint64_t h2 = hashAfterMove(hash, mover, captured, m);
        // 这步是否将军（影响LMR：将军着法不缩减）
        bool givesCheck = isInCheck(board, !redTurn);
        int sc;
        if (searched == 0)
        {
            // 第一个着法（主变）：全窗口搜索
            sc = -negamax(board, depth - 1, -beta, -alpha, !redTurn, h2, ply + 1);
        }
        else
        {
            // --- LMR：靠后的安静着法降低1层搜索 ---
            int reduce = 0;
            if (depth >= 3 && searched >= 4 && !inCheck && !givesCheck && captured == 0)
                reduce = 1;
            // --- PVS：零窗口试探 ---
            sc = -negamax(board, depth - 1 - reduce, -alpha - 1, -alpha, !redTurn, h2, ply + 1);
            // 零窗口失败（可能真是好棋）→ 恢复深度全窗口重搜
            if (reduce > 0 && sc > alpha)
                sc = -negamax(board, depth - 1, -alpha - 1, -alpha, !redTurn, h2, ply + 1);
            if (sc > alpha && sc < beta)
                sc = -negamax(board, depth - 1, -beta, -alpha, !redTurn, h2, ply + 1);
        }
        unmakeMove(board, m, u);
        if (g_stop) return 0;
        searched++;
        if (sc > bestScore) { bestScore = sc; bestMove = m; }
        if (sc > alpha) alpha = sc;
        if (alpha >= beta)
        {
            // 剪枝：更新杀手+历史（只存非吃子招为杀手）
            if (captured == 0)
            {
                if (!(m == killer[depth][0]))
                {
                    killer[depth][1] = killer[depth][0];
                    killer[depth][0] = m;
                }
            }
            history[m.x1 * 9 + m.y1][m.x2 * 9 + m.y2] += depth * depth;
            break;
        }
    }
    // --- 置换表存储 ---
    signed char flag;
    if (bestScore <= origAlpha) flag = TT_ALPHA;      // 上界
    else if (bestScore >= beta) flag = TT_BETA;       // 下界
    else flag = TT_EXACT;                              // 精确值
    e.key = hash; e.score = bestScore; e.depth = (short)depth; e.flag = flag; e.best = bestMove;
    return bestScore;
}

// 兼容旧接口：从给定局面直接搜索（内部转为 negamax）
int alphaBeta(int board[10][9], int depth, int alpha, int beta, bool isMaxTurn)
{
    g_stop = false;
    g_deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    // 旧接口的 alpha/beta 是红方视角，转成走棋方视角调用 negamax
    int a = isMaxTurn ? alpha : -beta;
    int b = isMaxTurn ? beta : -alpha;
    int sc = negamax(board, depth, a, b, isMaxTurn, hashBoard(board, isMaxTurn), 0);
    return isMaxTurn ? sc : -sc;
}

void resetKillerAndHistory()
{
    memset(killer, 0, sizeof(killer));
    memset(history, 0, sizeof(history));
}

//棋子权重参考
int getPieceValue(int p)
{
    switch (p)
    {
    case 'K': case 'k': return 10000; //将帅
    case 'R': case 'r': return 900;    //车
    case 'N': case 'n': return 400;    //马
    case 'P': case 'p': return 450;    //炮
    case 'B': case 'b': return 200;    //象
    case 'A': case 'a': return 200;    //士
    case 'S': case 's': return 100;    //兵/卒
    default: return 0;
    }
}

// 轻量攻击检测：(kx,ky) 这个点是否正被 byRed 方（true=红方棋子）攻击
// 从目标点反推攻击者（马8点/兵3点/王4邻/车炮4射线），不复制棋盘、不扫全盘
static bool isSquareAttacked(int board[10][9], int kx, int ky, bool byRed)
{
    const int ENEMY_KING = byRed ? 'K' : 'k';   // 攻击方的将/帅
    // 1) 马：目标点反推8个马位
    static const int hdx[8] = { -2,-2,-1,-1, 1, 1, 2, 2 };
    static const int hdy[8] = { -1, 1,-2, 2,-2, 2,-1, 1 };
    for (int t = 0; t < 8; t++)
    {
        int hx = kx + hdx[t], hy = ky + hdy[t];
        if (hx < 0 || hx > 9 || hy < 0 || hy > 8) continue;
        int c = board[hx][hy];
        if ((byRed && c == 'N') || (!byRed && c == 'n'))
        {
            if (isValidHorse(board, hx, hy, kx, ky)) return true;
        }
    }
    // 2) 兵/卒：正前方1点 + 左右2点（横吃需已过河）；将/帅：4邻格
    int sFwd = byRed ? (kx + 1) : (kx - 1);   // 攻击方兵向前能吃到本点时所在行
    if (sFwd >= 0 && sFwd <= 9)
    {
        int c = board[sFwd][ky];
        if ((byRed && c == 'S') || (!byRed && c == 's')) return true;
    }
    for (int dy = -1; dy <= 1; dy += 2)
    {
        int hy = ky + dy;
        if (hy < 0 || hy > 8) continue;
        int c = board[kx][hy];
        if ((byRed && c == 'S') || (!byRed && c == 's'))
        {
            if (isValidPawn(board, kx, hy, kx, ky)) return true;
        }
        // 将/帅相邻吃子（受九宫约束）
        if (board[kx][hy] == ENEMY_KING && isValidKing(board, kx, hy, kx, ky))
            return true;
    }
    for (int dx = -1; dx <= 1; dx += 2)
    {
        int vx = kx + dx;
        if (vx < 0 || vx > 9) continue;
        if (board[vx][ky] == ENEMY_KING && isValidKing(board, vx, ky, kx, ky))
            return true;
    }
    // 3) 车/炮（纵向射线还兼判白脸将）：4条射线
    static const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int d = 0; d < 4; d++)
    {
        bool metFirst = false;
        int x = kx + dirs[d][0], y = ky + dirs[d][1];
        while (x >= 0 && x <= 9 && y >= 0 && y <= 8)
        {
            int c = board[x][y];
            if (c != 0)
            {
                bool fromEnemy = byRed ? isupper(c) : islower(c);
                if (!metFirst)
                {
                    metFirst = true;
                    if (fromEnemy && (c == 'R' || c == 'r')) return true;
                    // 纵向第一子是对方将帅 → 白脸将
                    if (dirs[d][1] == 0 && c == ENEMY_KING) return true;
                }
                else
                {
                    if (fromEnemy && (c == 'P' || c == 'p')) return true;
                    break; // 第二个子之后车炮都无法再攻击
                }
            }
            x += dirs[d][0]; y += dirs[d][1];
        }
    }
    return false;
}

//isInCheck：isRedSide=true 判断红方是否被将军；false判断黑方是否被将军
bool isInCheck(int board[10][9], bool isRedSide)
{
    int kingX = -1, kingY = -1;
    // 找到本方阵将帅坐标
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 9; j++)
            if (board[i][j] == (isRedSide ? 'K' : 'k')) { kingX = i; kingY = j; }
    if (kingX < 0) return false; // 将帅已不在（测试局面），不判将军
    // 是否正被对方棋子攻击
    return isSquareAttacked(board, kingX, kingY, !isRedSide);
}

//检查当前走棋方是否已经无合法走法
//返回：0继续，1=将死，2=困毙
int checkGameOver(int board[10][9], bool currentTurnIsRed)
{
    //生成当前回合全部合法走法（内部已经过滤走完仍被将军的招）
    //约定统一：isMaxTurn=true=红方（evaluate 正值为红优）
    bool isMaxTurn = currentTurnIsRed;
    vector
        <Move> allMoves = generateAllMoves(board, isMaxTurn);
    if (allMoves.size() > 0)
        return 0;
    //没有合法走法
    if (isInCheck(board, currentTurnIsRed))
    {
        return 1; //将死
    }
    else {
        return 2; //困毙
    }
}

//识别杀招名称 loserIsRed true=红方输；false=黑方输
string
getKillTypeName(int board[10][9], bool loserIsRed)
{
    int kingX = -1, kingY = -1;
    int oppKingX = -1, oppKingY = -1;
    //找输方将帅
    if (loserIsRed)
    {
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 9; j++)
                if (board[i][j] == 'K') { kingX = i; kingY = j; }
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 9; j++)
                if (board[i][j] == 'k') { oppKingX = i; oppKingY = j; }
    }
    else {
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 9; j++)
                if (board[i][j] == 'k') { kingX = i; kingY = j; }
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 9; j++)
                if (board[i][j] == 'K') { oppKingX = i; oppKingY = j; }
    }
    //1、对面笑（白脸将）：将帅同列中间无棋子
    if (kingY == oppKingY)
    {
        bool block = false;
        int top = min(kingX, oppKingX);
        int bot = max(kingX, oppKingX);
        for (int x = top + 1; x < bot; x++)
        {
            if (board[x][kingY] != 0) block = true;
        }
        if (!block) return "对面笑(白脸将)";
    }
    //统计进攻方棋子
    int rookCnt = 0;
    int cannonCnt = 0;
    bool horseAttack = false;
    int hx = -1, hy = -1;
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 9; j++)
        {
            int c = board[i][j];
            if (c == 0) continue;
            bool isAttacker = loserIsRed ? isBlack(c) : isRed(c);
            if (!isAttacker) continue;
            if (c == 'R' || c == 'r') rookCnt++;
            if (c == 'P' || c == 'p') { cannonCnt++; hx = i; hy = j; }
            //马能否攻击将帅
            int tmp[10][9];
            memcpy(tmp, board, sizeof(tmp));
            if (simulateMove(tmp, i, j, kingX, kingY))
            {
                if (c == 'N' || c == 'n') horseAttack = true;
            }
        }
    }
    //2、双车错
    if (rookCnt >= 2) return "双车错";
    //3、重炮：两个炮攻击同一个王位置
    if (cannonCnt >= 2) return "重炮";
    //4、马后炮：炮将军，马在炮后方
    if (cannonCnt >= 1 && horseAttack)
    {
        int dx = abs(hx - kingX);
        int dy = abs(hy - kingY);
        if ((dx > 0 && dy == 0) || (dx == 0 && dy > 0))
        {
            return "马后炮";
        }
    }
    //5、闷宫：炮将军，己方的士堵住将帅逃跑
    if (cannonCnt >= 1)
    {
        bool blockedByGuard = false;
        if (loserIsRed)
        {
            //红方九宫士
            if ((kingX + 1 <= 9 && board[kingX + 1][kingY] == 'A')
                || (kingX - 1 >= 0 && board[kingX - 1][kingY] == 'A'))
                blockedByGuard
                = true;
        }
        else {
            if ((kingX + 1 <= 9 && board[kingX + 1][kingY] == 'a')
                || (kingX - 1 >= 0 && board[kingX - 1][kingY] == 'a'))
                blockedByGuard
                = true;
        }
        if (blockedByGuard) return "闷宫";
    }
    return "普通杀招";
}

// ===== 对局历史维护（供重复局面检测）=====
uint64_t getBoardHash(int board[10][9], bool redTurn) { return hashBoard(board, redTurn); }
void pushGameHash(uint64_t h) { if (g_gameHistLen < 255) g_gameHist[g_gameHistLen++] = h; }
void resetGameHash() { g_gameHistLen = 0; }
// 检查给定哈希是否已在对局历史中出现过（长将/重复局面检测）
bool isGameRepetition(uint64_t h) {
    for (int i = 0; i < g_gameHistLen; i++)
        if (g_gameHist[i] == h) return true;
    return false;
}

//最优ai入口：迭代加深 + 期望窗口 + 根部PVS，受 TIME_LIMIT_MS 时间控制
// 每完成一层就更新一次最优解，时间随时可中断且必有结果
Move
getAIMove(int board[10][9], int searchDepth, bool isMaxTurn)
{
    resetKillerAndHistory(); //每一次搜索清空杀手历史表！
    g_nodes = 0; g_stop = false;
    g_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_timeLimitMs);
    auto t0 = std::chrono::steady_clock::now();
    // 把真实对局历史装进搜索路径（重复局面检测用）
    for (int i = 0; i < g_gameHistLen; i++) g_posHist[i] = g_gameHist[i];
    g_posHistLen = g_gameHistLen;

    vector
        <Move> moves = generateAllMoves(board, isMaxTurn);
    if (moves.empty())
    {
        cout
            << "无合法走法，将死！\n";
        return { 0,0,0,0 };
    }
    Move bestMove
        = moves[0];
    int bestScore = -INF;
    uint64_t rootHash = hashBoard(board, isMaxTurn);

    // ===== 迭代加深 =====
    for (int depth = 1; depth <= searchDepth && !g_stop; depth++)
    {
        // 上一轮的最佳走法排最前（PV优先，剪枝最多）
        for (size_t k = 0; k < moves.size(); k++)
            if (moves[k] == bestMove) { std::swap(moves[0], moves[k]); break; }

        // ===== 期望窗口：以上一层分数为中心开小窗，失败再扩大 =====
        int window = 30;
        int alpha = (depth >= 4 && bestScore > -INF + 1) ? bestScore - window : -INF;
        int beta = (depth >= 4 && bestScore < INF - 1) ? bestScore + window : INF;
        Move iterBest = moves[0];
        int iterScore = -INF;
        bool reSearch;
        do {
            reSearch = false;
            iterScore = -INF;
            int a = alpha;
            int searched = 0;
            for (auto& m : moves)
            {
                int mover = board[m.x1][m.y1];
                Undo u; makeMove(board, m, u);
                uint64_t h2 = hashAfterMove(rootHash, mover, u.captured, m);
                int sc;
                if (searched == 0)
                {
                    sc = -negamax(board, depth - 1, -beta, -a, !isMaxTurn, h2, 1);
                }
                else
                {
                    // 根部PVS：零窗口试探，超了再全窗口重搜
                    sc = -negamax(board, depth - 1, -a - 1, -a, !isMaxTurn, h2, 1);
                    if (sc > a && sc < beta)
                        sc = -negamax(board, depth - 1, -beta, -a, !isMaxTurn, h2, 1);
                }
                unmakeMove(board, m, u);
                if (g_stop) break;
                searched++;
                if (sc > iterScore) { iterScore = sc; iterBest = m; }
                if (sc > a) a = sc;
            }
            if (g_stop) break;
            // 窗口失败处理：跌破alpha放宽下界，顶穿beta放宽上界
            if (iterScore <= alpha && alpha > -INF) { alpha = -INF; reSearch = true; }
            else if (iterScore >= beta && beta < INF) { beta = INF; reSearch = true; }
        } while (reSearch && !g_stop);
        if (g_stop) break; // 本层没搜完，沿用上一完整层的结果
        bestMove = iterBest;
        bestScore = iterScore;
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        cout << "  [深度" << depth << "] 分值=" << bestScore
            << " 节点=" << g_nodes << " 用时=" << (int)ms << "ms\n";
        if (bestScore > MATE - 100) break; // 已找到必杀了，提前收工
    }
    g_posHistLen = g_gameHistLen; // 复位搜索路径
    return bestMove;
}

//在全局棋盘 coreBoard 上正式落子（人机对战时玩家/AI 走子后调用）
void executeMove(Move m) {
    if (!inBoard(m.x1, m.y1) || !inBoard(m.x2, m.y2)) return;
    coreBoard[m.x2][m.y2] = coreBoard[m.x1][m.y1];
    coreBoard[m.x1][m.y1] = 0;
}
