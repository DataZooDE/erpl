FROM ubuntu:18.04

# Setup the basic necessities
RUN apt-get update -y -qq
RUN apt-get install -y -qq software-properties-common
RUN apt-get install -y -qq --fix-missing ninja-build make gcc-multilib g++-multilib libssl-dev wget openjdk-8-jdk zip maven unixodbc-dev libc6-dev-i386 lib32readline6-dev libssl-dev libcurl4-gnutls-dev libexpat1-dev gettext unzip build-essential checkinstall libffi-dev curl libz-dev openssh-client pkg-config
RUN apt-get install -y -qq ccache autoconf

# Setup cross compiler because GH actions does not have open source arm runners yet
RUN apt-get install -y -qq gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Install cmake 3.31
RUN mkdir /cmake_3_31 && \
    cd /cmake_3_31 && \
    wget https://github.com/Kitware/CMake/releases/download/v3.31.3/cmake-3.31.3-linux-x86_64.sh && \
    chmod +x cmake-3.31.3-linux-x86_64.sh && \
    ./cmake-3.31.3-linux-x86_64.sh --skip-license --prefix=/usr/local && \
    cmake --version

# Install GIT
RUN wget https://github.com/git/git/archive/refs/tags/v2.25.0.tar.gz && \
    tar xvf v2.25.0.tar.gz && \
    cd git-2.25.0 && \
    make && \
    make prefix=/usr install

# Setup VCPKG n a mounted volume TODO: figure out how to cache this
ARG vcpkg_url
ARG vcpkg_commit
RUN mkdir /vcpkg && \
    cd /vcpkg && \
    git init && \
    git remote add origin $vcpkg_url && \
    git fetch origin $vcpkg_commit && \
    git checkout $vcpkg_commit && \
    ./bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/vcpkg
ENV VCPKG_TOOLCHAIN_PATH=/vcpkg/scripts/buildsystems/vcpkg.cmake

# Common environment variables
ENV GEN=ninja

# Specify where we expect the extension to be mounted and use that as working dir
VOLUME /duckdb_build_dir
WORKDIR /duckdb_build_dir

# Mount for ccache to allow restoring ccache in GH actions
VOLUME /ccache_dir
ENV CCACHE_DIR=/ccache_dir
ENV CCACHE_COMPRESS=TRUE
ENV CCACHE_COMPRESSLEVEL=6
ENV CCACHE_MAXSIZE=400M

###
# Conditionally configure some extra dependencies
###
# a `;` separated list of extra toolchains to install (passed in like this to makes things easier through GitHub Actions)
# Note that it should start and end with a `;` e.g. `;rust;parser_tools;`
ARG extra_toolchains

# NOTE: the weird case conditionals are because of bash limitations in the ubuntu image used (see https://stackoverflow.com/questions/229551/how-to-check-if-a-string-contains-a-substring-in-bash)

# Install Parser tools
RUN case "$extra_toolchains" in \
  *\;parser_tools\;*) \
    apt-get install -y -qq bison flex \
  ;; \
esac

# Install Fortran
RUN case "$extra_toolchains" in \
  *\;fortran\;*) \
    apt-get install -y -qq gfortran gfortran-aarch64-linux-gnu \
  ;; \
esac

# Configure Rust
ENV PATH="/root/.cargo/bin:${PATH}"
RUN case "$extra_toolchains" in \
  *\;rust\;*) \
    curl https://sh.rustup.rs -sSf | bash -s -- -y; \
    rustup target add aarch64-unknown-linux-gnu \
  ;; \
esac

# Configure go
RUN case "$extra_toolchains" in \
  *\;go\;*) \
    apt-get install -y -qq golang-go \
  ;; \
esac

# Install Python3
RUN case "$extra_toolchains" in \
  *\;python3\;*) \
    apt-get install -y -qq python3 \
  ;; \
esac
