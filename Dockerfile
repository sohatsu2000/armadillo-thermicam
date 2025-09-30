FROM docker.io/arm32v7/alpine:3.22
LABEL version="2.0.0"

# Add apt download source information
COPY atmark_resources/etc/apk/ /etc/apk/

RUN apk upgrade \
    && apk add --no-cache alpine-sdk


# Modify as necessary
RUN adduser -D -u 1000 atmark

ENV LD_LIBRARY_PATH=/vol_app/lib