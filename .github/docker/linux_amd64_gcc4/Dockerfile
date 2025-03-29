FROM quay.io/pypa/manylinux2014_x86_64:latest

RUN yum install -y epel-release -y pkgconfig

# TODO do we need this?
RUN git config --global --add safe.directory '*'

# TODO do we need these?
RUN yum install -y gcc-c++
RUN yum install -y nodejs

# Setup the basic necessities
RUN yum install -y curl zip unzip tar autoconf
RUN yum install -y ninja-build
RUN yum install -y perl-IPC-Cmd
RUN yum install -y ccache
RUN yum install -y java-11-openjdk-devel maven
RUN yum install -y libgcc*i686 libstdc++*i686 glibc*i686 libgfortran*i686

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

RUN echo "$extra_toolchains" > ~/extra_toolchains.txt

# Install Parser tools
RUN if [[ $extra_toolchains == *";parser_tools;"* ]]; then \
      yum install -y bison flex;\
    fi

# Configure Rust
RUN if [[ $extra_toolchains == *";rust;"* ]] || [ "$enable_rust" = "1" ]; then \
      curl https://sh.rustup.rs -sSf | bash -s -- -y ;\
    fi
ENV PATH="/root/.cargo/bin:${PATH}"

# Configure go
RUN if [[ $extra_toolchains == *";go;"* ]]; then \
      yum install -y wget; \
      wget https://go.dev/dl/go1.23.1.linux-amd64.tar.gz; \
      tar -C /usr/local -xzf go1.23.1.linux-amd64.tar.gz; \
fi
ENV PATH="/usr/local/go/bin:${PATH}"

# Install Python3
RUN case "$extra_toolchains" in \
  *\;python3\;*) \
    yum install -y python3; \
  ;; \
esac
