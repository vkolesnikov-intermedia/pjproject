#!/bin/bash

#  Automatic build script for libbcg729
#  for iPhoneOS and iPhoneSimulator

VERSION="1.1.1"
if test "x${MIN_IOS_VERSION}" = "x"; then
  MIN_IOS_VERSION="14.0"
  echo "$F: MIN_IOS_VERSION is not specified, using ${MIN_IOS_VERSION}"
fi  
CURRENTPATH=`pwd`
DEVELOPER=`xcode-select -print-path`

if [ ! -d "$DEVELOPER" ]; then
    echo "xcode path is not set correctly $DEVELOPER does not exist (most likely because of xcode > 4.3)"
    echo "run"
    echo "sudo xcode-select -switch <xcode path>"
    echo "for default installation:"
    echo "sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer"
    exit 1
fi

if [ ! -x "$(command -v cmake)" ]; then
  echo "Cmake is not installed."
  echo "BCG729 is not included into PJSIP."
  echo "For installation camke:"
  echo "brew install cmake"
  exit 1
fi

case $DEVELOPER in
    *\ * )
    echo "Your Xcode path contains whitespaces, which is not supported."
    exit 1
    ;;
esac

case $CURRENTPATH in
    *\ * )
    echo "Your path contains whitespaces, which is not supported by 'make install'."
    exit 1
    ;;
esac

set -e
if [ ! -e bcg729-${VERSION}.tar.gz ]; then
    echo "Downloading bcg729-${VERSION}.tar.gz"
    curl -O -L -s https://gitlab.linphone.org/BC/public/bcg729/-/archive/${VERSION}/bcg729-${VERSION}.tar.gz
    
else
    echo "Using bcg729-${VERSION}.tar.gz"
fi

mkdir -p "${CURRENTPATH}/src/logs"
mkdir -p "${CURRENTPATH}/build"

tar zxf bcg729-${VERSION}.tar.gz -C "${CURRENTPATH}/src"
cd "${CURRENTPATH}/src/bcg729-${VERSION}"

CONF_LOG="${CURRENTPATH}/src/logs/conf-BCG729-${VERSION}.log"
BUILD_LOG="${CURRENTPATH}/src/logs/build-BCG729-${VERSION}.log"
INSTALL_LOG="${CURRENTPATH}/src/logs/install-BCG729-${VERSION}.log"

echo "Configurating bcg729-${VERSION} ..."
cmake . -G Xcode \
    -DPLATFORM=OS64COMBINED \
    -DDEPLOYMENT_TARGET=${MIN_IOS_VERSION} \
    -DCMAKE_TOOLCHAIN_FILE="${CURRENTPATH}/ios.toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="${CURRENTPATH}/build" \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
    -DENABLE_BITCODE=0 \
     > "${CONF_LOG}" 2>&1

echo "Building bcg729-${VERSION} ..."

cmake --build . --config Release > "${BUILD_LOG}" 2>&1

echo "Installing bcg729-${VERSION} ..."
cmake --install . --config Release > "${INSTALL_LOG}" 2>&1

lipo -info ${CURRENTPATH}/build/lib/libbcg729.a

rm ${CURRENTPATH}/build/lib/*.dylib
rm -rf ${CURRENTPATH}/build/lib/pkgconfig
rm -rf ${CURRENTPATH}/src/bcg729-${VERSION}

