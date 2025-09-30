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
#include <string.h> 
#include <unistd.h> 
#include <string>
#include <netdb.h>
#include <ctime>


//サーミカム
//#define TC1_ADRESS	"192.168.3.151"		//TC1_IPアドレス
//#define TC1_PORT	"80"				//TC1_ポート
#define TC1_ADRESS	"192.168.3.130"		//JEAN //TC1_IPアドレス
#define TC1_PORT	"12130"				//JEAN //TC1_ポート

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
			sprintf(cBufSend, "GET /api/events/open\r\n\r\n");
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
	
	char cBufTimeS[80] = "2023-08-16T16:39:00.000+09:00";
	char cBufTimeE[80] = "2023-08-16T16:39:00.000+09:00";

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
			sprintf(cBufSend, "GET /api/data?begintime=%s&endtime=%s\r\n\r\n", cBufTimeS, cBufTimeE);
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
	メイン
  -----------------*/
int main(int argc, char* argv[]) {

	struct URL stUrl1 = { TC1_ADRESS, "/", TC1_PORT };

	int nSetData1[20] = { 0 };	//TC1からの受信データ

	bool bTc1Fail = false;		//TC1故障
	bool bTc1EventFail = false;	//TC1イベント故障

	while (true) {
		try {
			
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

            // JEAN print current value
            printf("UP_LIGHT_NUM: %d\t",nSetData1[UP_LIGHT_NUM]);
            printf("UP_LIGHT_SPD: %d\t",nSetData1[UP_LIGHT_SPD]);
            printf("UP_HEAVY_NUM: %d\t",nSetData1[UP_HEAVY_NUM]);
            printf("UP_HEAVY_SPD: %d\t",nSetData1[UP_HEAVY_SPD]);
            printf("UP_OCC      : %d\n",nSetData1[UP_OCC] );
            printf("DW_LIGHT_NUM: %d\t",nSetData1[DW_LIGHT_NUM]);
            printf("DW_LIGHT_SPD: %d\t",nSetData1[DW_LIGHT_SPD]);
            printf("DW_HEAVY_NUM: %d\t",nSetData1[DW_HEAVY_NUM]);
            printf("DW_HEAVY_SPD: %d\t",nSetData1[DW_HEAVY_SPD]);
            printf("DW_OCC      : %d\n",nSetData1[DW_OCC] );
		}
		catch(...) {
			perror("Err");
		}
	}
}
