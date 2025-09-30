/*
 FILE NAME    :  McThCom.cpp
 FUNCTION     :  PLC(MCプロトコル)とサーミカムの通信処理
 CREATE       :  2023/08/23   A.Mitani
 UPDATE       :  2025/09/17   A.Mitani 猪ノ鼻トンネル向け  
 (C) COPYRIGHT 2023 SOHATSU SYSTEM LABORATORY INC.
*/

/*-------------------
	関数ヘッダ
  -----------------*/
#include <cstdio>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h> 
#include <errno.h>
#include <string>
#include <sys/time.h>
#include <netdb.h>
#include <ctime>

/*-------------------
	定義
  -----------------*/
//PLC(MCプロトコル)
#define MC_MSG_SIZE 1024				//メッセージサイズ
#define MC_BUFSIZE (MC_MSG_SIZE + 1)	//メッセージバッファサイズ

#define MC_ADRESS	"192.168.3.1"		//アドレス
#define MC_PORT	"5015"					//ポート

//サーミカム
#define TC1_ADRESS	"192.168.3.151"		//TC1_IPアドレス
#define TC1_PORT	"80"				//TC1_ポート

#define BUF_URL 256				//URL文字列バッファ
#define BUF_SED 1024			//送信データバッファ
#define BUF_RES 1024 * 5		//受信データバッファ
#define STRING_DATA 1024		//受信データ文字列バッファ

//URLの構造体
struct URL {
	char host[BUF_URL];
	char path[BUF_URL];
	char port[BUF_URL];
};

//交通データ列挙
enum e_data_num {
	UP_HEAVY_NUM,
	UP_HEAVY_SPD,
	UP_LIGHT_NUM,
	UP_LIGHT_SPD,
	UP_OCC,
	DW_HEAVY_NUM,
	DW_HEAVY_SPD,
	DW_LIGHT_NUM,
	DW_LIGHT_SPD,
	DW_OCC,
};

/*-------------------
	グローバル変数
  -----------------*/


  /*-------------------
	  時刻の設定
	-----------------*/
int th_time_set(struct URL stUrl) {
	int nAddinfoErr;	//アドレスインフォ返送値
	int nSoket;			//ソケット返送値
	int nReadSize;		//読込データサイズ

	bool bRet = false;	//返送値

	std::string sBufRes = "";				//返送データバッファ

	char cBufSend[BUF_SED] = "";		//送信データバッファ
	char cBufRes[BUF_RES] = "";			//受信データバッファ

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;

	printf("%s\n", stUrl.host);

	if ((nAddinfoErr = getaddrinfo(stUrl.host, stUrl.port, &hints, &res)) != 0) {
		printf("TH Error %d\n", nAddinfoErr);
		//ソケットアドレスの開放
		freeaddrinfo(res);
		return false;
	}

	time_t tNowTime = time(NULL);
	struct tm *pNowTime = localtime(&tNowTime);

	//ソケット作成
	if ((nSoket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) >= 0) {
		//接続
		if (0 == connect(nSoket, res->ai_addr, res->ai_addrlen)) {
			//データ送信
			sprintf(cBufSend, "POST /api/device/time HTTP/1.1\r\n");
			write(nSoket, cBufSend, strlen(cBufSend));

			sprintf(cBufSend, "Content-Length: 45\r\n");
			write(nSoket, cBufSend, strlen(cBufSend));

			sprintf(cBufSend, "Content-Type: application/json\r\n");
			write(nSoket, cBufSend, strlen(cBufSend));

			sprintf(cBufSend, "Host:%s\r\n\r\n", stUrl.host);
			write(nSoket, cBufSend, strlen(cBufSend));

			sprintf(cBufSend, "{\r\"time\":\"%d-%02d-%02dT%02d:%02d:%02d.000+09:00\"\r}\r\n\r\n",
				pNowTime->tm_year + 1900, pNowTime->tm_mon + 1, pNowTime->tm_mday, pNowTime->tm_hour, pNowTime->tm_min, pNowTime->tm_sec);
			printf("TH WRITE %s", cBufSend);
			write(nSoket, cBufSend, strlen(cBufSend));

			//データ応答
			nReadSize = read(nSoket, cBufRes, sizeof(cBufRes));
			sBufRes = "";
			if (0 < nReadSize) {
				sBufRes.append(cBufRes, nReadSize);
			}
			printf("TH READ %s\n", sBufRes.c_str());
			bRet = true;
		}
		else {
			printf("TH connect failed\n");
		}
		//ソケットクローズ
		close(nSoket);
	}
	else {
		printf("TH socket creation failed\n");
	}
	//ソケットアドレスの開放
	freeaddrinfo(res);

	return bRet;
}


