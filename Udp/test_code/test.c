#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "define.h"

extern int readConfig(void);

extern FORMAT_TYPE gFormatType; // フォーマット種別 0=関西支社, 1=関東支社, 2=東北支社, 3=名古屋支社
// extern char gVersion[PARAM_WORDS];
// extern char gIpAddr[32];			// ユーザー名（IPアドレス）
// extern int gPortNo;					// ポート番号
// extern int gSpotNum;				// 設置地点数
// extern int gLaneNum;				// 総車線数
// extern int gSpotLaneNum[LANEMAX];	// 設置地点ごとの車線数
// extern int gDirection[LANEMAX];		// 設置地点ごとの方向
// extern int gKp[LANEMAX];			// 設置地点ごとのキロポスト
// extern char gRouteCode[LANEMAX][8]; // 設置地点ごとの路線コード

int main(void)
{
	// カレントディレクトリを１つ上に移動
	chdir("..");
	// 設定ファイルの読み込み
	readConfig();

	switch (gFormatType)
	{
	case KANSAI:
		printf("関西支社\n");
		break;
	case NAGOYA:
		printf("名古屋支社\n");
		break;
	case KANTO:
		printf("関東支社\n");
		break;
	case TOHOKU:
		printf("東北支社\n");
		break;
	case CHUBU:
		printf("中部横断道\n");
		break;
	default:
		printf("不明\n");
		break;
	}


	return 0;
}
