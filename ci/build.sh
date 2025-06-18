#!/bin/bash

set -eux -o pipefail
shopt -s failglob extglob

ZUUL_JOB_NAME=$(jq < ~/zuul-env.json -r '.job')
ZUUL_TENANT=$(jq < ~/zuul-env.json -r '.tenant')
LEAF_PROJECT_NAME=CzechLight/velia
ZUUL_PROJECT_SRC_DIR=$HOME/$(jq < ~/zuul-env.json -r ".projects[] | select(.name == \"${LEAF_PROJECT_NAME}\").src_dir")
ZUUL_PROJECT_SHORT_NAME=$(jq < ~/zuul-env.json -r ".projects[] | select(.name == \"${LEAF_PROJECT_NAME}\").short_name")
ZUUL_GERRIT_HOSTNAME=$(jq < ~/zuul-env.json -r '.project.canonical_hostname')
ZUUL_JOB_NAME_NO_PROJECT=${ZUUL_JOB_NAME##${ZUUL_PROJECT_SHORT_NAME}-}
ZUUL_SRC_COMMON_PREFIX=${ZUUL_PROJECT_SRC_DIR:0:-${#LEAF_PROJECT_NAME}}

CI_PARALLEL_JOBS=$(awk -vcpu=$(getconf _NPROCESSORS_ONLN) 'BEGIN{printf "%.0f", cpu*1.3+1}')
CMAKE_OPTIONS=""
CFLAGS="-O2 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
CXXFLAGS="-O2 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
LDFLAGS=""

if [[ $ZUUL_JOB_NAME =~ .*-clang.* ]]; then
    export CC=clang
    export CXX=clang++
    export LD=clang
fi

if [[ $ZUUL_JOB_NAME =~ .*-ubsan ]]; then
    export CFLAGS="-fsanitize=undefined ${CFLAGS}"
    export CXXFLAGS="-fsanitize=undefined ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=undefined ${LDFLAGS}"
    export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
fi

if [[ $ZUUL_JOB_NAME =~ .*-asan-ubsan ]]; then
    export CFLAGS="-fsanitize=address,undefined -Wp,-U_FORTIFY_SOURCE ${CFLAGS}"
    export CXXFLAGS="-fsanitize=address,undefined -Wp,-U_FORTIFY_SOURCE ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=address,undefined ${LDFLAGS}"
    export ASAN_OPTIONS=intercept_tls_get_addr=0,log_to_syslog=true,handle_abort=2,strip_path_prefix=${ZUUL_SRC_COMMON_PREFIX}
    export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
fi

if [[ $ZUUL_JOB_NAME =~ .*-tsan ]]; then
    export CFLAGS="-fsanitize=thread -Wp,-U_FORTIFY_SOURCE ${CFLAGS}"
    export CXXFLAGS="-fsanitize=thread -Wp,-U_FORTIFY_SOURCE ${CXXFLAGS}"
    export LDFLAGS="-fsanitize=thread ${LDFLAGS}"
    export TSAN_OPTIONS="suppressions=$HOME/target/tsan.supp"
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
export PATH=${PREFIX}/bin:/sbin:/usr/sbin:$PATH
export LD_LIBRARY_PATH=${PREFIX}/lib64:${PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PKG_CONFIG_PATH=${PREFIX}/lib64/pkgconfig:${PREFIX}/lib/pkgconfig:${PREFIX}/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}

ARTIFACT_URL=$(jq < ~/zuul-env.json -r '[.artifacts[]? | select(.name == "tarball") | select(.project == "CzechLight/dependencies")][-1]?.url + ""')

if [[ -z "${ARTIFACT_URL}" ]]; then
    # nothing ahead in the pipeline -> fallback to the latest promoted artifact
    DEPSRCDIR=$(jq < ~/zuul-env.json -e -r ".projects[] | select(.name == \"CzechLight/dependencies\").src_dir")
    DEP_SUBMODULE_COMMIT=$(git --git-dir ${HOME}/${DEPSRCDIR}/.git rev-parse HEAD)
    ARTIFACT_URL="https://object-store.brno.openstack.cloud.e-infra.cz/swift/v1/KEY_b637b9c937414b29b3e277b4a85cc658/ci-artifacts-${ZUUL_TENANT}/${ZUUL_GERRIT_HOSTNAME}/CzechLight/dependencies/deps-${ZUUL_JOB_NAME_NO_PROJECT%%-cover?(-previous)}/${DEP_SUBMODULE_COMMIT}.tar.zst"
fi

curl ${ARTIFACT_URL} | unzstd --stdout | tar -C ${PREFIX} -xf -

# build sysrepo-ietf-alarms project (for tests)
BUILD_SYSREPO_IETF_ALARMS=~/build-alarms
mkdir ${BUILD_SYSREPO_IETF_ALARMS}
pushd ${BUILD_SYSREPO_IETF_ALARMS}
cmake -GNinja -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Debug} -DCMAKE_INSTALL_PREFIX=${PREFIX} ${ZUUL_PROJECT_SRC_DIR}/../sysrepo-ietf-alarms
ninja-build install
popd

cd ${BUILD_DIR}
cmake -GNinja -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Debug} -DCMAKE_INSTALL_PREFIX=${PREFIX} ${CMAKE_OPTIONS} ${ZUUL_PROJECT_SRC_DIR}
ninja-build

set_save=$-
set -E
trap "echo sysrepo-ietf-alarmsd test failed, copying log; cp ${BUILD_DIR}/sysrepo-ietf-alarmsd.log ~/zuul-output/logs" ERR
ctest -j${CI_PARALLEL_JOBS} --output-on-failure
trap - ERR
set +E -$set_save

if [[ $JOB_PERFORM_EXTRA_WORK == 1 ]]; then
    ninja-build doc
    pushd html
    zip -r ~/zuul-output/docs/html.zip .
    popd
fi

if [[ $LDFLAGS =~ .*--coverage.* ]]; then
    gcovr -j ${CI_PARALLEL_JOBS} --object-directory ${BUILD_DIR} --root ${ZUUL_PROJECT_SRC_DIR} --xml --output ${BUILD_DIR}/coverage.xml
fi
