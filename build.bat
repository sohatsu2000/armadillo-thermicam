docker run --rm -it --platform linux/arm/v7 -v %cd%\McThCom:/src cross-compile-armv7:2.0.0 make -C /src all || pause;exit
docker run --rm -it --platform linux/arm/v7 -v %cd%\Udp:/src cross-compile-armv7:2.0.0 make -C /src udp || pause;exit
echo Finished!
pause