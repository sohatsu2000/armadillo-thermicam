docker run --rm --privileged multiarch/qemu-user-static --reset -p yes || pause; exit
docker buildx create --name armv7builder --use || pause; exit
docker buildx inspect --bootstrap || pause; exit

docker buildx build --platform linux/arm/v7 -t cross-compile-armv7:2.0.0 --load . || pause; exit
pause