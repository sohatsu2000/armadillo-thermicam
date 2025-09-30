// 中部横断道54.605KP
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PRINT 1 // 1:メッセージ表示

#define CRCPOLY2 0x8408U /* 左右逆転 */
#define BUFMAX 4096
#define PACKET_NO 13 // データパケット番号：13バイト目
#define LANENUM 2	 // 車線数
#define TIMERNUM 2	 // 使用タイマー数
#define PORT 355	 // ポート番号(自分が開放する受信ポート)
// static	char srcDomain[]	= "datex.tc.10.122.49.234";	//送信元ドメイン
// static	char dstDomain[]	= "datex.tc.10.122.49.234";	//宛先ドメイン
// static	char userName[]		= "10.122.49.234";			//ユーザ名
static char userName[] = "10.155.173.53"; // ユーザ名
static char pass[] = "torakan";			  // パスワード

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

LANEDATA lane[LANENUM]; // 車線毎の交通量データ
DT resDate;				// 応答日時
DT startDate;			// 収集開始日時
DT endDate;				// 収集終了日時

// テストパターン
LANEDATA lanePattern[7] = {
	{0, 0, 0, 0, 0, 0},
	{255, 255, 255, 255, 255, 3},
	{256, 256, 256, 256, 256, 3},
	{65535, 65535, 65535, 9999, 999, 5},
	{65536, 65536, 65536, 9999, 999, 5},
	{999999, 999999, 999999, 9999, 999, 99},
	{30, 20, 10, 812, 123, 0}};
