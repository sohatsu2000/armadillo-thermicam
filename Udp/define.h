/*
 * define.h
 *
 *  Created on: 2025/01/06
 *      Author: Oshiro
 */

// 定義を集約するヘッダファイル

///////////////////////////////////////////////
// 共通
///////////////////////////////////////////////
typedef enum {
	KANSAI = 0, // 関西支社
	NAGOYA,		// 名古屋支社
	KANTO,		// 関東支社
	TOHOKU,		// 東北支社
	CHUBU,		// 中部横断道
} FORMAT_TYPE;

///////////////////////////////////////////////
// MainLoop.c 用
///////////////////////////////////////////////
#define PRINT 1			 // 1:メッセージ表示
#define CRCPOLY2 0x8408U // 左右逆転
#define BUFMAX 4096
#define PACKET_NO 13 // データパケット番号：13バイト目
#define LANEMAX 4	 // 最大車線数
#define TIMERNUM 2	 // 使用タイマー数

typedef struct
{
	int year;
	int month;
	int day;
	int hour;
	int min;
	int sec;
} DT;

typedef struct
{
	int lTraffic; // 大型車交通量
	int sTraffic; // 小型車交通量
	int nTraffic; // 判定不能交通量
	int aveSpeed; // 平均速度km/h
	int senyu;	  // 占有率
	int state;	  // 機器状態
} LANEDATA;


///////////////////////////////////////////////
// ReadConfig.c 用
///////////////////////////////////////////////
#define LINE_WORDS 64  // 1行の最大長
#define TITLE_WORDS 32 // 項目の最大長
#define PARAM_WORDS 32 // 値の最大長
#define LANEMAX 4	   // 最大車線数
