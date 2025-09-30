/*
 * readConfig.c
 *
 *  Created on: 2011/02/01
 *      Author: spinach
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_WORDS 64  // 1行の最大長
#define TITLE_WORDS 32 // 項目の最大長
#define PARAM_WORDS 32 // 値の最大長
#define LANEMAX 4	   // 最大車線数

char gVersion[64] = {0};
char gIpAddr[32] = {0};	   // ユーザー名（IPアドレス）
int gPortNo;			   // ポート番号
int gLaneNum;			   // 総車線数
int gSpotNum;			   // 設置地点数
int gSpotLaneNum[4] = {0}; // 設置地点ごとの車線数
char gRouteCode[4][8];	   // 設置地点ごとの路線コード
int gDirection[4] = {0};   // 設置地点ごとの方向
int gKp[4] = {0};		   // 設置地点ごとのキロポスト

/*
 * config.iniを読み出す
 */
int readConfig(void)
{
	FILE *fp;
	char buff[LINE_WORDS] = {0}, title[TITLE_WORDS] = {0}, param[PARAM_WORDS] = {0};
	int i, j;

	// config.iniをオープン
	fp = fopen("/etc/config/udpConfig.ini", "r");
	if (fp == NULL) {
		perror("config file open");
		printf("%d\n", errno);
		return (-1);
	}

	for (i = 0; i < LINE_WORDS; i++) {
		buff[i] = '\0';
	}

	// 1行読み込み値をセット
	while (fgets(buff, LINE_WORDS, fp) != NULL) {
		// 各バッファを初期化
		for (i = 0; i < TITLE_WORDS; i++) {
			title[i] = '\0';
			param[i] = '\0';
		}
		// 項目名を獲得
		for (i = 0; i < TITLE_WORDS; i++) {
			if (buff[i] == ':') {
				break;
			}
			title[i] = buff[i];
		}
		i++; //':'を読み飛ばす

		// 値を獲得
		for (j = 0; i < PARAM_WORDS; i++, j++) {
			if (buff[i] == '\n') {
				break;
			}
			param[j] = buff[i];
		}

		/*値をセット*/
		if (strcmp(title, "Version") == 0) { // バージョン
			strcpy(gVersion, param);
		}
		else if (strcmp(title, "IPAddres") == 0) { // IPアドレス
			strcpy(gIpAddr, param);
			printf("IP:%s\n", gIpAddr);
		}
		else if (strcmp(title, "PortNo") == 0) { // ポート番号
			gPortNo = atoi(param);
			printf("Port:%d\n", gPortNo);
		}
		else if (strcmp(title, "SpotNum") == 0) { // 設置地点数
			gSpotNum = atoi(param);
			printf("SpotNum:%d\n", gSpotNum);
		}
		else if (strcmp(title, "Spot1LaneNum") == 0) { // 地点1車線数
			gSpotLaneNum[0] = atoi(param);
			printf("Spot1LaneNum:%d\n", gSpotLaneNum[0]);
		}
		else if (strcmp(title, "Spot2LaneNum") == 0) { // 地点2車線数
			gSpotLaneNum[1] = atoi(param);
			printf("Spot2LaneNum:%d\n", gSpotLaneNum[1]);
		}
		else if (strcmp(title, "Spot3LaneNum") == 0) { // 地点3車線数
			gSpotLaneNum[2] = atoi(param);
			printf("Spot3LaneNum:%d\n", gSpotLaneNum[2]);
		}
		else if (strcmp(title, "Spot4LaneNum") == 0) { // 地点4車線数
			gSpotLaneNum[3] = atoi(param);
			printf("Spot4LaneNum:%d\n", gSpotLaneNum[3]);
		}
		else if (strcmp(title, "Spot1Direction") == 0) { // 地点1車線方向
			gDirection[0] = atoi(param);
			printf("Spot1Direction:%d\n", gDirection[0]);
		}
		else if (strcmp(title, "Spot2Direction") == 0) { // 地点2車線方向
			gDirection[1] = atoi(param);
			printf("Spot2Direction:%d\n", gDirection[1]);
		}
		else if (strcmp(title, "Spot3Direction") == 0) { // 地点3車線方向
			gDirection[2] = atoi(param);
			printf("Spot3Direction:%d\n", gDirection[2]);
		}
		else if (strcmp(title, "Spot4Direction") == 0) { // 地点4車線方向
			gDirection[3] = atoi(param);
			printf("Spot4Direction:%d\n", gDirection[3]);
		}
		else if (strcmp(title, "Spot1Kp") == 0) { // 地点1キロポスト
			gKp[0] = atoi(param);
			printf("Kp:%d\n", gKp[0]);
		}
		else if (strcmp(title, "Spot2Kp") == 0) { // 地点2キロポスト
			gKp[1] = atoi(param);
			printf("Kp:%d\n", gKp[1]);
		}
		else if (strcmp(title, "Spot3Kp") == 0) { // 地点3キロポスト
			gKp[2] = atoi(param);
			printf("Kp:%d\n", gKp[2]);
		}
		else if (strcmp(title, "Spot4Kp") == 0) { // 地点4キロポスト
			gKp[3] = atoi(param);
			printf("Kp:%d\n", gKp[3]);
		}
		else if (strcmp(title, "Spot1RouteCode") == 0) { // 地点1路線コード
			strcpy(gRouteCode[0], param);
			printf("Code:%s\n", gRouteCode[0]);
		}
		else if (strcmp(title, "Spot2RouteCode") == 0) { // 地点2路線コード
			strcpy(gRouteCode[1], param);
			printf("Code:%s\n", gRouteCode[1]);
		}
		else if (strcmp(title, "Spot3RouteCode") == 0) { // 地点3路線コード
			strcpy(gRouteCode[2], param);
			printf("Code:%s\n", gRouteCode[2]);
		}
		else if (strcmp(title, "Spot4RouteCode") == 0) { // 地点4路線コード
			strcpy(gRouteCode[3], param);
			printf("Code:%s\n", gRouteCode[3]);
		}
		// バッファ初期化
		for (i = 0; i < LINE_WORDS; i++) {
			buff[i] = '\0';
		}
	}
	// 総車線数
	gLaneNum = gSpotLaneNum[0] + gSpotLaneNum[1] + gSpotLaneNum[2] + gSpotLaneNum[3];
	fclose(fp);
	return 0;
}