DT startPattern[7] = {
	{0, 1, 1, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{9999, 12, 31, 23, 59, 59},
	{2012, 2, 9, 12, 34, 0}};
DT endPattern[7] = {
	{0, 1, 1, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{2012, 2, 8, 0, 0, 0},
	{9999, 12, 31, 23, 59, 59},
	{2012, 2, 9, 12, 34, 56}};

int sock;
struct sockaddr_in senderinfo;
socklen_t addrlen;
char recvBuf[BUFMAX];
char sendBuf[BUFMAX];
char trafficBuf[256];
int timerCount[TIMERNUM] = {-1};

int rp = 0;
int pduTop = 0;							   // PDU先頭位置
unsigned long recvPacketNo = 0;			   // 受信データパケット番号
unsigned long sendPacketNo = 0;			   // 送信データパケット番号
unsigned long subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号
unsigned char dataStatus;				   // 応答結果
int datexDataPoint;						   // datex-Dataが先頭から何バイト目か
int PFlag = 0;

void saveMessage(char *fname, char *dat, int len)
{
	FILE *fp;
	int i;

	fp = fopen(fname, "w");
	for (i = 0; i < len; i++) {
		fprintf(fp, "%02x\n", *(dat + i));
	}
	fclose(fp);
}

/*-----------------------------------------------------------
 時刻設定
-----------------------------------------------------------*/
void setTime(void)
{
	time_t now, ret;
	struct tm recv;
	int year, mon, day;
	int hour, min, sec;
	char dt[32];
	double diff;

	// 現在日付取得
	now = time(NULL);

	recv.tm_year = resDate.year - 1900;
	recv.tm_mon = resDate.month - 1;
	recv.tm_mday = resDate.day;
	recv.tm_hour = resDate.hour;
	recv.tm_min = resDate.min;
	recv.tm_sec = resDate.sec;

	ret = mktime(&recv); // 受信時刻をtime_t型にする
	if (ret != -1) {
		diff = difftime(now, ret); // 内部時計との差分を求め得る
		// printf("Diff %lf\n", diff);
		if ((diff <= -2) || (diff >= 2)) // 2秒以上差があれば時刻合わせ
		{
#if PRINT
			printf("TimeSet!\n");
#endif
			year = resDate.year;
			mon = resDate.month;
			day = resDate.day;
			hour = resDate.hour;
			min = resDate.min;
			sec = resDate.sec;
			// date 月日時分年
			sprintf(dt, "date -s %02d%02d%02d%02d%04d.%02d", mon, day, hour, min, year, sec);
			system(dt);								 // コマンド実行
			system("hwclock --systohc --localtime"); // ハードウエアクロック設定(RTC)
		}
	}
}
// 全コマンド共通
/***********************************************************
crc2 -- 16bitCRC（反転）
***********************************************************/
unsigned int crc2(int n, unsigned char c[])
{
	unsigned int i, j, r;

	r = 0xFFFFU;
	for (i = 0; i < n; i++) {
		// printf("buf[%d]: %x\n",i, c[i]);
		r ^= c[i];
		for (j = 0; j < CHAR_BIT; j++)
			if (r & 1)
				r = (r >> 1) ^ CRCPOLY2;
			else
				r >>= 1;
	}
	return r ^ 0xFFFFU;
}
//=============タイマースタート=============
void timerStart(int no, int time)
{
	timerCount[no] = time;
}
//=============タイマーストップ=============
void timerStop(int no)
{
	timerCount[no] = -1;
}
//=============タイマー割り込み=============
void timerInt(int signum)
{
	int i;
	for (i = 0; i < TIMERNUM; i++) {
		if (timerCount[i]) {
			timerCount[i]--;
		}
		if (timerCount[i] == 0) {
			// タイムアウト
			switch (i) {
			case 0:		// 1電文受信タイムアウト
				rp = 0; // 受信バッファリードポイント初期化
#if PRINT
				printf("タイムアウト\n");
#endif
				break;
			}
			timerStop(i); // タイマーストップ
		}
	}
}

//=============タイマー初期化=============
void timerInit(void)
{
	struct sigaction act, oldact;
	struct itimerval value, ovalue;
	// sigset_t *sigset = NULL;

	memset(&act, 0, sizeof(act));

	/* 割り込み関数の設定 */
	act.sa_handler = timerInt;
	// act.sa_mask = sigset;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGALRM, &act, &oldact);
	/* 割り込みの設定 */
	/*   最初の割り込みは it_value で指定した値 */
	/*   2 度目以降は it_interval で指定した値 */
	// 1秒タイマー
	value.it_value.tv_usec = 0;
	value.it_value.tv_sec = 1;
	value.it_interval.tv_usec = 0;
	value.it_interval.tv_sec = 1;
	setitimer(ITIMER_REAL, &value, &ovalue);
}
void timerReset(void)
{
	struct itimerval value, ovalue;

	value.it_value.tv_usec = 0;
	value.it_value.tv_sec = 0;
	value.it_interval.tv_usec = 0;
	value.it_interval.tv_sec = 0;
	setitimer(ITIMER_REAL, &value, &ovalue);
}
//=============交通量データファイル読み込み=============
void readFile(void)
{
	FILE *fp;

	// tdata.txtをオープンint setTime(char *p)
	fp = fopen("/home/spinach/tdata.txt", "r");
	if (fp == NULL) {
		perror("TrafficData file open Error");
#if PRINT
		printf("%d\n", errno);
#endif
		return;
	}

	fscanf(fp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
		   &startDate.year, &startDate.month, &startDate.day, &startDate.hour, &startDate.min, &startDate.sec,
		   &endDate.year, &endDate.month, &endDate.day, &endDate.hour, &endDate.min, &endDate.sec,
		   &lane[0].lTraffic, &lane[0].sTraffic, &lane[0].nTraffic, &lane[0].aveSpeed, &lane[0].senyu, &lane[0].state,
		   &lane[1].lTraffic, &lane[1].sTraffic, &lane[1].nTraffic, &lane[1].aveSpeed, &lane[1].senyu, &lane[1].state);
	fclose(fp);

	/*
	printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				startDate.year,startDate.month,startDate.day,startDate.hour,startDate.min,startDate.sec,
				endDate.year,endDate.month,endDate.day,endDate.hour,endDate.min,endDate.sec,
				lane[0].lTraffic,lane[0].sTraffic,lane[0].nTraffic,lane[0].aveSpeed,lane[0].senyu,lane[0].state,
				lane[1].lTraffic,lane[1].sTraffic,lane[1].nTraffic,lane[1].aveSpeed,lane[1].senyu,lane[1].state,
				lane[2].lTraffic,lane[2].sTraffic,lane[2].nTraffic,lane[2].aveSpeed,lane[2].senyu,lane[2].state);
	*/
}

//========================================================
//					送信データ生成
//========================================================

// 可変長数値を与えて必要バイト数を得る(4バイト以下)
int getIntLength(long dat)
{
	int len = 0;

	if (dat <= 0xFF) {
		len = 1;
	}
	else if (dat <= 0xFFFF) {
		len = 2;
	}
	else if (dat <= 0xFFFFFF) {
		len = 3;
	}
	else {
		len = 4;
	}

	return len;
}

// オクテット数表現に必要なバイト数を得る
int getOctetLength(int dat)
{
	int len = 0;

	if (dat <= 127) {
		len = 2; // Type(30)+Length(バイト数)
	}
	else if (dat <= 0xFF) // 1byte
	{
		len = 3; // Type+Length(81+バイト数)
	}
	else // 2byte
	{
		len = 4; // Type(30) + Length(82+バイト数上位+バイト数下位)
	}

	return len;
}

// オクテット数表現に必要なパケット生成
int makeOctetPacket(char buf[], int dat)
{
	int i = 0;

	if (dat <= 127) {
		buf[i++] = dat;
	}
	else if (dat <= 0xFF) {
		buf[i++] = 0x81; // 1byte
		buf[i++] = dat;
	}
	else {
		// dat = htons(dat);
		buf[i++] = 0x82; // 2byte
		buf[i++] = (dat & 0xFF00) >> 8;
		buf[i++] = dat & 0x00FF;
	}
	return i;
}
// 可変長数値表現に必要なパケット生成(LengthとVlaueの部分)
int makeIntPacket(char buf[], long dat)
{
	long dat2;
	int i = 0;

	if (dat <= 0xFF) {
		buf[i++] = 0x01; // 1byte
		buf[i++] = dat;
	}
	else if (dat <= 0xFFFF) {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = 0x02; // 2byte
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = dat2 & 0x00FF;
	}
	else if (dat <= 0xFFFFFF) {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = 0x03; // 3byte
		buf[i++] = (dat2 & 0xFF0000) >> 16;
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = dat2 & 0x00FF;
	}
	else {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = 0x04; // 4byte
		buf[i++] = (dat2 & 0xFF000000) >> 24;
		buf[i++] = (dat2 & 0xFF0000) >> 16;
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = dat2 & 0x00FF;
	}
	return i;
}
//========================================================
//						送信処理
//========================================================
int SendUdp(char *buff, int bufflen)
{
	int state;
	/*	int i;

		if(PFlag)
		{
			for(i=0;i<bufflen;i++)
			{
				printf("buff[%d]:%x\n", i, buff[i]);
			}
		}
	*/

	/*データの送信*/
	state = sendto(sock, buff, bufflen, 0, (struct sockaddr *)&senderinfo, addrlen);

	if (state == -1) /*送信失敗*/
	{
		perror("send");
		// printf("%d\n", errno);
		return (-1);
	}

	// セッション接続済みならデータパケット番号加算
	/*if(sendPacketNo)
	{
		sendPacketNo++;
	}*/
	return 0;
}
//========================================================
//						ASPH生成
//========================================================
int makeASPH(int size) // size:データ数（CRC除く）
{
	int i = 0;
	int asphLen = 0; // アプリケーションサービスヘッダ以後のバイト数
	int dpnLen = 0;	 // データパケット番号のLength

	sendPacketNo++; // 送信のたびに+1(Logout受信後のFrED送信で0に)

	for (i = 0; i < BUFMAX; i++) {
		sendBuf[i] = 0;
	}
	//=============== 各レングス計算 ===============
	// データパケット番号のLength判定
	dpnLen = getIntLength(sendPacketNo);

	// アプリケーションサービスヘッダ以後のバイト数
	asphLen = 3 + 2 + dpnLen; // バージョン番号:3 + データパケット番号（2+dpnLen
	asphLen = asphLen + size; // PDU先頭 + データ部:size

	//=============== パケット生成 ===============
	i = 0;
	// アプリケーションサービスヘッダ
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], asphLen);

	// バージョン番号
	sendBuf[i++] = 0x0A;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = 0x01;

	// データパケット番号
	sendBuf[i++] = 0x02;
	i += makeIntPacket(&sendBuf[i], sendPacketNo);

	return i;
}
//========================================================
//						FrED送信
//========================================================
int sendFrED(int flg)
{
	int i;
	int ret;

	i = makeASPH(3);

	sendBuf[i++] = 0x82;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = flg; // 確認パケット番号

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：FrED\n");
#endif
	return ret;
}
//========================================================
//						Accept送信
//========================================================
int sendAccept()
{
	int i;
	int ret;
	int dpnLen;

	// データパケット番号のLength判定
	dpnLen = getIntLength(recvPacketNo);

	i = makeASPH(4 + dpnLen);

	sendBuf[i++] = 0xA7;
	sendBuf[i++] = 2 + dpnLen;
	// 応答データパケット番号
	sendBuf[i++] = 0x02;
	i += makeIntPacket(&sendBuf[i], recvPacketNo);

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：Accept\n");
#endif
	return ret;
}
//========================================================
//						Reject送信
//========================================================
int sendReject()
{
	int i;
	int ret;
	int dpnLen;

	// データパケット番号のLength判定
	dpnLen = getIntLength(recvPacketNo);

	i = makeASPH(4 + dpnLen);

	sendBuf[i++] = 0xA8;
	sendBuf[i++] = 2 + dpnLen;
	// 応答データパケット番号
	sendBuf[i++] = 0x02;
	i += makeIntPacket(&sendBuf[i], recvPacketNo);

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：Reject\n");
#endif

	// sendPacketNo = 0;//リセット

	return ret;
}
//========================================================
//						Publication処理
//========================================================
// 断面交通量以降のパケット作成
int makeTrafficData()
{
	int i = 0;
	int j = 0;
	int syasen[LANENUM]; // 車線毎の交通量
	int total[LANENUM];	 // 交通量（総台数）
	int len1[LANENUM];	 // 車種別交通量
	int len2[LANENUM];	 // 交通量詳細
	int dataLen;		 // 断面交通量

	// DEBUG
	/*	lane[0].lTraffic = 3;
		lane[0].sTraffic = 1;
		lane[0].nTraffic = 0;
		lane[0].aveSpeed = 0x294;
		lane[0].senyu = 0x1E;
		lane[0].state = 0;

		lane[1].lTraffic = 0;
		lane[1].sTraffic = 1;
		lane[1].nTraffic = 0;
		lane[1].aveSpeed = 0x334;
		lane[1].senyu = 0;
		lane[1].state = 0;
	*/
	//=============== 各レングス計算 ===============

	for (j = 0; j < LANENUM; j++) // 2車線分
	{
		// 交通量詳細
		len1[j] = getIntLength(lane[j].lTraffic) + 2;
		len1[j] += getIntLength(lane[j].sTraffic) + 2;
		len1[j] += getIntLength(lane[j].nTraffic) + 2;

		len2[j] = len1[j]; // 車種別交通量で使用

		len1[j] += getIntLength(lane[j].aveSpeed) + 2;
		len1[j] += getIntLength(lane[j].senyu) + 2;
		len1[j] += 3; // 機器状態

		// 交通量（総台数）
		total[j] = lane[j].lTraffic + lane[j].sTraffic + lane[j].nTraffic;

		// 車線別交通量
		syasen[j] = (3 + (2 + getIntLength(total[j])) + 2 + len1[j]); // 車線:3 + 交通量:(2+getIntLength(total[j]))
		// printf("syasen:%d\n",syasen);
	}

	dataLen = 0;
	for (j = 0; j < LANENUM; j++) // 2車線分
	{
		dataLen += syasen[j] + 2; // 断面交通量
	}

	//=============== パケット生成 ===============
	i = 0;
	// 断面交通量
	trafficBuf[i++] = 0x30;
	i += makeOctetPacket(&trafficBuf[i], dataLen);

	for (j = 0; j < LANENUM; j++) // 3車線分
	{
		// 車線毎の交通量
		trafficBuf[i++] = 0x30;
		i += makeOctetPacket(&trafficBuf[i], syasen[j]);

		// 車線
		trafficBuf[i++] = 0x0A;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = j + 1;
		// trafficBuf[i++] = 0x01;
		// 交通量
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], total[j]);
		// 車種別交通量
		trafficBuf[i++] = 0x30;
		i += makeOctetPacket(&trafficBuf[i], len2[j]);
		// 車種別交通量(大型)
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], lane[j].lTraffic);
		// 車種別交通量(小型)
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], lane[j].sTraffic);
		// 車種別交通量(判定不能)
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], lane[j].nTraffic);
		// 平均速度
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], lane[j].aveSpeed);
		// 占有率
		trafficBuf[i++] = 0x02;
		i += makeIntPacket(&trafficBuf[i], lane[j].senyu);
		// 機器状態
		trafficBuf[i++] = 0x0A;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = lane[j].state;
	}
	return i;
}
// Publication送信
int sendPublication(void)
{
	int i;
	int ret;
	int trafficLen;
	int octet1;	 // パブリケーションデータパケット
	int octet4;	 // パブリケーションデータ
	int octet8;	 // 応答タイプ
	int octet10; // メッセージセット
	int octet11; // データ応答メッセージセット
	int octet40; // 地点数
	int octet41; // 計測地点
	int kpLen;

	char fname[32];

	//=============== 各レングス計算 ===============
	trafficLen = makeTrafficData(); // 断面交通量～機器状態
	// 路線コード～キロポスト
	kpLen = 13;
	// 計測地点以後のバイト数計算
	octet41 = trafficLen + kpLen + 2;
	// 地点数以後のバイト数計算
	octet40 = octet41 + getOctetLength(octet41);

	// データ応答メッセージセット以後のバイト数計算
	octet11 = octet40 + getOctetLength(octet40) + 30 + 11 + 42 + 2;
	// メッセージセット以後のバイト数計算
	octet10 = octet11 + getOctetLength(octet11);
	// 応答タイプ以後のバイト数計算
	octet8 = octet10 + getOctetLength(octet10) + 2;

	// パブリケーションデータ以後のバイト数計算
	octet4 = octet8 + getOctetLength(octet8) + 2 + getIntLength(subscriptionNo);

	// パブリケーションデータパケット以後のバイト数計算
	octet1 = octet4 + getOctetLength(octet4);

	/*
		printf("trafficLen:%d\n",trafficLen);
		printf("octet41:%d\n",octet41);
		printf("octet40:%d\n",octet40);
		printf("octet11:%d\n",octet11);
		printf("octet10:%d\n",octet10);
		printf("octet8:%d\n",octet8);
		printf("octet4:%d\n",octet4);
		printf("octet3:%d\n",octet3);
		printf("octet1:%d\n",octet1);
	*/
	//=============== パケット生成 ===============
	// ASPH
	i = makeASPH(octet1 + getOctetLength(octet1));

	// パブリケーションデータパケット
	sendBuf[i++] = 0xA6;
	i += makeOctetPacket(&sendBuf[i], octet1);
	// パブリケーションデータ
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], octet4);
	// サブスクリプション番号
	sendBuf[i++] = 0x02;
	i += makeIntPacket(&sendBuf[i], subscriptionNo);

	// 応答タイプ
	sendBuf[i++] = 0xA1;
	i += makeOctetPacket(&sendBuf[i], octet8);
	// メッセージ識別子
	sendBuf[i++] = 0x06;
	sendBuf[i++] = 0x00;

	// メッセージセット
	sendBuf[i++] = 0x04;
	i += makeOctetPacket(&sendBuf[i], octet10);
	// データ応答メッセージセット
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], octet11);
	// 共通ヘッダ
	sendBuf[i++] = 0x30;
	sendBuf[i++] = 0x19;
	// メッセージID
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x03;
	sendBuf[i++] = 0xF3;
	// タイムスタンプ
	sendBuf[i++] = 0x31;
	sendBuf[i++] = 0x13;

	// DEBUG
	/*	resDate.year = 2011;
		resDate.month = 7;
		resDate.day = 13;
		resDate.hour = 0;
		resDate.min = 1;
		resDate.sec = 50;
	*/
	// 年
	sendBuf[i++] = 0x80;
	sendBuf[i++] = 0x02;
	sendBuf[i++] = (resDate.year & 0xFF00) >> 8;
	sendBuf[i++] = resDate.year & 0x00FF;
	// 月startDate.year
	sendBuf[i++] = 0x81;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = resDate.month;
	// 日
	sendBuf[i++] = 0x82;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = resDate.day;
	// 時
	sendBuf[i++] = 0x83;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = resDate.hour;
	// 分
	sendBuf[i++] = 0x84;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = resDate.min;
	// 秒
	sendBuf[i++] = 0x85;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = resDate.sec;
	// 応答結果
	sendBuf[i++] = 0x80;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = dataStatus;
	// 装置管理者情報
	sendBuf[i++] = 0x30;
	sendBuf[i++] = 0x09;
	// 装置管理システムコード(2048)
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x04;
	sendBuf[i++] = 0x32;
	sendBuf[i++] = 0x30;
	sendBuf[i++] = 0x34;
	sendBuf[i++] = 0x38;
	// 装置管理番号
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = 0x01;

	// 収集期間
	sendBuf[i++] = 0x30;
	sendBuf[i++] = 0x2A;
	// 収集開始日時刻
	sendBuf[i++] = 0x31;
	sendBuf[i++] = 0x13;

	// DEBUG用
	/*	startDate.year = 2011;
		startDate.month = 7;
		startDate.day = 13;
		startDate.hour = 0;
		startDate.min = 0;
		startDate.sec = 0;

		endDate.year = 2011;
		endDate.month = 7;
		endDate.day = 13;
		endDate.hour = 0;
		endDate.min = 0;
		endDate.sec = 59;
	*/
	// 年
	sendBuf[i++] = 0x80;
	sendBuf[i++] = 0x02;
	sendBuf[i++] = (startDate.year & 0xFF00) >> 8;
	sendBuf[i++] = startDate.year & 0x00FF;
	// 月
	sendBuf[i++] = 0x81;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = startDate.month;
	// 日
	sendBuf[i++] = 0x82;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = startDate.day;
	// 時
	sendBuf[i++] = 0x83;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = startDate.hour;
	// 分
	sendBuf[i++] = 0x84;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = startDate.min;
	// 秒
	sendBuf[i++] = 0x85;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = startDate.sec;
	// 収集終了日時刻
	sendBuf[i++] = 0x31;
	sendBuf[i++] = 0x13;
	// 年
	sendBuf[i++] = 0x80;
	sendBuf[i++] = 0x02;
	sendBuf[i++] = (endDate.year & 0xFF00) >> 8;
	sendBuf[i++] = endDate.year & 0x00FF;
	// 月
	sendBuf[i++] = 0x81;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = endDate.month;
	// 日
	sendBuf[i++] = 0x82;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = endDate.day;
	// 時
	sendBuf[i++] = 0x83;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = endDate.hour;
	// 分
	sendBuf[i++] = 0x84;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = endDate.min;
	// 秒
	sendBuf[i++] = 0x85;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = endDate.sec;
	// 地点数
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], octet40);
	// 計測地点
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], octet41);
	// DsExpresswaySpotKp
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], kpLen);
	// DEBUG
	/*	//計測位置　路線コード
		sendBuf[i++] = 0x0C;
		sendBuf[i++] = 0x04;
		sendBuf[i++] = 0x31;
		sendBuf[i++] = 0x32;
		sendBuf[i++] = 0x30;
		sendBuf[i++] = 0x31;
		//計測位置　方向
		sendBuf[i++] = 0x0A;
		sendBuf[i++] = 0x01;
		sendBuf[i++] = 0x03;
		//計測位置　キロポスト
		sendBuf[i++] = 0x02;
		sendBuf[i++] = 0x03;
		sendBuf[i++] = 0x01;
		sendBuf[i++] = 0xCE;
		sendBuf[i++] = 0x6C;
	*/
	// 計測位置　路線コード（1104）
	sendBuf[i++] = 0x0C;
	sendBuf[i++] = 0x04;
	sendBuf[i++] = 0x31;
	sendBuf[i++] = 0x31;
	sendBuf[i++] = 0x34;
	sendBuf[i++] = 0x30;
	// 計測位置　方向(上下：8)
	sendBuf[i++] = 0x0A;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = 0x08;
	// 計測位置　キロポスト(54.605KP)
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0xD5;
	sendBuf[i++] = 0x4D;

	// 断面交通量以後
	memcpy(&sendBuf[i], &trafficBuf[0], trafficLen);
	i += trafficLen;

	// printf("I:%d\n", i);

	PFlag = 1;
	ret = SendUdp(sendBuf, i);
	PFlag = 0;