/*-------------------
	イベント情報の取得
  -----------------*/
int traffic_event_get(struct URL stUrl, bool *bTcEventFail) {
	int nAddinfoErr;	//アドレスインフォ返送値
	int nSoket;			//ソケット返送値
	int nReadSize;		//読込データサイズ
	int nCnt;			//ループカウンタ
	int nWork1;			//作業用
	int nWork2;			//作業用

	bool bRet = false;	//返送値

	std::string sBufRes = "";				//返送データバッファ
	std::string sResData[STRING_DATA] = {""};	//データ文字列

	char cBufSend[BUF_SED] = "";		//送信データバッファ
	char cBufRes[BUF_RES] = "";			//受信データバッファ

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;

	printf("%s\n", stUrl.host);

	if ((nAddinfoErr = getaddrinfo(stUrl.host, stUrl.port, &hints, &res)) != 0) {
		printf("TH Error %d\n", nAddinfoErr);
		//ソケットアドレスの開放
		freeaddrinfo(res);
		return false;
	}

	//故障イベントの初期化
	*bTcEventFail = false;

	//ソケット作成
	if ((nSoket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) >= 0) {
		//接続
		if (0 == connect(nSoket, res->ai_addr, res->ai_addrlen)) {
			//データ要求
			sprintf(cBufSend, "GET /api/events/open\r\n");
			printf("TH WRITE %s", cBufSend);
			write(nSoket, cBufSend, strlen(cBufSend));

			//データ応答
			nReadSize = read(nSoket, cBufRes, sizeof(cBufRes));
			sBufRes = "";
			if (0 < nReadSize) {
				sBufRes.append(cBufRes, nReadSize);
			}
			printf("TH READ %s\n", sBufRes.c_str());

			nWork2 = -1;
			for (nCnt = 0; nCnt < STRING_DATA; nCnt++) {
				nWork1 = sBufRes.find("\"", nWork2 + 1);
				nWork2 = sBufRes.find("\"", nWork1 + 1);

				//受信データ無し
				if (-1 == nWork1 || -1 == nWork2) {
					break;
				}
				sResData[nCnt] = sBufRes.substr(nWork1 + 1, nWork2 - nWork1 - 1);

				//イベントのチェック
				if ("BadVideo" == sResData[nCnt] && "Begin" == sResData[nCnt - 4] && 0 <= nCnt - 4) {
					*bTcEventFail = true;
				}
				//printf("%s\n", sResData[nCnt].c_str());
			}
			bRet = true;
		}
		else {
			printf("TH connect failed\n");
		}
		//ソケットクローズ
		close(nSoket);
	}
	else {
		printf("TH socket creation failed\n");
	}
	//ソケットアドレスの開放
	freeaddrinfo(res);

	return bRet;
}

/*-------------------
	交通データの取得
  -----------------*/
