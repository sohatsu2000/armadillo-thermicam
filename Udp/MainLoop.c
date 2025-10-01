// おそらく草津JCTは、関西支社と同じコードで、ソースにベタ書きの設定を設定ファイルに書けば動く？
// おそらく中国池田は、関西支社と同じコード？
// おそらく中部横断路は、東北支社と同じコードだが、装置管理番号等が異なる。またログアウトの条件が関西支社と同じ？ 多分東北Verで動く？？ 装置管理番号(1(0x01)) 装置管理システムコード(2048) 装置管理者情報(0x09,0x30) 路線コード（1104） 方向（上下：8） キロポスト(54.605KP) → 机上LTCServerでは動いた
// おそらく南阪奈道路は、関西支社と同じコードで、ソースにベタ書きの設定を設定ファイルに書けば動く？
// おそらく福島JCTは、関東支社と同じコードだが、レーンの並び順が異なるかもしれない 3412？ 4523？ (コードを見るとレーン番号が0から始まっていないのでオーバーフローしている？)。 sendPacketNo も加算タイミングが異なるが、実質同じか？

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "define.h"

static char pass[] = "torakan"; // パスワード

LANEDATA lane[LANEMAX]; // 車線毎の交通量データ
DT resDate;				// 応答日時
DT startDate;			// 収集開始日時
DT endDate;				// 収集終了日時

int sock;
struct sockaddr_in senderinfo;
socklen_t addrlen;
// 2025/01/07 Oshiroメモ
//   char が signed か unsigned かは処理系依存で、
//   現時点で、x86系の処理系では signed になるが、ARM系の処理系では unsigned になる。
//   厳密には、unsigned char と書くべきだが、char と書いても問題ない。
//   なお、文字列は char の配列であるため、単に unsigned char にすると strncmp() などから警告が出る。
char recvBuf[BUFMAX];
char sendBuf[BUFMAX];
char trafficBuf[BUFMAX];
int timerCount[TIMERNUM] = {-1};

int rp = 0;
int pduTop = 0;									// PDU先頭位置
unsigned long long recvPacketNo = 0;			// 受信データパケット番号
unsigned long long sendPacketNo = 0;			// 送信データパケット番号
unsigned long long subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号
unsigned char dataStatus;						// 応答結果 // 関東支社にはない
int datexDataPoint;								// datex-Dataが先頭から何バイト目か
int PFlag = 0;

extern int readConfig(void);

extern FORMAT_TYPE gFormatType; // フォーマット種別 0=関西支社, 1=関東支社, 2=東北支社, 3=名古屋支社
extern char gVersion[64];
extern char gIpAddr[32];			// ユーザー名（IPアドレス）
extern int gPortNo;					// ポート番号
extern int gLaneNum;				////総車線数
extern int gSpotNum;				// 設置地点数
extern int gSpotLaneNum[LANEMAX];	// 設置地点ごとの車線数
extern char gRouteCode[LANEMAX][8]; // 設置地点ごとの路線コード
extern int gDirection[LANEMAX];		// 設置地点ごとの方向
extern int gKp[LANEMAX];			// 設置地点ごとのキロポスト

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

// ========================================================
//                        時刻設定
// ========================================================
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
			year = resDate.year;
			mon = resDate.month;
			day = resDate.day;
			hour = resDate.hour;
			min = resDate.min;
			sec = resDate.sec;
#if PRINT
			printf("TimeSet!\n");
#endif
			// Armadillo 400シリーズ用
			//   'date -s 月日時分年'
			// sprintf(dt, "date -s %02d%02d%02d%02d%04d.%02d", mon, day, hour, min, year, sec);
			// Armadillo G3用
			//   'date -s "yyyy/mm/dd_hh:mm:ss"'
			// JEAN: use ISO format YYY-MM-DD hh:mm:ss
			sprintf(dt, "date -s \"%04d-%02d-%02d %02d:%02d:%02d\"", year, mon, day, hour, min, sec);

			// コマンド実行
			system(dt);
			// // ハードウエアクロック設定(RTC) 電池による時計バックアップを行う場合には必要かもしれない？
			// system("hwclock --systohc --localtime");
		}
	}
}