#if PRINT
	printf("送信：Publication\n");

	sprintf(fname, "/home/spinach/sendP.txt");
	saveMessage(fname, &sendBuf[0], 256);
#endif
	// タイマースタート？？？

	return ret;
}

//========================================================
//						受信処理
//========================================================

// ASPH解析
int checkASPH(char *recv, int len)
{
	int i;
	int j;

	// printf("len:%d\n",len);

	// バッファにコピー
	for (i = 0; i < len; i++) {
		recvBuf[rp++] = *(recv + i);
		if (rp == BUFMAX) {
			rp = 0;
		}
	}

	if (recvBuf[0] == 0x30) // アプリケーションサービスヘッダ?
	{

		// printf("size %d\n", recvBuf[1]);

		// 1電文受信タイムアウト用タイマースタート？	//タイムアウト処理でrp=0
		// timerStart(0, 3);

		// 全データサイズ分受信したか？
		if ((recvBuf[1] == 0) || (recvBuf[1] > rp)) // データ不足
		{
			return 0;
		}
		else // 全データサイズ分データ受信
		{
			// タイマーストップ
			timerStop(0);

			// ASPHサイズ、データパケット番号取得
			j = 1;
			// アプリケーションサービスヘッダ
			if (recvBuf[j] & 0x80) // 127バイト以上
			{
				j += 1 + (recvBuf[j + 1] & 0x0F);
			}
			else {
				j += 1;
			}
			j += 4;
			// データパケット番号
			switch (recvBuf[j]) {
			case 1:
				recvPacketNo = recvBuf[j + 1];
				j++;
				break;
			case 2:
				recvPacketNo = (recvBuf[j + 1] << 8) + recvBuf[j + 2];
				j += 2;
				break;
			case 3:
				recvPacketNo = (recvBuf[j + 1] << 16) + (recvBuf[j + 2] << 8) + recvBuf[j + 3];
				j += 3;
				break;
			case 4:
				recvPacketNo = (recvBuf[j + 1] << 24) + (recvBuf[j + 2] << 16) + (recvBuf[j + 3] << 8) + recvBuf[j + 4];
				j += 4;
				break;
			}
			// printf("データパケット番号：%ld\n",recvPacketNo);
			pduTop = j + 1; // PDU先頭位置
			rp = 0;
			return 1; // 受信OK
		}
	}

	return 0;
}
//========================================================
//						FrED受信
//========================================================
void recvFrED(void)
{

	// printf("%x\n",recvBuf[pduTop + 2]);//@@@

	sendFrED(recvPacketNo); // 受信したデータパケット番号を返す

#if 0
	//確認パケット番号チェック
	if(recvBuf[pduTop + 2] == 0)
	{
		//０ならFrED（生存監視用：パケット番号０）送信
		sendFrED(0);
	}
	else
	{
		//違えばFrED（パケット番号２）送信
		sendFrED(2);
	}
#endif
}
//========================================================
//						ログアウト受信
//========================================================
void recvLogOut(void)
{

	// printf("%x\n",recvBuf[PACKET_NO]);//@@@
	// ログアウト理由チェック？
	if (recvBuf[pduTop + 2] == 2) // クライアント側要求
	{
		sendFrED(recvPacketNo); // 受信したデータパケット番号を返す

		sendPacketNo = 0;			 // データパケット番号初期化
		subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号初期化
	}
}
//========================================================
//						ログイン受信
//========================================================
int checkLogin(void)
{
	int ret = 0;
	int p = 0;
	int len;

	// 送信元ドメイン名チェック
	p = pduTop + 3;
	len = recvBuf[p];

	// 宛先ドメイン名チェック
	p = p + len + 2;
	len = recvBuf[p];

	// ユーザ名チェック
	p = p + len + 2;
	len = recvBuf[p];
	ret = strncmp(userName, &recvBuf[p + 1], len);
	if (ret) {
#if PRINT
		printf("ユーザ名 NG\n");
#endif
		return 0x80;
	}

	// パスワードチェック
	p = p + len + 2;
	len = recvBuf[p];
	ret = strncmp(pass, &recvBuf[p + 1], len);
	if (ret) {
#if PRINT
		printf("パスワード NG\n");
#endif
		return 0x83;
	}

	return 0;
}