int traffic_data_get(struct URL stUrl, int *nSetData) {
	int nAddinfoErr;	//アドレスインフォ返送値
	int nSoket;			//ソケット返送値
	int nReadSize;		//読込データサイズ
	int nCnt;			//ループカウンタ
	int nWork1;			//作業用
	int nWork2;			//作業用

	bool bRet = false;	//返送値

	int nKindVeh = 0;		//作業用
	int nWorkLightNum = 0;	//作業用
	int nWorkHeavyNum = 0;	//作業用
	int nWorkLightSpd = 0;	//作業用
	int nWorkHeavySpd = 0;	//作業用
	int nWorkOCC = 0;		//作業用

	std::string sBufRes = "";				//返送データバッファ
	std::string sResData[STRING_DATA] = {""};	//データ文字列

	char cBufSend[BUF_SED] = "";		//送信データバッファ
	char cBufRes[BUF_RES] = "";			//受信データバッファ
	
	char cBufTimeS[] = "2023-08-16T16:39:00.000+09:00";
	char cBufTimeE[] = "2023-08-16T16:39:00.000+09:00";

	struct addrinfo hints, *res;

	bool bTC1Set = false;	//TC1データセット完了
	bool bTC2Set = false;	//TC2データセット完了

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;

	printf("%s\n", stUrl.host);

	if ((nAddinfoErr = getaddrinfo(stUrl.host, stUrl.port, &hints, &res)) != 0) {
		printf("TH Error %d\n", nAddinfoErr);
		//ソケットアドレスの開放
		freeaddrinfo(res);
		return false;
	}

	//データ開始時間 00:17:00-> 00:15:50_00:16:10
	time_t tNowTime = time(NULL);
	tNowTime -= 70;
	struct tm *pNowTime = localtime(&tNowTime);
	sprintf(cBufTimeS, "%d-%02d-%02dT%02d:%02d:50.000+09:00", pNowTime->tm_year + 1900, pNowTime->tm_mon + 1, pNowTime->tm_mday, pNowTime->tm_hour, pNowTime->tm_min);

	//データ終了時間
	tNowTime = time(NULL);
	tNowTime -= 50;
	pNowTime = localtime(&tNowTime);
	sprintf(cBufTimeE, "%d-%02d-%02dT%02d:%02d:10.000+09:00", pNowTime->tm_year + 1900, pNowTime->tm_mon + 1, pNowTime->tm_mday, pNowTime->tm_hour, pNowTime->tm_min);

	tNowTime = time(NULL);
	pNowTime = localtime(&tNowTime);

	//ソケット作成
	if ((nSoket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) >= 0) {
		//接続
		if (0 == connect(nSoket, res->ai_addr, res->ai_addrlen)) {
			//データ要求
			sprintf(cBufSend, "GET /api/data?begintime=%s&endtime=%s\r\n", cBufTimeS, cBufTimeE);
			printf("TH WRITE %s", cBufSend);
			write(nSoket, cBufSend, strlen(cBufSend));

			//データ応答
			nReadSize = read(nSoket, cBufRes, sizeof(cBufRes));
			sBufRes = "";
			if (0 < nReadSize) {
				sBufRes.append(cBufRes, nReadSize);
			}
			//printf("TH READ %s\n", sBufRes.c_str());

			nWork2 = -1;
			for (nCnt = 0; nCnt < STRING_DATA; nCnt++) {
				nWork1 = sBufRes.find("\"", nWork2 + 1);
				nWork2 = sBufRes.find("\"", nWork1 + 1);

				//受信データ無し
				if (-1 == nWork1 || -1 == nWork2) {
					break;
				}
				sResData[nCnt] = sBufRes.substr(nWork1 + 1, nWork2 - nWork1 - 1);
			}
			//受信データの時刻を表示
			printf("TH READ TIME %s\n", sResData[8].c_str());
			bRet = true;
		}
		else {
			printf("TH connect failed\n");
		}
		//ソケットクローズ
		close(nSoket);

		//データのセット
		for (nCnt = 0; nCnt < STRING_DATA; nCnt++) {
			//車種確認
			if ("classNr" == sResData[nCnt]) {
				nKindVeh = atoi(sResData[nCnt + 1].c_str());
			}

			//小型
			if (1 == nKindVeh && "numVeh" == sResData[nCnt]) {
				nWorkLightNum = atoi(sResData[nCnt + 1].c_str());
			}
			if (1 == nKindVeh && "speed" == sResData[nCnt]) {
				nWorkLightSpd = atoi(sResData[nCnt + 1].c_str());
			}
			//大型
			if (2 == nKindVeh && "numVeh" == sResData[nCnt]) {
				nWorkHeavyNum = atoi(sResData[nCnt + 1].c_str());
			}
			if (2 == nKindVeh && "speed" == sResData[nCnt]) {
				nWorkHeavySpd = atoi(sResData[nCnt + 1].c_str());
			}
			//占有率
			if ("occupancy" == sResData[nCnt]) {
				nWorkOCC = atoi(sResData[nCnt + 1].c_str());
			}

			//車線選択
			if (false == bTC1Set && "zoneId" == sResData[nCnt] && 1 == atoi(sResData[nCnt + 1].c_str())) {
				nSetData[UP_LIGHT_NUM] = nWorkLightNum;
				nSetData[UP_LIGHT_SPD] = nWorkLightSpd;
				nSetData[UP_HEAVY_NUM] = nWorkHeavyNum;
				nSetData[UP_HEAVY_SPD] = nWorkHeavySpd;
				nSetData[UP_OCC] = nWorkOCC;
				bTC1Set = true;
			}
			if (false == bTC2Set && "zoneId" == sResData[nCnt] && 2 == atoi(sResData[nCnt + 1].c_str())) {
				nSetData[DW_LIGHT_NUM] = nWorkLightNum;
				nSetData[DW_LIGHT_SPD] = nWorkLightSpd;
				nSetData[DW_HEAVY_NUM] = nWorkHeavyNum;
				nSetData[DW_HEAVY_SPD] = nWorkHeavySpd;
				nSetData[DW_OCC] = nWorkOCC;
				bTC2Set = true;
			}
		}
	}
	else {
		printf("TH socket creation failed\n");
	}
	//ソケットアドレスの開放
	freeaddrinfo(res);
	
	return bRet;
}

