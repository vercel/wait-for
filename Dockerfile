FROM alpine:3.7 AS build
WORKDIR /src
RUN apk add --no-cache alpine-sdk cmake
COPY . ./
ARG CMAKE_BUILD_TYPE
RUN mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE-Release}" && cmake --build .

FROM scratch
COPY --from=build /src/build/wait-for /bin/wait-for
