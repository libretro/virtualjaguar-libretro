# docker/jaguar-toolchain.Dockerfile
#
# Multi-stage: builder stage fetches+builds the Jaguar assembler toolchain
# (rmac, rln, lyxass, pc_jagcrypt/jagcrypt, new_bjl) from source via
# tools/jaguar-toolchain/setup.sh (Tasks 1-3); final stage carries only the
# built binaries and new_bjl's data/inc tree, not the build toolchain.
#
# BUILD CONTEXT: must be built from the repo root, not from docker/, because
# the builder stage COPYs tools/jaguar-toolchain/{PIN,setup.sh}:
#
#   docker build -f docker/jaguar-toolchain.Dockerfile -t jaguar-toolchain:local .
#
# Usage once built -- run against a mounted copy of this repo:
#
#   docker run --rm -v "$PWD":/workspace jaguar-toolchain:local make jaguar-demos-build
#
# NOTE: `make jaguar-demos-build` depends on `jaguar-demos-fetch`, which
# shells out to git against codeberg.org every invocation (even when
# test/vendor/JaguarDemos is already cloned in the mounted volume, it still
# runs `git remote set-url` / `git fetch`) -- hence git + ca-certificates are
# installed in the final stage below, not just the builder stage. Verified
# with `make -n jaguar-demos-build` on the host: exactly those two `run.sh`
# invocations, nothing else shells out to git at parse time for this target.
#
# The container runs as root by default, so git refuses a bind-mounted repo
# owned by the host user ("detected dubious ownership") without the
# safe.directory escape hatch below -- and anything the build writes into
# the mount (e.g. test/vendor/JaguarDemos) lands root-owned on the host.
# Pass `--user "$(id -u):$(id -g)"` to `docker run` to avoid the latter.

FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    git build-essential ca-certificates bzip2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY tools/jaguar-toolchain/PIN tools/jaguar-toolchain/setup.sh /src/tools/jaguar-toolchain/
RUN bash tools/jaguar-toolchain/setup.sh fetch \
    && bash tools/jaguar-toolchain/setup.sh build

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    bzip2 make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/tools/vendor/jaguar-toolchain /opt/jaguar-toolchain
# pc_jagcrypt's built binary is named `jagcrypt` (upstream's own naming, not
# renamed here) -- adding its directory to PATH is what the task interface
# means by "pc_jagcrypt on PATH"; there is no `pc_jagcrypt` command.
ENV PATH="/opt/jaguar-toolchain/rmac:/opt/jaguar-toolchain/rln:/opt/jaguar-toolchain/lyxass:/opt/jaguar-toolchain/pc_jagcrypt:${PATH}"
ENV BJL_ROOT="/opt/jaguar-toolchain/new_bjl"
RUN git config --global --add safe.directory '*'
WORKDIR /workspace
CMD ["/bin/bash"]
