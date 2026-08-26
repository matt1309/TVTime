FROM gcc:13-bookworm AS build

WORKDIR /src
COPY . .

RUN g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
  -static-libstdc++ -static-libgcc \
  -Iinclude \
  src/core/media_library.cpp \
  src/core/schedule.cpp \
  src/plugins/local_file_source.cpp \
  src/server/http_server.cpp \
  src/server/main.cpp \
  -o /usr/local/bin/tvtime_server

FROM debian:bookworm-slim

WORKDIR /app

COPY --from=build /usr/local/bin/tvtime_server /usr/local/bin/tvtime_server
COPY index.html app.js styles.css /app/public/

USER 10001:10001
ENV TVTIME_HOST=0.0.0.0
ENV TVTIME_PORT=8080
EXPOSE 8080
VOLUME ["/media"]

CMD ["/usr/local/bin/tvtime_server", "/app/public", "/media"]