/*-------------------
	MCプロトコル通信・オープン
  -----------------*/
int server_open(int *nSocket, const char *cAddress, unsigned short usPort) {
	bool bConnect = false;
	struct sockaddr_in sockaddr;

	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	if (inet_aton(cAddress, &sockaddr.sin_addr) == 0) {
		printf("MC Invalid IP cAddress.\n");
	}

	if (usPort == 0) {
		printf("MC invalid usPort number.\n");
	}
	sockaddr.sin_port = htons(usPort);

	if ((*nSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("MC socket() failed.");
	}

	if (connect(*nSocket, (struct sockaddr*) &sockaddr, sizeof(sockaddr)) < 0) {
		perror("MC connect() failed.");
	}
	else {
		bConnect = true;
		//printf("connect to %s\n", inet_ntoa(sockaddr.sin_addr));
	}

	return bConnect;
}
/*-------------------
	MCプロトコル通信・データ送信
  -----------------*/
void server_send(int nServer, const char *cData, int nSize) {
	if (send(nServer, cData, nSize, 0) <= 0) {
		perror("MC send() failed.");
	}
}
/*-------------------
	MCプロトコル通信・データ受信
  -----------------*/
void server_receive(int nServer, char *cBuffer, int nSize) {
	int nReceivedSize = 0;	//受信データサイズ
	
	nReceivedSize = recv(nServer, &cBuffer[0], nSize, 0);
	if (0 <= nReceivedSize) {

	} 
	else if (0 == nReceivedSize) {
		perror("MC ERR_EMPTY_RESPONSE");
	}
	else {
		perror("MC recv() failed.");
	}
}
/*-------------------
	MCプロトコル通信・通信クローズ
  -----------------*/
void server_close(int nServer) {
	if (-1 == close(nServer)) {
		printf("MC fopen error(%d)\n", errno);
		printf("%03d : %s\n", errno, strerror(errno));
		perror("MC close() failed.");
	}
}
/*-------------------
	MCプロトコル通信・データ読込要求
  -----------------*/
int word_read(char *cBuffer) {
	int nCnt;	//ループカウンタ
	char cHeader[] = {
		(char)0x50, (char)0x00,		//サブヘッダ
		(char)0x00,					//ネットワーク番号
		(char)0xFF,					//PC番号
		(char)0xFF, (char)0x03,		//要求先ユニットI／O番号
		(char)0x00,					//要求先ユニット局番号
		(char)0x0C, (char)0x00,		//要求データ長 12⇒0Ch
		(char)0x03, (char)0x00,		//CPU監視タイマ
		(char)0x01, (char)0x04,		//コマンド
		(char)0x00, (char)0x00,		//サブコマンド
	};

	for (nCnt = 0; nCnt < 15; nCnt++) {
		cBuffer[nCnt] = cHeader[nCnt];
	}
	//cBuffer[15] = (char)0x00;				//先頭デバイス番号D0
	//cBuffer[15] = (char)0xFE;				//先頭デバイス番号D9470(24FE)
	//cBuffer[16] = (char)0x24;
	cBuffer[15] = (char)0xAC;				//先頭デバイス番号D3500(0DAC)
	cBuffer[16] = (char)0x0D;
	cBuffer[17] = (char)0x00;

	cBuffer[18] = (char)0xA8;				//デバイス種別

	cBuffer[19] = (char)0x06;				//デバイス点数 年、月、日、時、分、秒で6個
	cBuffer[20] = (char)0x00;

	return 21;
}

/*-------------------
	MCプロトコル通信・平均車速算出
  -----------------*/
int avg_speed(int nHeavyNum, int nHeavySpd, int nLightNum, int nLightSpd) {
	int nWork = 0;
	if (0 == nHeavyNum + nLightNum) {
		return 0;
	}
	else {
		nWork = (nHeavyNum * nHeavySpd + nLightNum * nLightSpd) / (nHeavyNum + nLightNum);

		if (255 < nWork) {
			nWork = 255;
		}
		return nWork;
	}
}

/*-------------------
	MCプロトコル通信・10進数の16進数文字列変換
  -----------------*/
char C10_to_16(int nValue) {
	char cWork[] = { (char)0x00 };

	if (255 < nValue) {
		nValue = 255;
	}
	sprintf(cWork, "%c", nValue);

	return cWork[0];
}

/*-------------------
	MCプロトコル通信・データ書込み要求
  -----------------*/
int word_write(const int *nSetData1, bool bTc1Fail, char *cBuffer, int nHelth) {
	int nCnt;		//ループカウンタ
	int nDCnt;		//ポジションカウンタ
	int nSpdWork;	//平均車速

	char cHeader[] = {
		(char)0x50, (char)0x00,		//サブヘッダ
		(char)0x00,					//ネットワーク番号
		(char)0xFF,					//PC番号
		(char)0xFF, (char)0x03,		//要求先ユニットI／O番号
		(char)0x00,					//要求先ユニット局番号
		(char)0x36, (char)0x00,		//要求データ長 12 + 2 * 21個 = 54⇒36h
		(char)0x03, (char)0x00,		//CPU監視タイマ
		(char)0x01, (char)0x14,		//コマンド
		(char)0x00, (char)0x00,		//サブコマンド
	};

	for (nCnt = 0; nCnt < 15; nCnt++) {
		cBuffer[nCnt] = cHeader[nCnt];
	}
	//cBuffer[15] = (char)0x64;				//先頭デバイス番号 D100
	//cBuffer[15] = (char)0x08;				//先頭デバイス番号 D9480(2508)
	//cBuffer[16] = (char)0x25;
	cBuffer[15] = (char)0xB6;				//先頭デバイス番号 D3510(0DB6)
	cBuffer[16] = (char)0x0D;	
	cBuffer[17] = (char)0x00;

	cBuffer[18] = (char)0xA8;				//デバイス種別 Dデバイス

	cBuffer[19] = (char)0x15;				//デバイス点数 21個 15h
	cBuffer[20] = (char)0x00;

	//  D3510	上り線 大型車交通量
	//	D3511	上り線 小型車交通量
	//	D3512	上り線 平均車速
	//	D3513	上り線 占有率
	//	D3514	上り線 故障
	//	D3515	下り線 大型車交通量
	//	D3516	下り線 小型車交通量
	//	D3517	下り線 平均車速
	//	D3518	下り線 占有率
	//	D3519	下り線 故障

	nDCnt = 0;
	nSpdWork = avg_speed(
		nSetData1[UP_HEAVY_NUM], nSetData1[UP_HEAVY_SPD], nSetData1[UP_LIGHT_NUM], nSetData1[UP_LIGHT_SPD]);
	cBuffer[21 + nDCnt] = C10_to_16(nSetData1[UP_HEAVY_NUM]);//上り・大型車
	cBuffer[22 + nDCnt] = (char)0x00;
	cBuffer[23 + nDCnt] = C10_to_16(nSetData1[UP_LIGHT_NUM]);//上り・小型車
	cBuffer[24 + nDCnt] = (char)0x00;
	cBuffer[25 + nDCnt] = C10_to_16(nSpdWork);				//上り・車速
	cBuffer[26 + nDCnt] = (char)0x00;
	cBuffer[27 + nDCnt] = C10_to_16(nSetData1[UP_OCC]);		//上り・占有率
	cBuffer[28 + nDCnt] = (char)0x00;
	cBuffer[29 + nDCnt] = (char)bTc1Fail;					//上り・故障
	cBuffer[30 + nDCnt] = (char)0x00;

	nDCnt = 10;
	nSpdWork = avg_speed(
		nSetData1[DW_HEAVY_NUM], nSetData1[DW_HEAVY_SPD], nSetData1[DW_LIGHT_NUM], nSetData1[DW_LIGHT_SPD]);
	cBuffer[21 + nDCnt] = C10_to_16(nSetData1[DW_HEAVY_NUM]);//下り・大型車
	cBuffer[22 + nDCnt] = (char)0x00;
	cBuffer[23 + nDCnt] = C10_to_16(nSetData1[DW_LIGHT_NUM]);//下り・小型車
	cBuffer[24 + nDCnt] = (char)0x00;
	cBuffer[25 + nDCnt] = C10_to_16(nSpdWork);				//下り・車速
	cBuffer[26 + nDCnt] = (char)0x00;
	cBuffer[27 + nDCnt] = C10_to_16(nSetData1[DW_OCC]);		//下り・占有率
	cBuffer[28 + nDCnt] = (char)0x00;
	cBuffer[29 + nDCnt] = (char)bTc1Fail;					//下り・故障
	cBuffer[30 + nDCnt] = (char)0x00;

	nDCnt = 20;
	nDCnt = 30;
	cBuffer[31 + nDCnt] = C10_to_16(nHelth);			//ヘルスカウンタ(秒)
	cBuffer[32 + nDCnt] = (char)0x00;

	return (33 + nDCnt + 1);
}
/*-------------------
	アルマジロ時刻修正
  -----------------*/
int time_set(const char *cBuffer) {
	int nYear = 0;		//年
	int nMonth = 0;		//月
	int nDay = 0;		//日
	int nHour = 0;		//時
	int nMin = 0;		//分
	int nSec = 0;		//秒

	struct timeval tTimeSet;	//時刻
	time_t tTime_t;				//時刻
	struct tm *tTime_tm;		//時刻

	bool bRet = false;

	// 時刻修正
	// 値セット
	nYear = cBuffer[11] - '0' + 48 + 2000;
	nMonth= cBuffer[13] - '0' + 48;
	nDay  = cBuffer[15] - '0' + 48;
	nHour = cBuffer[17] - '0' + 48;
	nMin  = cBuffer[19] - '0' + 48;
	nSec  = cBuffer[21] - '0' + 48;

	// 現在時刻の取得
	time(&tTime_t);
	tTime_tm = localtime(&tTime_t);
	printf("localtimeGet = %s", asctime(localtime(&tTime_t)));

	if (1900 < nYear && 
		1 <= nMonth && 12 >= nMonth && 1 <= nDay && 31 >= nDay &&
		0 <= nHour  && 24 >= nHour  && 0 <= nMin && 60 >= nMin &&
		0 <= nSec && 60 >= nSec) { 

		tTime_tm->tm_year = nYear - 1900;
		tTime_tm->tm_mon  = nMonth - 1;
		tTime_tm->tm_mday = nDay;
		tTime_tm->tm_hour = nHour;
		tTime_tm->tm_min = nMin;
		tTime_tm->tm_sec = nSec;

		printf("PLC Time %d/%02d/%02d %02d:%02d:%02d\n", nYear, nMonth, nDay, nHour, nMin, nSec);

		// 秒に変換
		tTime_t = mktime(tTime_tm);
		// 値をセット
		tTimeSet.tv_usec = 0;
		tTimeSet.tv_sec = tTime_t;
		printf("PC localtimeSet = %s", asctime(localtime(&tTimeSet.tv_sec)));

		// 時刻修正(Suでないと反映されない)
		if (settimeofday(&tTimeSet, NULL) != 0) {
			perror("settimeofday");
		}
		else {
			//設定に成功したら時刻を取得し、表示する
			gettimeofday(&tTimeSet, NULL);
			printf("PC localtimeGet = %s", asctime(localtime(&tTimeSet.tv_sec)));
			bRet = true;
		}
	}
	return bRet;
}

/*-------------------
	メイン
  -----------------*/
int main(int argc, char* argv[]) {
	bool bConnect = false;	//接続返送値
	int nSocket = 0;	//接続返送値
	int nSizeWrite = 0;	//書込データサイズ
	int nSizeRead = 0;	//読込データサイズ

	time_t tTime_t;				//時刻
	struct tm *tTime_tm;		//時刻

	char cAddress[BUF_URL] = MC_ADRESS;	//PLCアドレス
	unsigned short usPort = (unsigned short)atoi(MC_PORT);	//PLCポート

	char cSend_buffer[MC_BUFSIZE] = "";	//送信データバッファ
	char cRecv_buffer[MC_BUFSIZE] = "";	//受信データバッファ

	struct URL stUrl1 = { TC1_ADRESS, "/", TC1_PORT };

	int nSetData1[20] = { 0 };	//TC1からの受信データ

	bool bTc1Fail = false;		//TC1故障
	bool bTc1EventFail = false;	//TC1イベント故障

	int nTimeOldHour = 99;	//時刻修正、前回時間

	bool bFirst = false;	//起動初期

	while (true) {
		try {
			// 現在時刻の取得
			time(&tTime_t);
			tTime_tm = localtime(&tTime_t);

			//PLC接続
			bConnect = server_open(&nSocket, cAddress, usPort);
			//PLC読書き
			if (true == bConnect) {
				nSizeRead = word_read(cSend_buffer);
				server_send(nSocket, cSend_buffer, nSizeRead);
				server_receive(nSocket, cRecv_buffer, sizeof(cRecv_buffer));

				nSizeWrite = word_write(nSetData1, bTc1Fail, cSend_buffer, tTime_tm->tm_sec);
				server_send(nSocket, cSend_buffer, nSizeWrite);
				server_receive(nSocket, cRecv_buffer, sizeof(cRecv_buffer));
				server_close(nSocket);

				//時刻修正(1時間に一回)
				if ((nTimeOldHour != tTime_tm->tm_hour && 0 <= tTime_tm->tm_sec) || false == bFirst) {
					time_set(cRecv_buffer);	//アルマジロの時刻
					th_time_set(stUrl1);	//サーミカムの時刻
					nTimeOldHour = tTime_tm->tm_hour;	//前回時間の更新
					bFirst = true;
				}
			}
			else
			{
				server_close(nSocket);
				bFirst = false;
			}
			sleep(1);

			//交通データの取得
			bTc1Fail = false;
			if (false == traffic_event_get(stUrl1, &bTc1EventFail)) {
				bTc1Fail = true;
			} else {
				if (false == traffic_data_get(stUrl1, nSetData1)) {
					bTc1Fail = true;
				}
			}
			bTc1Fail = bTc1Fail || bTc1EventFail;
		}
		catch(...) {
			perror("Err");
			server_close(nSocket);
		}
	}
}
