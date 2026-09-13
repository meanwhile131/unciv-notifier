FROM alpine AS build
RUN apk add --no-cache cmake ninja g++ git gperf openssl-dev zlib-dev curl-dev
RUN apk add --no-cache --repository=https://dl-cdn.alpinelinux.org/alpine/edge/testing telegram-tdlib-static telegram-tdlib-dev
WORKDIR /root

COPY CMakeLists.txt .
COPY src src

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
RUN cmake --build build

FROM alpine AS run
RUN apk add --no-cache zlib openssl libcurl libstdc++ libgcc
COPY --from=build /root/build/main /
CMD ["/main"]
