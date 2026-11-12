#include "board.h"  
#include<iostream>
#include<cstring>
#include<cstdlib>
#include<Windows.h>
#include<cctype>
#include <string>
#include<random>
using namespace std;
void test() {
    initBoard();
    bool humanIsRed = true;   // 玩家的固定阵营
    cout << "请选择你的阵营：1=红方(先手，默认)  2=黑方(后手) 输入1或2：目前不建议选2，视觉错位";
    int sel;
    cin >> sel;
    if (sel == 2) humanIsRed = false;

    resetGameHash();                       // 新对局清空重复检测历史
    pushGameHash(getBoardHash(coreBoard, true)); // 记录初始局面
    bool redTurn = true;      // 当前轮到哪一方走（象棋红方先行）
    while (true)
    {
        system("cls");
        printBoard();

        //检查当前回合方是否已经结束
        int over = checkGameOver(coreBoard, redTurn);
        if (over != 0)
        {
            if (over == 1)
            {
                string killType = getKillTypeName(coreBoard, redTurn);
                cout << "====================\n";
                cout << "【游戏结束】被将死！\n";
                cout << "死因：" << killType << "\n";
            }
            else if (over == 2)
            {
                cout << "====================\n";
                cout << "【游戏结束】困毙，无子可走！\n";
            }
            cout << "对局结束，按回车退出\n";
            cin.get(); cin.get();
            break;
        }

        bool humanTurn = (redTurn == humanIsRed);
        if (humanTurn)
        {
            int sx, sy, tx, ty;
            cout << "【你的回合】输入起点x y 目标x y：";
            cin >> sx >> sy >> tx >> ty;
            bool ok = movePiece(sx, sy, tx, ty);
            if (!ok)
            {
                cout << "走法非法！按回车继续...";
                cin.get(); cin.get();
                continue;
            }
        }
        else
        {
            cout << "糖果思考中...\n";
            // 约定：getAIMove 第三参数 true=红方走，false=黑方走，与当前回合方一致
            // 深度20只是上限：内置迭代加深+4秒时间控制，到点自动截断返回当前最优
            Move aiMove = getAIMove(coreBoard, 20, redTurn);
            movePiece(aiMove.x1, aiMove.y1, aiMove.x2, aiMove.y2);
        }
        redTurn = !redTurn;
        pushGameHash(getBoardHash(coreBoard, redTurn)); // 记录走子后的局面（含换边）
    }
}
int main() {
    // 控制台输入输出按UTF-8解析，配合 /utf-8 编译选项，保证任何语言的Windows上中文都不乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    test();
	return 0;
}