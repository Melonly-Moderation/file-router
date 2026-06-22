FROM cgr.dev/chainguard/rust:latest-dev AS build

WORKDIR /src

COPY Cargo.toml Cargo.lock ./
COPY src ./src

RUN cargo build --release --locked

FROM cgr.dev/chainguard/glibc-dynamic:latest

COPY --from=build /src/target/release/file-router /file-router

USER 65532:65532
EXPOSE 8080

ENTRYPOINT ["/file-router"]
