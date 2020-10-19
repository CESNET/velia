#!/bin/bash

set -eux -o pipefail
shopt -s failglob extglob

ZUUL_JOB_NAME=$(jq < ~/zuul-env.json -r '.job')
ZUUL_TENANT=$(jq < ~/zuul-env.json -r '.tenant')
ZUUL_PROJECT_SRC_DIR=$HOME/$(jq < ~/zuul-env.json -r '.project.src_dir')
ZUUL_PROJECT_SHORT_NAME=$(jq < ~/zuul-env.json -r '.project.short_name')
ZUUL_GERRIT_HOSTNAME=$(jq < ~/zuul-env.json -r '.project.canonical_hostname')

CI_PARALLEL_JOBS=$(awk -vcpu=$(getconf _NPROCESSORS_ONLN) 'BEGIN{printf "%.0f", cpu*1.3+1}')
CMAKE_OPTIONS=""
CFLAGS=""
CXXFLAGS=""
LDFLAGS=""

if [[ $ZUUL_JOB_NAME =~ .*-clang.* ]]; then
    export CC=clang
    export CXX=clang++
    export LD=clang
    export CXXFLAGS="-stdlib=libc++"
    # spdlog passes std::string instances, and it's built against GCC's libstdc++ on the CI
    sed -i \
        -e 's/spdlog::spdlog/spdlog::spdlog_header_only/' \
        -e '/find_package(spdlog/ a add_definitions(-DFMT_HEADER_ONLY)' \
        ${ZUUL_PROJECT_SRC_DIR}/CMakeLists.txt
fi

if [[ $ZUUL_JOB_NAME =~ .*-ubsan ]]; then
    export CFLAGS="-fsanitize=undefined ${CFLAGS}"
    export CXXFLAGS="-fsanitize=undefined ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=undefined ${LDFLAGS}"
fi

if [[ $ZUUL_JOB_NAME =~ .*-asan ]]; then
    export CFLAGS="-fsanitize=address ${CFLAGS}"
    export CXXFLAGS="-fsanitize=address ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=address ${LDFLAGS}"
fi

if [[ $ZUUL_JOB_NAME =~ .*-tsan ]]; then
    export CFLAGS="-fsanitize=thread ${CFLAGS}"
    export CXXFLAGS="-fsanitize=thread ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=thread ${LDFLAGS}"
fi

if [[ $ZUUL_JOB_NAME =~ .*-cover.* ]]; then
    export CFLAGS="${CFLAGS} --coverage"
    export CXXFLAGS="${CXXFLAGS} --coverage"
    export LDFLAGS="${LDFLAGS} --coverage"
fi

PREFIX=~/target
mkdir ${PREFIX}
BUILD_DIR=~/build
mkdir ${BUILD_DIR}
export PATH=${PREFIX}/bin:$PATH
export LD_LIBRARY_PATH=${PREFIX}/lib64:${PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PKG_CONFIG_PATH=${PREFIX}/lib64/pkgconfig:${PREFIX}/lib/pkgconfig:${PREFIX}/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}

ARTIFACT_URL=$(jq < ~/zuul-env.json -r '[.artifacts[]? | select(.name == "tarball") | select(.project == "CzechLight/dependencies")][-1]?.url + ""')

if [[ -z "${ARTIFACT_URL}" ]]; then
    # nothing ahead in the pipeline -> fallback to the latest promoted artifact
    DEPSRCDIR=$(jq < ~/zuul-env.json -e -r ".projects[] | select(.name == \"CzechLight/dependencies\").src_dir")
    DEP_SUBMODULE_COMMIT=$(git --git-dir ${HOME}/${DEPSRCDIR}/.git rev-parse HEAD)
    ARTIFACT_URL="https://object-store.cloud.muni.cz/swift/v1/ci-artifacts-${ZUUL_TENANT}/${ZUUL_GERRIT_HOSTNAME}/CzechLight/dependencies/${ZUUL_JOB_NAME%%-cover?(-previous|-diff)}/${DEP_SUBMODULE_COMMIT}.tar.zst"
fi

curl ${ARTIFACT_URL} | unzstd --stdout | tar -C ${PREFIX} -xf -

cd ${BUILD_DIR}
cmake -GNinja -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Debug} -DCMAKE_INSTALL_PREFIX=${PREFIX} ${CMAKE_OPTIONS} ${ZUUL_PROJECT_SRC_DIR}
ninja-build
ctest -j${CI_PARALLEL_JOBS} --output-on-failure

if [[ $JOB_PERFORM_EXTRA_WORK == 1 ]]; then
    ninja-build doc
    pushd html
    zip -r ~/zuul-output/docs/html.zip .
    popd
fi

if [[ $LDFLAGS =~ .*--coverage.* ]]; then
    gcovr -j ${CI_PARALLEL_JOBS} --object-directory ${BUILD_DIR} --root ${ZUUL_PROJECT_SRC_DIR} --xml --output ${BUILD_DIR}/coverage.xml
fi
