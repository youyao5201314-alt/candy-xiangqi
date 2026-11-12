#pragma once
#include<iostream>
#include<cstring>
#include<cstdlib>
#include<cctype>
#include<vector>
#include<array>
#include<cstdint>
#include<climits>
#include <string>
#include<random>
using namespace std;
extern const int MAX_DEPTH;

//博弈树相关

struct Move {
    int x1, y1, x2, y2;
    Move() :x1(-1), y1(-1), x2(-1), y2(-1) {}
    Move(int a, int b, int c, int d) :x1(a), y1(b), x2(c), y2(d) {}
    bool operator==(const Move& other) const {
        return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
    }
};

vector<Move> generateAllMoves(int board[10][9], bool isMaxTurn);//生成全部合法走法（isMaxTurn=true=红方走，false=黑方走）

Move getAIMove(int board[10][9], int searchDepth, bool isMaxTurn);//电脑AI调用入口，返回最优一步Move
void setAiTimeLimitMs(int ms);//设置AI每步思考时间上限（毫秒），难度选项用

int evaluate(int board[10][9]);//局面估值（正值=红方优势）

bool simulateMove(int board[10][9], int sx, int sy, int tx, int ty);//在传入的棋盘副本上模拟走子








/*博弈树优化*/
int alphaBeta(int board[10][9], int depth, int alpha, int beta, bool isMaxTurn);//αβ剪枝递归（negamax+置换表+静止搜索）
// 重复局面检测：对局历史哈希维护
uint64_t getBoardHash(int board[10][9], bool redTurn);//当前局面Zobrist哈希
void pushGameHash(uint64_t h);//每走完一手记录局面哈希
void resetGameHash();//新对局清空历史
//杀手与历史
int scoreMove(int depth, const Move& m, int board[10][9], const Move& killer1, const Move& killer2);//给单个走法打分
int mvvLvaScore(const Move& m, int board[10][9]);//吃子优先级(权重)
void sortMoves(int depth, int board[10][9], vector<Move>& moves);//对move列表按分数降序排序
void resetKillerAndHistory();//新对局清空杀手表
int getPieceValue(int p);//权重参考
void executeMove(Move m);


extern int coreBoard[10][9];//棋盘
void initBoard();//初始化棋盘
bool inBoard(int x, int y);//判断坐标是否合法在棋盘内
bool movePiece(int sx, int sy, int tx, int ty);//移动棋子：(sx,sy)起点，(tx,ty)终 现阶段：不校验棋子行走规则！仅做移动框架，后面补规则
const char* getChess(int c);//棋子字符转中文
bool isRed(int c);//总所周知
bool isBlack(int c);//吃自己棋子是不对的
bool isInCheck(int board[10][9], bool isRedSide);// 判断该方是否正在被将军
// 以下规则函数都在传入的棋盘 board 上判定（AI搜索副本或全局coreBoard）
bool isValidHorse(int board[10][9], int sx, int sy, int tx, int ty);//马走路逻辑
bool isValidRook(int board[10][9], int sx, int sy, int tx, int ty);//车
bool isValidCannon(int board[10][9], int sx, int sy, int tx, int ty);//炮
bool isValidElephant(int board[10][9], int sx, int sy, int tx, int ty);//象/相
bool isValidGuard(int board[10][9], int sx, int sy, int tx, int ty);//士
bool isValidKing(int board[10][9], int sx, int sy, int tx, int ty);//将/帅
bool isValidPawn(int board[10][9], int sx, int sy, int tx, int ty);//卒/兵
bool isKingFaceToFace();//对面笑
int checkGameOver(int board[10][9], bool currentTurnIsRed);//判断对局是否结束，返回0未结束；1将死；2困毙
string getKillTypeName(int board[10][9], bool loserIsRed);//获取杀招名称
