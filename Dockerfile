FROM alpine:latest AS builder

RUN apk add --no-cache gcc musl-dev

WORKDIR /app

COPY main.c .

RUN gcc -O2 -static -o main main.c

FROM scratch

COPY --from=builder /app/main /main

EXPOSE 8080

ENTRYPOINT ["/main"]