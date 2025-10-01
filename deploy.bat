scp McThCom\mcthcom_* arma: || (pause && exit 1)
scp Udp\udp arma: || (pause && exit 1)
scp Udp\udpConfig_armadillo.ini arma:udpConfig.ini || (pause && exit 1)
scp Udp\tdata.txt arma: || (pause && exit 1)
pause