// 全コマンド共通
// ========================================================
//                crc2 -- 16bitCRC（反転）
// ========================================================
unsigned int crc2(int n, unsigned char c[])
{
	int i;
	unsigned int j, r;

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
// // =============タイマースタート=============
// void timerStart(int no, int time)
// {
// 	timerCount[no] = time;
// }
// // =============タイマーストップ=============
// void timerStop(int no)
// {
// 	timerCount[no] = -1;
// }
// // =============タイマー割り込み=============
// void timerInt(int signum)
// {
// 	int i;
// 	for (i = 0; i < TIMERNUM; i++) {
// 		if (timerCount[i]) {
// 			timerCount[i]--;
// 		}
// 		if (timerCount[i] == 0) {
// 			// タイムアウト
// 			switch (i) {
// 			case 0:		// 1電文受信タイムアウト
// 				rp = 0; // 受信バッファリードポイント初期化
// #if PRINT
// 				printf("タイムアウト\n");
// #endif
// 				break;
// 			}
// 			timerStop(i); // タイマーストップ
// 		}
// 	}
// }
// // =============タイマー初期化=============
// void timerInit(void)
// {
// 	struct sigaction act, oldact;
// 	struct itimerval value, ovalue;
// 	// sigset_t *sigset = NULL;

// 	memset(&act, 0, sizeof(act));

// 	// 割り込み関数の設定
// 	act.sa_handler = timerInt;
// 	// act.sa_mask = sigset;
// 	act.sa_flags = 0;
// 	sigemptyset(&act.sa_mask);
// 	sigaction(SIGALRM, &act, &oldact);
// 	// 割り込みの設定
// 	//   最初の割り込みは it_value で指定した値
// 	//   2 度目以降は it_interval で指定した値
// 	// 1秒タイマー
// 	value.it_value.tv_usec = 0;
// 	value.it_value.tv_sec = 1;
// 	value.it_interval.tv_usec = 0;
// 	value.it_interval.tv_sec = 1;
// 	setitimer(ITIMER_REAL, &value, &ovalue);
// }
// // =============タイマーリセット=============
// void timerReset(void)
// {
// 	struct itimerval value, ovalue;

// 	value.it_value.tv_usec = 0;
// 	value.it_value.tv_sec = 0;
// 	value.it_interval.tv_usec = 0;
// 	value.it_interval.tv_sec = 0;
// 	setitimer(ITIMER_REAL, &value, &ovalue);
// }
// =============交通量データファイル読み込み=============
void readFile(void)
{
	FILE *fp;

	// tdata.txtをオープンint setTime(char *p)
	fp = fopen("tdata.txt", "r");
	if (fp == NULL) {
		perror("TrafficData file open Error");
#if PRINT
		printf("%d\n", errno);
#endif
		return;
	}

	switch (gLaneNum) // 車線数
	{
	case 1:
		fscanf(fp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
			   &startDate.year, &startDate.month, &startDate.day, &startDate.hour, &startDate.min, &startDate.sec,
			   &endDate.year, &endDate.month, &endDate.day, &endDate.hour, &endDate.min, &endDate.sec,
			   &lane[0].lTraffic, &lane[0].sTraffic, &lane[0].nTraffic, &lane[0].aveSpeed, &lane[0].senyu, &lane[0].state);
		break;
	case 2:
		fscanf(fp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
			   &startDate.year, &startDate.month, &startDate.day, &startDate.hour, &startDate.min, &startDate.sec,
			   &endDate.year, &endDate.month, &endDate.day, &endDate.hour, &endDate.min, &endDate.sec,
			   &lane[0].lTraffic, &lane[0].sTraffic, &lane[0].nTraffic, &lane[0].aveSpeed, &lane[0].senyu, &lane[0].state,
			   &lane[1].lTraffic, &lane[1].sTraffic, &lane[1].nTraffic, &lane[1].aveSpeed, &lane[1].senyu, &lane[1].state);
		break;
	case 3:
		fscanf(fp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
			   &startDate.year, &startDate.month, &startDate.day, &startDate.hour, &startDate.min, &startDate.sec,
			   &endDate.year, &endDate.month, &endDate.day, &endDate.hour, &endDate.min, &endDate.sec,
			   &lane[0].lTraffic, &lane[0].sTraffic, &lane[0].nTraffic, &lane[0].aveSpeed, &lane[0].senyu, &lane[0].state,
			   &lane[1].lTraffic, &lane[1].sTraffic, &lane[1].nTraffic, &lane[1].aveSpeed, &lane[1].senyu, &lane[1].state,
			   &lane[2].lTraffic, &lane[2].sTraffic, &lane[2].nTraffic, &lane[2].aveSpeed, &lane[2].senyu, &lane[2].state);
		break;
	case 4:
		fscanf(fp, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
			   &startDate.year, &startDate.month, &startDate.day, &startDate.hour, &startDate.min, &startDate.sec,
			   &endDate.year, &endDate.month, &endDate.day, &endDate.hour, &endDate.min, &endDate.sec,
			   &lane[0].lTraffic, &lane[0].sTraffic, &lane[0].nTraffic, &lane[0].aveSpeed, &lane[0].senyu, &lane[0].state,
			   &lane[1].lTraffic, &lane[1].sTraffic, &lane[1].nTraffic, &lane[1].aveSpeed, &lane[1].senyu, &lane[1].state,
			   &lane[2].lTraffic, &lane[2].sTraffic, &lane[2].nTraffic, &lane[2].aveSpeed, &lane[2].senyu, &lane[2].state,
			   &lane[3].lTraffic, &lane[3].sTraffic, &lane[3].nTraffic, &lane[3].aveSpeed, &lane[3].senyu, &lane[3].state);
		break;
	}
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

// ========================================================
//                     送信データ生成
// ========================================================
// 可変長数値を与えて必要バイト数を得る(4バイト以下)
int getIntLength(unsigned long long dat)
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
// オクテット数表現に必要なパケット生成(最後から組み立てて送信時に逆に並べ替える)
int makeOctetPacketRvs(char buf[], int dat)
{
	int i = 0;

	if (dat <= 127) {
		buf[i++] = dat;
	}
	else if (dat <= 0xFF) {
		buf[i++] = dat;
		buf[i++] = 0x81; // 1byte
	}
	else {
		buf[i++] = dat & 0x00FF;
		buf[i++] = (dat & 0xFF00) >> 8;
		buf[i++] = 0x82; // 2byte
	}
	return i;
}
// 可変長数値表現に必要なパケット生成(LengthとValueの部分)最後から組み立てて送信時に逆に並べ替える
int makeIntPacketRvs(char buf[], unsigned long long dat)
{
	long dat2;
	int i = 0;

	if (dat <= 0xFF) {
		buf[i++] = dat;
		buf[i++] = 0x01; // 1byte
	}
	else if (dat <= 0xFFFF) {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = dat2 & 0x00FF;
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = 0x02; // 2byte
	}
	else if (dat <= 0xFFFFFF) {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = dat2 & 0x00FF;
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = (dat2 & 0xFF0000) >> 16;
		buf[i++] = 0x03; // 3byte
	}
	else {
		// dat2 = htons(dat);
		dat2 = dat;
		buf[i++] = dat2 & 0x00FF;
		buf[i++] = (dat2 & 0xFF00) >> 8;
		buf[i++] = (dat2 & 0xFF0000) >> 16;
		buf[i++] = (dat2 & 0xFF000000) >> 24;
		buf[i++] = 0x04; // 4byte
	}
	return i;
}
// オクテット数表現に必要なパケット生成 順方向
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
// 可変長数値表現に必要なパケット生成(LengthとVlaueの部分) 順方向
int makeIntPacket(char buf[], unsigned long long dat)
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

// ========================================================
//                        送信処理
// ========================================================
int SendUdp(char *buff, int bufflen)
{
	// データの送信
	ssize_t state; // 符号付きsize_t型
	state = sendto(sock, buff, (size_t)bufflen, 0, (struct sockaddr *)&senderinfo, addrlen);
	if (state == -1) // 送信失敗
	{
		perror("send");
		// printf("%d\n", errno);
		return (-1);
	}

	// セッション接続済みならデータパケット番号加算
	if (sendPacketNo) {
		sendPacketNo++;
	}
	return 0;
}

// ========================================================
//                       ASPH生成
// ========================================================
// 関西支社、名古屋支社
int makeASPHKansaiNagoya(int size) // size:データ数（CRC除く）
{
	int i = 0;
	int asphLen = 0; // アプリケーションサービスヘッダ以後のバイト数
	int dpnLen = 0;	 // データパケット番号のLength
	int c2cLen = 0;	 // C2CAuthenticated Message以後のバイト数
	int dtxLen = 0;	 // Datex-Data以後のバイト数
	for (i = 0; i < BUFMAX; i++) {
		sendBuf[i] = 0;
	}
	//=============== 各レングス計算 ===============
	// データパケット番号のLength判定
	dpnLen = getIntLength(sendPacketNo);

	// C2CAuthenticated Message以後のバイト数計算
	c2cLen = size + 9 + dpnLen;

	// Datex-Data以後のバイト数計算
	dtxLen = c2cLen + getOctetLength(c2cLen);

	// アプリケーションサービスヘッダ以後のバイト数
	asphLen = dtxLen + getOctetLength(dtxLen);
	asphLen += 3; // バージョン番号の加算
	asphLen += 4; // CRCの分加算

	//=============== パケット生成 ===============
	i = 0;
	// アプリケーションサービスヘッダ
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], asphLen);

	// バージョン番号
	sendBuf[i++] = 0x0A;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = 0x01;

	// datex-Data
	datexDataPoint = i;
	sendBuf[i++] = 0x04;
	i += makeOctetPacket(&sendBuf[i], dtxLen);

	// C2CAuthenticated Message
	sendBuf[i++] = 0x30;
	i += makeOctetPacket(&sendBuf[i], c2cLen);

	// バージョン番号
	sendBuf[i++] = 0x04;
	sendBuf[i++] = 0x00;

	// データパケット番号
	sendBuf[i++] = 0x02;
	i += makeIntPacket(&sendBuf[i], sendPacketNo);

	// プライオリティ番号
	sendBuf[i++] = 0x02;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = 0x05;

	// オプション
	sendBuf[i++] = 0x30;
	sendBuf[i++] = 0x00;

	return i;
}
// 関東支社、東北支社（、中部横断道）
int makeASPHKantoTohoku(int size) // size:データ数（CRC除く）
{
	int i = 0;
	int asphLen = 0; // アプリケーションサービスヘッダ以後のバイト数
	int dpnLen = 0;	 // データパケット番号のLength
	sendPacketNo++;	 // 送信のたびに+1(Logout受信後のFrED送信で0に)
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

// ========================================================
//                        FrED送信
// ========================================================
int sendFrED(int flg)
{
	int i;
	int ret;

	switch (gFormatType) {
	case KANSAI:
	case NAGOYA:
		i = makeASPHKansaiNagoya(3);
		break;
	case KANTO:
	case TOHOKU:
	case CHUBU:
	default:
		i = makeASPHKantoTohoku(3);
		break;
	}

	// 確認パケット番号
	sendBuf[i++] = 0x82;
	sendBuf[i++] = 0x01;
	sendBuf[i++] = flg;

	switch (gFormatType) {
	// 関西支社、名古屋支社
	case KANSAI:
	case NAGOYA:
		// CRC計算
		unsigned int crc;
		crc = crc2((i - datexDataPoint), (unsigned char *)&sendBuf[datexDataPoint]);
		crc = htons((u_short)crc);
		sendBuf[i++] = 0x04;
		sendBuf[i++] = 0x02;
		sendBuf[i++] = (crc & 0xFF00) >> 8;
		sendBuf[i++] = crc & 0x00FF;
		break;
	// 関東支社、東北支社
	case KANTO:
	case TOHOKU:
	case CHUBU:
	default:
		// 何もしない
		break;
	}

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：FrED\n");
#endif
	return ret;
}

// ========================================================
//                       Accept送信
// ========================================================
// 関西支社、名古屋支社は引数を使用。関東支社、東北支社は引数を無視する
int sendAccept(int flg)
{
	int i;
	int ret;
	unsigned int crc;
	int dpnLen;

	switch (gFormatType) {
	// 関西支社、名古屋支社
	case KANSAI:
	case NAGOYA:
		if (flg == 0) // Login応答
		{
			// データパケット番号のLength判定
			dpnLen = getIntLength(recvPacketNo);

			i = makeASPHKansaiNagoya(15 + dpnLen);
			sendBuf[i++] = 0x88;
			sendBuf[i++] = 0x0E;
			// 応答データパケット番号
			sendBuf[i++] = 0x02;
			i += makeIntPacket(&sendBuf[i], recvPacketNo);
			sendBuf[i++] = 0x80;
			sendBuf[i++] = 0x09;
			sendBuf[i++] = 0x2B;
			sendBuf[i++] = 0x06;
			sendBuf[i++] = 0x01;
			sendBuf[i++] = 0x04;
			sendBuf[i++] = 0x01;
			sendBuf[i++] = 0xD9;
			sendBuf[i++] = 0x5A;
			sendBuf[i++] = 0x02;
			sendBuf[i++] = 0x02;
		}
		else // Subscription応答
		{
			// 関西支社
			if (gFormatType == KANSAI) {
				// データパケット番号のLength判定
				dpnLen = getIntLength(subscriptionNo);

				i = makeASPHKansaiNagoya(6 + dpnLen);
				sendBuf[i++] = 0x88;
				sendBuf[i++] = 0x05;
				// 応答データパケット番号
				sendBuf[i++] = 0x02;
				i += makeIntPacket(&sendBuf[i], subscriptionNo);
				sendBuf[i++] = 0x81;
				sendBuf[i++] = 0x00;
			}
			// 名古屋支社
			else {
				i = makeASPHKansaiNagoya(7);
				sendBuf[i++] = 0x88;
				sendBuf[i++] = 0x05;
				// 応答データパケット番号
				sendBuf[i++] = 0x02;
				i += makeIntPacket(&sendBuf[i], recvPacketNo);
				sendBuf[i++] = 0x81;
				sendBuf[i++] = 0x00;
			}
		}
		// CRC計算
		crc = crc2((i - datexDataPoint), (unsigned char *)&sendBuf[datexDataPoint]);
		crc = htons((u_short)crc);
		sendBuf[i++] = 0x04;
		sendBuf[i++] = 0x02;
		sendBuf[i++] = (crc & 0xFF00) >> 8;
		sendBuf[i++] = crc & 0x00FF;
		break;
	// 関東支社、東北支社
	case KANTO:
	case TOHOKU:
	case CHUBU:
	default:
		// データパケット番号のLength判定
		dpnLen = getIntLength(recvPacketNo);

		i = makeASPHKantoTohoku(4 + dpnLen);
		sendBuf[i++] = 0xA7;
		sendBuf[i++] = 2 + dpnLen;
		// 応答データパケット番号
		sendBuf[i++] = 0x02;
		i += makeIntPacket(&sendBuf[i], recvPacketNo);
		break;
	}

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：Accept\n");
#endif
	return ret;
}

// ========================================================
//                       Reject送信
// ========================================================
// 関西支社、名古屋支社は引数を使用。関東支社、東北支社は引数を無視する
int sendReject(unsigned char type, char value)
{
	int i;
	int ret;
	unsigned int crc;
	int dpnLen;

	// 関西支社、名古屋支社
	switch (gFormatType) {
	case KANSAI:
	case NAGOYA:
		if (type == 0x80) // Login否定応答
		{
			// 拒否データパケット番号のLength判定
			dpnLen = getIntLength(recvPacketNo);

			i = makeASPHKansaiNagoya(8 + dpnLen);

			sendBuf[i++] = 0x89;
			sendBuf[i++] = 0x08;
			// 拒否データパケット番号
			sendBuf[i++] = 0x02;
			i += makeIntPacket(&sendBuf[i], recvPacketNo);
			sendBuf[i++] = 0x80;
			sendBuf[i++] = 0x01;
			sendBuf[i++] = value;
			sendBuf[i++] = 0x30;
			sendBuf[i++] = 0x00;
		}
		else // Subscription否定応答
		{
			// 拒否データパケット番号のLength判定
			dpnLen = getIntLength(subscriptionNo);

			i = makeASPHKansaiNagoya(8 + dpnLen);

			sendBuf[i++] = 0x89;
			sendBuf[i++] = 0x08;
			// 拒否データパケット番号
			sendBuf[i++] = 0x02;
			i += makeIntPacket(&sendBuf[i], subscriptionNo);
			sendBuf[i++] = 0x80;
			sendBuf[i++] = 0x01;
			sendBuf[i++] = value;
			sendBuf[i++] = 0x30;
			sendBuf[i++] = 0x00;
		}

		// CRC計算
		crc = crc2((i - datexDataPoint), (unsigned char *)&sendBuf[datexDataPoint]);
		crc = htons((u_short)crc);
		sendBuf[i++] = 0x04;
		sendBuf[i++] = 0x02;
		sendBuf[i++] = (crc & 0xFF00) >> 8;
		sendBuf[i++] = crc & 0x00FF;
		break;
	// 関東支社、東北支社
	case KANTO:
	case TOHOKU:
	case CHUBU:
	default:
		// データパケット番号のLength判定
		dpnLen = getIntLength(recvPacketNo);

		i = makeASPHKantoTohoku(4 + dpnLen);

		sendBuf[i++] = 0xA8;
		sendBuf[i++] = 2 + dpnLen;
		// 応答データパケット番号
		sendBuf[i++] = 0x02;
		i += makeIntPacket(&sendBuf[i], recvPacketNo);
		break;
	}

	ret = SendUdp(sendBuf, i);
#if PRINT
	printf("送信：Reject\n");
#endif
	return ret;
}

// ========================================================
//                      Publication処理
// ========================================================
// Publication送信
// 関西支社、名古屋支社
int sendPublicationKansaiNagoya(void)
{
	int i, j, k, m, n, p, no;
	int len;
	int countStart;
	int spotStart;
	int ret;
	char fname[32];
	unsigned int crc;

	//=============== パケット生成 ===============
	i = 0;
	no = gLaneNum - 1;
	// 地点数分ループ
	for (m = (gSpotNum - 1); m >= 0; --m) {
		spotStart = i;								 // オクテット数、数え始め
		for (n = (gSpotLaneNum[m] - 1); n >= 0; --n) // 車線数分ループ
		{
			countStart = i; // オクテット数、数え始め
			// 機器状態
			trafficBuf[i++] = lane[no].state;
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x0A;
			// 占有率
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].senyu);
			trafficBuf[i++] = 0x02;
			// 平均速度
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].aveSpeed);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(判定不能)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].nTraffic);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(小型)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].sTraffic);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(大型)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].lTraffic);
			trafficBuf[i++] = 0x02;
			// 交通量詳細
			i += makeOctetPacketRvs(&trafficBuf[i], i - countStart);
			trafficBuf[i++] = 0x30;
			// 車種別交通量
			i += makeOctetPacketRvs(&trafficBuf[i], i - countStart);
			trafficBuf[i++] = 0x81;
			// 交通量
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)(lane[no].lTraffic + lane[no].sTraffic + lane[no].nTraffic));
			trafficBuf[i++] = 0x02;
			// 車線
			trafficBuf[i++] = n + 1;
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x0A;
			no--;
		}
		// 車線毎の交通量
		i += makeOctetPacketRvs(&trafficBuf[i], i - spotStart);
		trafficBuf[i++] = 0x30;

		// 断面交通量
		i += makeOctetPacketRvs(&trafficBuf[i], i - spotStart);
		trafficBuf[i++] = 0x30;

		//---------- 計測位置 ----------
		// 計測位置　キロポスト
		if ((uint)gKp[m] <= 0xFF) {
			trafficBuf[i++] = gKp[m];
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = 0x02;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = (gKp[m] & 0xFF0000) >> 16;
			trafficBuf[i++] = 0x03;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFFFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = (gKp[m] & 0xFF0000) >> 16;
			trafficBuf[i++] = (gKp[m] & 0xFF000000) >> 24;
			trafficBuf[i++] = 0x04;
			trafficBuf[i++] = 0x02;
		}

		// 計測位置　方向
		trafficBuf[i++] = gDirection[m];
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0A;
		// 計測位置　路線コード
		len = (int)strlen(gRouteCode[m]);
		for (p = (len - 1); p >= 0; --p) {
			trafficBuf[i++] = gRouteCode[m][p];
		}
		trafficBuf[i++] = len;
		trafficBuf[i++] = 0x0C;
	}

	// 計測地点
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;
	// 地点数
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;

	//---------- 収集期間 ----------
	// 秒
	trafficBuf[i++] = endDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 分
	trafficBuf[i++] = endDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 時
	trafficBuf[i++] = endDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 日
	trafficBuf[i++] = endDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 月
	trafficBuf[i++] = endDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 年
	trafficBuf[i++] = endDate.year & 0x00FF;
	trafficBuf[i++] = (endDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x02;
	// 収集終了日時刻
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;

	// 秒
	trafficBuf[i++] = startDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 分
	trafficBuf[i++] = startDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 時
	trafficBuf[i++] = startDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 日
	trafficBuf[i++] = startDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 月
	trafficBuf[i++] = startDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 年
	trafficBuf[i++] = startDate.year & 0x00FF;
	trafficBuf[i++] = (startDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x02;
	// 収集開始日時刻
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;
	// 収集期間
	trafficBuf[i++] = 0x2A;
	trafficBuf[i++] = 0x30;

	// 関西支社
	if (gFormatType == KANSAI) {
		// 装置管理番号
		trafficBuf[i++] = 0x31;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0C;
		// 装置管理システムコード(101)
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
		trafficBuf[i++] = 0x02;
		// 装置管理者情報
		trafficBuf[i++] = 0x07; // 装置管理システムコードから装置管理番号までのバイト数
		trafficBuf[i++] = 0x30;
		// 応答結果
		trafficBuf[i++] = dataStatus;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0A;
	}
	// 名古屋支社
	else {
		// 装置管理番号
		trafficBuf[i++] = 0x31;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0C;
		// 装置管理システムコード
		trafficBuf[i++] = 0x65;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
		// 装置管理者情報
		trafficBuf[i++] = 0x06; // 装置管理システムコードから装置管理番号までのバイト数
		trafficBuf[i++] = 0x30;
		// 応答結果
		trafficBuf[i++] = 0x00;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0A;
	}

	//---------- 共通ヘッダ ----------
	// 秒
	trafficBuf[i++] = resDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 分
	trafficBuf[i++] = resDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 時
	trafficBuf[i++] = resDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 日
	trafficBuf[i++] = resDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 月
	trafficBuf[i++] = resDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x02;
	// 年
	trafficBuf[i++] = resDate.year & 0x00FF;
	trafficBuf[i++] = (resDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x02;
	// タイムスタンプ
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;
	// メッセージID
	trafficBuf[i++] = 0xF3;
	trafficBuf[i++] = 0x03;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x02;
	// 共通ヘッダ
	trafficBuf[i++] = 0x19;
	trafficBuf[i++] = 0x30;

	// データ応答メッセージセット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;
	// メッセージセット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x04;
	// メッセージ識別子
	trafficBuf[i++] = 0x00;
	trafficBuf[i++] = 0x06;
	// 応答タイプ
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x81;
	
	// 関西支社
	if (gFormatType == KANSAI) {
		// フラグ
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		// シリアルNo
		i += makeIntPacketRvs(&trafficBuf[i], subscriptionNo);
		trafficBuf[i++] = 0x02;
	}
	// 名古屋支社
	else {
		// フラグ
		trafficBuf[i++] = 0x00;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		// シリアルNo
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
	}

	// サブスクリプション番号
	i += makeIntPacketRvs(&trafficBuf[i], subscriptionNo);
	trafficBuf[i++] = 0x02;
	// パブリケーションデータ
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;

	// 応答データ
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x80;
	// ギャランティ
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x01;
	// パブリケーションデータパケット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x86;

	//=============== パケット生成 ===============
	// ASPH生成
	j = makeASPHKansaiNagoya(i);

	// 送信データ作成（逆順から戻す）
	for (k = (i - 1); k >= 0; k--) {
		sendBuf[j++] = trafficBuf[k];
	}

	// CRC計算
	crc = crc2((j - datexDataPoint), (unsigned char *)&sendBuf[datexDataPoint]);
	crc = htons((u_short)crc);
	sendBuf[j++] = 0x04;
	sendBuf[j++] = 0x02;
	sendBuf[j++] = (crc & 0xFF00) >> 8;
	sendBuf[j++] = crc & 0x00FF;

	PFlag = 1;
	ret = SendUdp(sendBuf, j);
	PFlag = 0;
#if PRINT
	char *dir = "udpLog";
	// カレントディレクトリにディレクトリを作成 (既に存在する場合は無視)
	mkdir(dir, 0777);
	printf("送信：Publication\n");
	sprintf(fname, "%s/sendP.txt", dir);
	// 関西支社
	if (gFormatType == KANSAI) {
		saveMessage(fname, &sendBuf[0], 256);
	}
	// 名古屋支社
	else {
		saveMessage(fname, &sendBuf[0], j);
	}
#endif

	return ret;
}
// 関東支社、東北支社（、中部横断道）
int sendPublicationKantoTohoku(void)
{
	int i, j, k, m, n, p, no;
	int len;
	int countStart;
	int spotStart;
	int ret;
	char fname[32];

	//=============== パケット生成 ===============
	i = 0;
	no = gLaneNum - 1;
	// 地点数分ループ
	for (m = (gSpotNum - 1); m >= 0; --m) {
		spotStart = i;								 // オクテット数、数え始め
		for (n = (gSpotLaneNum[m] - 1); n >= 0; --n) // 車線数分ループ
		{
			countStart = i; // オクテット数、数え始め
			// 機器状態
			trafficBuf[i++] = lane[no].state;
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x0A;
			// 占有率
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].senyu);
			trafficBuf[i++] = 0x02;
			// 平均速度
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].aveSpeed);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(判定不能)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].nTraffic);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(小型)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].sTraffic);
			trafficBuf[i++] = 0x02;
			// 車種別交通量(大型)
			i += makeIntPacketRvs(&trafficBuf[i], (unsigned long long)lane[no].lTraffic);
			trafficBuf[i++] = 0x02;
			// 車種別交通量
			i += makeOctetPacketRvs(&trafficBuf[i], i - countStart);
			trafficBuf[i++] = 0x30;
			// 交通量
			i += makeIntPacketRvs(&trafficBuf[i], lane[no].lTraffic + lane[no].sTraffic + lane[no].nTraffic);
			trafficBuf[i++] = 0x02;
			// 車線
			trafficBuf[i++] = n + 1;
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x0A;
			// 車線毎の交通量
			i += makeOctetPacketRvs(&trafficBuf[i], i - countStart);
			trafficBuf[i++] = 0x30;
			no--;
		}

		// 断面交通量
		i += makeOctetPacketRvs(&trafficBuf[i], i - spotStart);
		trafficBuf[i++] = 0x30;

		countStart = i;

		// 計測位置　キロポスト
		if ((uint)gKp[m] <= 0xFF) {
			trafficBuf[i++] = gKp[m];
			trafficBuf[i++] = 0x01;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = 0x02;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = (gKp[m] & 0xFF0000) >> 16;
			trafficBuf[i++] = 0x03;
			trafficBuf[i++] = 0x02;
		}
		else if ((uint)gKp[m] <= 0xFFFFFFFF) {
			trafficBuf[i++] = gKp[m] & 0XFF;
			trafficBuf[i++] = (gKp[m] & 0xFF00) >> 8;
			trafficBuf[i++] = (gKp[m] & 0xFF0000) >> 16;
			trafficBuf[i++] = (gKp[m] & 0xFF000000) >> 24;
			trafficBuf[i++] = 0x04;
			trafficBuf[i++] = 0x02;
		}

		// 計測位置　方向
		trafficBuf[i++] = gDirection[m];
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x0A;
		// 計測位置　路線コード
		len = (int)strlen(gRouteCode[m]);
		for (p = (len - 1); p >= 0; --p) {
			trafficBuf[i++] = gRouteCode[m][p];
		}
		trafficBuf[i++] = len;
		trafficBuf[i++] = 0x0C;
		// 計測位置
		i += makeOctetPacketRvs(&trafficBuf[i], i - countStart);
		trafficBuf[i++] = 0x30;
		// 計測地点
		i += makeOctetPacketRvs(&trafficBuf[i], i - spotStart);
		trafficBuf[i++] = 0x30;
	}
	// 地点数
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;

	//---------- 収集期間 ----------
	// 秒
	trafficBuf[i++] = endDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x85;
	// 分
	trafficBuf[i++] = endDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x84;
	// 時
	trafficBuf[i++] = endDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x83;
	// 日
	trafficBuf[i++] = endDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x82;
	// 月
	trafficBuf[i++] = endDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x81;
	// 年
	trafficBuf[i++] = endDate.year & 0x00FF;
	trafficBuf[i++] = (endDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x80;
	// 収集終了日時刻
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;

	// 秒
	trafficBuf[i++] = startDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x85;
	// 分
	trafficBuf[i++] = startDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x84;
	// 時
	trafficBuf[i++] = startDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x83;
	// 日
	trafficBuf[i++] = startDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x82;
	// 月
	trafficBuf[i++] = startDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x81;
	// 年
	trafficBuf[i++] = startDate.year & 0x00FF;
	trafficBuf[i++] = (startDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x80;
	// 収集開始日時刻
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;
	// 収集期間
	trafficBuf[i++] = 0x2A;
	trafficBuf[i++] = 0x30;

	// 中部横断道
	if (gFormatType == CHUBU) {
		// 装置管理番号(1)
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
		// 装置管理システムコード(2048)
		trafficBuf[i++] = 0x38;
		trafficBuf[i++] = 0x34;
		trafficBuf[i++] = 0x30;
		trafficBuf[i++] = 0x32;
		trafficBuf[i++] = 0x04;
		trafficBuf[i++] = 0x02;
		// 装置管理者情報
		trafficBuf[i++] = 0x09;
		trafficBuf[i++] = 0x30;
	}
	// 関東支社、東北支社
	else {
		// 装置管理番号
		trafficBuf[i++] = 0x31;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
		// 装置管理システムコード(101)
		trafficBuf[i++] = 0x65;
		trafficBuf[i++] = 0x01;
		trafficBuf[i++] = 0x02;
		// 装置管理者情報
		trafficBuf[i++] = 0x06;
		trafficBuf[i++] = 0x30;
	}
	// 応答結果
	trafficBuf[i++] = 0;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x80;

	//---------- 共通ヘッダ ----------
	// 秒
	trafficBuf[i++] = resDate.sec;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x85;
	// 分
	trafficBuf[i++] = resDate.min;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x84;
	// 時
	trafficBuf[i++] = resDate.hour;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x83;
	// 日
	trafficBuf[i++] = resDate.day;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x82;
	// 月
	trafficBuf[i++] = resDate.month;
	trafficBuf[i++] = 0x01;
	trafficBuf[i++] = 0x81;
	// 年
	trafficBuf[i++] = resDate.year & 0x00FF;
	trafficBuf[i++] = (resDate.year & 0xFF00) >> 8;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x80;
	// タイムスタンプ
	trafficBuf[i++] = 0x13;
	trafficBuf[i++] = 0x31;
	// メッセージID
	trafficBuf[i++] = 0xF3;
	trafficBuf[i++] = 0x03;
	trafficBuf[i++] = 0x02;
	trafficBuf[i++] = 0x02;
	// 共通ヘッダ
	trafficBuf[i++] = 0x19;
	trafficBuf[i++] = 0x30;

	// データ応答メッセージセット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;
	// メッセージセット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x04;
	// メッセージ識別子
	trafficBuf[i++] = 0x00;
	trafficBuf[i++] = 0x06;
	// 応答タイプ
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0xA1;
	// サブスクリプション番号
	i += makeIntPacketRvs(&trafficBuf[i], subscriptionNo);
	trafficBuf[i++] = 0x02;
	// パブリケーションデータ
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0x30;
	// パブリケーションデータパケット
	i += makeOctetPacketRvs(&trafficBuf[i], i);
	trafficBuf[i++] = 0xA6;

	//=============== パケット生成 ===============
	// ASPH生成
	j = makeASPHKantoTohoku(i);

	// 送信データ作成（逆順から戻す）
	for (k = (i - 1); k >= 0; k--) {
		sendBuf[j++] = trafficBuf[k];
	}
	PFlag = 1;
	ret = SendUdp(sendBuf, j);
	PFlag = 0;
#if PRINT
	char *dir = "udpLog";
	// カレントディレクトリにディレクトリを作成 (既に存在する場合は無視)
	mkdir(dir, 0777);
	printf("送信：Publication\n");
	sprintf(fname, "%s/sendP.txt", dir);
	saveMessage(fname, &sendBuf[0], j);
#endif

	return ret;
}

// ========================================================
//                        受信処理
// ========================================================
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
			// 2025/01/06 timerInit() と同様の理由でコメントアウト
			// // タイマーストップ
			// timerStop(0);

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
			switch (gFormatType) {
			// 関西支社、名古屋支社
			case KANSAI:
			case NAGOYA:
				// datex-Data
				if (recvBuf[j] & 0x80) // 127バイト以上
				{
					j += 1 + (recvBuf[j + 1] & 0x0F);
				}
				else {
					j += 1;
				}
				j++;
				// C2CAuthenticatedMessage
				if (recvBuf[j] & 0x80) // 127バイト以上
				{
					j += 1 + (recvBuf[j + 1] & 0x0F);
				}
				else {
					j += 1;
				}
				j += 3;
				break;
			// 関東支社、東北支社、中部横断道
			case KANTO:
			case TOHOKU:
			case CHUBU:
			default:
				// datex-Data, C2CAuthenticatedMessage の処理無し
				break;
			}

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

			switch (gFormatType) {
			// 関西支社、名古屋支社
			case KANSAI:
			case NAGOYA:
				j += 5;
				break;
			// 関東支社、東北支社、中部横断道
			case KANTO:
			case TOHOKU:
			case CHUBU:
			default:
				// 何もしない
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

// 名古屋支社、関東支社、東北支社用関数
// ========================================================
//                        FrED受信
// ========================================================
void recvFrED(void)
{
	// printf("%x\n",recvBuf[pduTop + 2]);//@@@

	// 名古屋支社、東北支社、中部横断道
	switch (gFormatType) {
	case NAGOYA:
	case TOHOKU:
	case CHUBU:
		sendFrED(recvPacketNo); // 受信したデータパケット番号を返す
		break;
	// 関東支社
	case KANTO:
	default:
		sendFrED(0); // 生存監視用に0を返す
		break;
	}
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

// ========================================================
//                      ログアウト受信
// ========================================================
// 関西支社
void recvLogOutKansai(void)
{
	// ログアウト理由チェック？
	if (recvBuf[pduTop + 2] == 2) {
		// データパケット番号チェック
		if (recvBuf[PACKET_NO] == 0) {
			// ０ならFrED（生存監視用：パケット番号０）送信
			sendFrED(0);
		}
		else {
			// 違えばFrED（パケット番号２）送信
			sendFrED(2);
		}
		sendPacketNo = 0;			 // データパケット番号初期化
		subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号初期化
	}
}
// 名古屋支社
void recvLogOutNagoya(void)
{
	// ログアウト理由チェック？
	if (recvBuf[pduTop + 2] == 1) // サーバ側要求
	{
		sendFrED(recvPacketNo); // 受信したデータパケット番号を返す
		sendPacketNo = 0;			 // データパケット番号初期化
		subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号初期化
	}
}
// 関東支社、東北支社（、中部横断道）
void recvLogOutKantoTohoku(void)
{
	sendFrED(recvPacketNo);		 // 受信したデータパケット番号を返す
	sendPacketNo = 0;			 // データパケット番号初期化
	subscriptionNo = 0xFFFFFFFF; // サブスクリプション番号初期化
}

// ========================================================
//                       ログイン受信
// ========================================================
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
	ret = strncmp(gIpAddr, &recvBuf[p + 1], (size_t)len);
	if (ret) {
#if PRINT
		printf("ユーザ名 NG\n");
#endif
		return 0x80;
	}

	// パスワードチェック
	p = p + len + 2;
	len = recvBuf[p];
	ret = strncmp(pass, &recvBuf[p + 1], (size_t)len);
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
		sendReject(0x80, 0x08); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
	}
	else {
		// ドメイン名、ユーザ名、パスワードチェック
		ret = checkLogin();
		if (ret == 0) // チェックOK
		{
#if PRINT
			printf("Login OK\n");
#endif
			// Accept送信
			switch (gFormatType) {
			// 関西支社、名古屋支社
			case KANSAI:
			case NAGOYA:
				sendAccept(0);
				sendPacketNo = 1;
				break;
			// 関東支社、東北支社、中部横断道
			case KANTO:
			case TOHOKU:
			case CHUBU:
			default:
				sendAccept(0); // 便宜上、引数 0 を渡すが無視される
				// sendPacketNo をリセットしない
				break;
			}
		}
		else {
#if PRINT
			printf("Login NG\n");
#endif
			sendReject(0x80, (ret & 0x0F)); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
		}
	}
}

// ========================================================
//                     Subscription受信
// ========================================================
void recvSubscription(void)
{
	unsigned long long no = 0;
	int j;

	// セッション未接続時は無視？
	if (sendPacketNo == 0) {
		return;
	}

	// サブスクリプション番号取得
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

	// printf("no:%d subscriptionNo:%ld\n", no, subscriptionNo);
	switch (gFormatType) {
	// 関西支社
	case KANSAI:
		// サブスクリプション番号チェック（前回と同じ番号なら破棄）
		if (no != subscriptionNo) // 前回と違うなら
		{
			subscriptionNo = no; // サブスクリプション番号取得
		}
		else {
			sendReject(0x81, 0x01); // Reject送信
#if PRINT
			printf("サブスクリプション番号 NG\n");
#endif
			return;
		}
		// サブスクリプションデータのタイプ0x80かチェックしない
		// パーシステント０かチェックしない
		j += 4;
		// ステータス０かチェックしない
		j += 3;
		// modeのタイプ0x80かチェックしない
		j++;
		// フォーマット３かチェックしない
		j += 4;
		// プライオリティ５かチェックしない
		j += 3;
		// ギャランティ１かチェックしない
		j += 3;
		// メッセージID1010(0x03F2)かチェックしない
		j += 11;
		// 年月日時分秒チェックへ
		j += 6;
		break;
	// 名古屋支社
	case NAGOYA:
		// サブスクリプション番号チェック（前回と同じ番号なら破棄）
		if (no != subscriptionNo) // 前回と違うなら
		{
			subscriptionNo = no; // サブスクリプション番号取得
		}
		else {
			sendReject(0x81, 0x01); // Reject送信
#if PRINT
			printf("サブスクリプション番号 NG\n");
#endif
			return;
		}
		// サブスクリプションデータのタイプ0x80かチェックしない
		// パーシステント０かチェックしない
		j += 4;
		// ステータス０かチェックしない
		j += 3;
		// modeのタイプ0x80かチェックしない
		j++;
		// フォーマット３かチェックしない
		j += 4;
		// プライオリティ５かチェックしない
		j += 3;
		// ギャランティ１かチェックしない
		j += 3;

		j += 4;
		j += recvBuf[j]; // endApplication-Mesage-idの分たす
		// メッセージID1010(0x03F2)かチェックしない
		j += 7;
		// 年月日時分秒チェックへ
		j += 6;
		break;
	// 関東支社
	case KANTO:
		subscriptionNo = no; // サブスクリプション番号取得
		// 年月日時分秒チェックへ
		j += 23;
		break;
	// 東北支社、中部横断道
	case TOHOKU:
	case CHUBU:
	default:
		subscriptionNo = no; // サブスクリプション番号取得
		// 年月日時分秒チェックへ
		j += 16; // 23->16に変更　endApplicationMessageIdが0固定なので
		break;
	}

	// 年月日時分秒チェック
	// 年はチェックしない
	resDate.year = (recvBuf[j] << 8) + recvBuf[j + 1];
	// 月チェック
	j += 4;
	if (((signed char)recvBuf[j] < 1) || ((signed char)recvBuf[j] > 12)) {
		sendReject(0x81, 0x02); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
#if PRINT
		printf("NG Month\n");
#endif
		return;
	}
	else {
		resDate.month = recvBuf[j];
	}
	// 日チェック
	j += 3;
	if (((signed char)recvBuf[j] < 1) || ((signed char)recvBuf[j] > 31)) {
		sendReject(0x81, 0x02); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
#if PRINT
		printf("NG Day\n");
#endif
		return;
	}
	else {
		resDate.day = recvBuf[j];
	}
	// 時チェック
	j += 3;
	if (((signed char)recvBuf[j] < 0) || ((signed char)recvBuf[j] > 24)) {
		sendReject(0x81, 0x02); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
#if PRINT
		printf("NG Hour\n");
#endif
		return;
	}
	else {
		resDate.hour = recvBuf[j];
	}
	// 分チェック
	j += 3;
	if (((signed char)recvBuf[j] < 0) || ((signed char)recvBuf[j] > 59)) {
		sendReject(0x81, 0x02); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
#if PRINT
		printf("NG Minute\n");
#endif
		return;
	}
	else {
		resDate.min = recvBuf[j];
	}
	// 秒チェック
	j += 3;
	if (((signed char)recvBuf[j] < 0) || ((signed char)recvBuf[j] > 59)) {
		sendReject(0x81, 0x02); // Reject送信 (便宜上、関東支社、東北支社も引数を渡すが無視される)
#if PRINT
		printf("NG Second\n");
#endif
		return;
	}
	else {
		resDate.sec = recvBuf[j];
	}

	setTime(); // 時刻合わせ

	// Accept送信
	sendAccept(1); // 便宜上、関東支社、東北支社も引数を渡すが、無視される

	// トラフィックデータファイルリード
	readFile();
	// makeLaneData();//テスト用データ生成

	// Publication送信
	switch (gFormatType) {
	// 関西支社、名古屋支社
	case KANSAI:
	case NAGOYA:
		sendPublicationKansaiNagoya();
		break;
	// 関東支社、東北支社、中部横断道
	case KANTO:
	case TOHOKU:
	case CHUBU:
	default:
		sendPublicationKantoTohoku();
		break;
	}
}

// ========================================================
//                      コマンド分類
// ========================================================
void checkCmd(void)
{
	// -------------------------------
	// 共通処理
	// -------------------------------
#if PRINT
	// ファイル出力先作成
	char fname[64];
	char *dir = "udpLog";
	// カレントディレクトリにディレクトリを作成 (既に存在する場合は無視)
	mkdir(dir, 0777);
#endif

	// -------------------------------
	// 関西支社、名古屋支社の場合
	// -------------------------------
	if (gFormatType == KANSAI || gFormatType == NAGOYA) {
		switch ((unsigned char)recvBuf[pduTop]) {
		case 0x81: // Login
#if PRINT
			printf("受信：Login\n");
			sprintf(fname, "%s/login.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvLogin();
			break;
		case 0x82: // FrED
#if PRINT
			printf("受信：FrED\n");
			sprintf(fname, "%s/fred.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 名古屋支社なら応答を返す (関西支社は応答を返さない)
			if (gFormatType == NAGOYA) {
				recvFrED();
			}
			break;
		case 0x84: // Logout
#if PRINT
			printf("受信：Logout\n");
			sprintf(fname, "%s/logout.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 関西支社
			if (gFormatType == KANSAI) {
				recvLogOutKansai();
			}
			// 名古屋支社
			else {
				recvLogOutNagoya();
			}
			break;
		case 0x85: // Subscription
#if PRINT
			printf("受信：Subscription\n");
			sprintf(fname, "%s/subscription.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvSubscription();
			break;
		case 0x88: // Accept
#if PRINT
			printf("受信：Accept\n");
			sprintf(fname, "%s/accept.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 応答は返さない
			break;
		case 0x89: // Reject
#if PRINT
			printf("受信：Reject\n");
			sprintf(fname, "%s/reject.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 応答は返さない
			break;
		}
	}
	// -------------------------------
	// 関東支社、東北支社、中部横断道の場合
	// -------------------------------
	else if (gFormatType == KANTO || gFormatType == TOHOKU || gFormatType == CHUBU) {
		switch ((unsigned char)recvBuf[pduTop]) {
		case 0xA1: // Login
#if PRINT
			printf("受信：Login\n");
			sprintf(fname, "%s/login.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvLogin();
			break;
		case 0x82: // FrED
#if PRINT
			printf("受信：FrED\n");
			sprintf(fname, "%s/fred.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvFrED();
			break;
		case 0x84: // Logout
#if PRINT
			printf("受信：Logout\n");
			sprintf(fname, "%s/logout.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvLogOutKantoTohoku();
			break;
		case 0xA5: // Subscription
#if PRINT
			printf("受信：Subscription\n");
			sprintf(fname, "%s/subscription.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			recvSubscription();
			break;
		case 0xA7: // Accept
#if PRINT
			printf("受信：Accept\n");
			sprintf(fname, "%s/accept.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 応答は返さない
			break;
		case 0xA8: // Reject
#if PRINT
			printf("受信：Reject\n");
			sprintf(fname, "%s/reject.txt", dir);
			saveMessage(fname, &recvBuf[pduTop], 128);
#endif
			// 応答は返さない
			break;
		}
	}
	// -------------------------------
	// それ以外の場合
	// -------------------------------
	else {
		printf("支社設定不明 unknown setting\n");
	}
}

// ========================================================
//                      メインループ
// ========================================================
int main()
{
	// 設定ファイル読み込み
	readConfig();

	// 支社設定異常なら終了
	switch (gFormatType) {
	case KANSAI:
	case NAGOYA:
	case KANTO:
	case TOHOKU:
	case CHUBU:
		break;
	default:
		printf("支社設定不明\n");
		return 1;
		break;
	}

	// バージョン情報保存
	FILE *fp;
	fp = fopen("UDPVersion.txt", "w");
	fprintf(fp, "%s", gVersion);
	fclose(fp);

	// 2025/01/06 memo
	//   関西支社、中国池田、中部横断道、南阪奈、草津JCTのみここでタイマー初期化を行うようになっていたが、
	//   全てのソースコードにおいて timerStart() の呼び出しが生きているコードがないため、
	//   ここで初期化を行うと、無用な割り込み処理を発生させてしまう。
	//   (
	//     このコードは、1秒割り込みタイマを初期化・開始を行い、
	//     1電文受信タイムアウト、受信バッファリードポイントを初期化するものだが、
	//     timerStart() が実行されない限りその条件が満たされることはない。
	//     (アンダーフロー時は条件が満たされることがありえるが、それは連続稼働68年後くらいなので無視してよい)
	//   )
	//   そのため、コメントアウトした。
	// timerInit(); // タイマー初期化

	struct sockaddr_in addr;
	char buf[BUFMAX];
	int ret;

	sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP
	if (sock < 0) {
		perror("socket");
		return 1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)gPortNo); // 自分が開放する受信ポート
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("bind");
		return 1;
	}

	while (1) {
		// recvfrom()を利用してUDPソケットからデータを受信
		addrlen = sizeof(senderinfo);
		ssize_t read_size; // 符号付きsize_t型
		read_size = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&senderinfo, &addrlen);
		if (read_size > 0) {
			ret = checkASPH(buf, read_size); // ASPH解析
			if (ret == 1) {
				checkCmd(); // コマンド判定とその後の処理
			}
		}
	}
	close(sock);
}