// ログイン受信
void recvLogin(void)
{
	int ret;

	// セッション存在済みチェック
	if (sendPacketNo) {
		sendReject(); // Reject送信
	}
	else {
		// ドメイン名、ユーザ名、パスワードチェック
		ret = checkLogin();

		// ret = 0;//@@@

		if (ret == 0) // チェックOK
		{
#if PRINT
			printf("Login OK\n");
#endif
			// Accept送信
			sendAccept();
		}
		else {
#if PRINT
			printf("Login NG\n");
#endif
			sendReject();	  // Reject送信
			sendPacketNo = 0; // セッション未接続
		}
	}
}

//========================================================
//					Subscription受信
//========================================================
void recvSubscription(void)
{
	int no = 0;
	int j;

	// セッション未接続時は無視？
	if (sendPacketNo == 0) {
		return;
	}

	// サブスクリプション番号チェック（前回と同じ番号なら破棄）
	j = pduTop + 3;
	// printf("recvBuf:%d %d\n", recvBuf[j], recvBuf[j+1]);
	switch (recvBuf[j]) {
	case 1:
		no = recvBuf[j + 1];
		j += 2;
		break;
	case 2:
		no = (recvBuf[j + 1] << 8) + recvBuf[j + 2];
		j += 3;
		break;
	case 3:
		no = (recvBuf[j + 1] << 16) + (recvBuf[j + 2] << 8) + recvBuf[j + 3];
		j += 4;
		break;
	case 4:
		no = (recvBuf[j + 1] << 24) + (recvBuf[j + 2] << 16) + (recvBuf[j + 3] << 8) + recvBuf[j + 4];
		j += 5;
		break;
	}

	subscriptionNo = no; // サブスクリプション番号取得

	// 年月日時分秒チェック
	j += 16;
	resDate.year = (recvBuf[j] << 8) + recvBuf[j + 1];
	// 月チェック
	j += 4;
	if ((recvBuf[j] < 1) || (recvBuf[j] > 12)) {
		sendReject(); // Reject送信
#if PRINT
		printf("NGYear\n");
#endif
		return;
	}
	else {
		resDate.month = recvBuf[j];
	}
	// 日チェック
	j += 3;
	if ((recvBuf[j] < 1) || (recvBuf[j] > 31)) {
		sendReject(0x81, 0x02); // Reject送信
#if PRINT
		printf("NGDay\n");
#endif
		return;
	}
	else {
		resDate.day = recvBuf[j];
	}
	// 時チェック
	j += 3;
	if ((recvBuf[j] < 0) || (recvBuf[j] > 24)) {
		sendReject(0x81, 0x02); // Reject送信
#if PRINT
		printf("NGHour\n");
#endif
		return;
	}
	else {
		resDate.hour = recvBuf[j];
	}
	// 分チェック
	j += 3;
	if ((recvBuf[j] < 0) || (recvBuf[j] > 59)) {
		sendReject(0x81, 0x02); // Reject送信
#if PRINT
		printf("NGMinute\n");
#endif
		return;
	}
	else {
		resDate.min = recvBuf[j];
	}
	// 秒チェック
	j += 3;
	if ((recvBuf[j] < 0) || (recvBuf[j] > 59)) {
		sendReject(0x81, 0x02); // Reject送信
#if PRINT
		printf("NGSec\n");
#endif
		return;
	}
	else {
		resDate.sec = recvBuf[j];
	}

	setTime(); // 時刻合わせ

	// Accept送信
	sendAccept();
	// トラフィックデータファイルリード
	readFile();
	// makeLaneData();//テスト用データ生成

	// Publication送信
	sendPublication();
}

