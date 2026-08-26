FROM alpine:3.20 AS build

RUN apk add --no-cache cmake g++ make

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build /build --parallel

FROM alpine:3.20

RUN apk add --no-cache libstdc++ \
  && addgroup -S tvtime \
  && adduser -S -G tvtime tvtime \
  && mkdir -p /app/public /media \
  && chown -R tvtime:tvtime /app /media

COPY --from=build /build/tvtime_server /usr/local/bin/tvtime_server
COPY index.html app.js styles.css /app/public/

USER tvtime
ENV TVTIME_HOST=0.0.0.0
ENV TVTIME_PORT=8080
EXPOSE 8080
VOLUME ["/media"]
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -qO- "http://127.0.0.1:${TVTIME_PORT}/api/health" >/dev/null || exit 1

CMD ["/usr/local/bin/tvtime_server", "/app/public", "/media"]
