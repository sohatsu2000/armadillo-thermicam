-include env.mk

all:
	g++ McThCom.cpp -Wall -DTC1_ADRESS=${TEST_ADDR} -DTC1_PORT=${TEST_PORT} -o mcthcom_test
	g++ McThCom.cpp -Wall -DTC1_ADRESS=${TC1_ADDR} -DTC1_PORT=${TC1_PORT} -o mcthcom_tc1
	g++ McThCom.cpp -Wall -DTC1_ADRESS=${TC2_ADDR} -DTC1_PORT=${TC2_PORT} -o mcthcom_tc2

clean:
	rm mcthcom_*