//========================================================
//					コマンド分類
//========================================================
void checkCmd(void)
{
	char fname[64];

	// printf("コマンド：%x\n",recvBuf[pduTop]);
	switch (recvBuf[pduTop]) {
	case 0xA1: // Login
#if PRINT
		printf("受信：Loin\n");

		sprintf(fname, "/home/spinach/login.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif
		recvLogin();
		break;
	case 0x82: // FrED
#if PRINT
		printf("受信：FrED\n");

		sprintf(fname, "/home/spinach/fred.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif
		recvFrED();
		break;
	case 0x84: // Logout
#if PRINT
		printf("受信：Logout\n");

		sprintf(fname, "/home/spinach/logout.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif
		recvLogOut();

		break;
	case 0xA5: // Subscription
#if PRINT
		printf("受信：Subscription\n");

		sprintf(fname, "/home/spinach/subscription.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif
		recvSubscription();

		break;
	case 0xA7: // Accept
#if PRINT
		printf("受信：Accept\n");

		sprintf(fname, "/home/spinach/accept.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif

		// タイマーストップ
		break;
	case 0xA8: // Reject
#if PRINT
		printf("受信：Reject\n");

		sprintf(fname, "/home/spinach/reject.txt");
		saveMessage(fname, &recvBuf[pduTop], 128);
#endif
		// タイマーストップ
		break;
	}
}

//========================================================
//						メインループ
//========================================================
int main()
{
	struct sockaddr_in addr;
	int read_size;
	char buf[4096];
	int ret;

	timerInit(); // タイマー初期化

	sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP
	if (sock < 0) {
		perror("socket");
		return 1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT); // 自分が開放する受信ポート
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("bind");
		return 1;
	}

	while (1) {
		/* recvfrom()を利用してUDPソケットからデータを受信 */
		addrlen = sizeof(senderinfo);
		read_size = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&senderinfo, &addrlen);
		if (read_size > 0) {
			ret = checkASPH(buf, read_size); // ASPH解析

			// ret = 1;//@@@

			if (ret == 1) {
				checkCmd(); // コマンド判定とその後の処理
			}
		}
	}
	close(sock);
}
