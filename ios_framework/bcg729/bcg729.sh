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

ARCH=$1
SDK=$2
SOURCE_DIR="${CURRENTPATH}/src/bcg729-${VERSION}"
BUILD_DIR="${CURRENTPATH}/bin"
DESTINATION_DIR="${CURRENTPATH}/build"
DESTINATION_LIB_DIR="$DESTINATION_DIR/lib"
DESTINATION_HEADERS_DIR="$DESTINATION_DIR/include/bcg729"

# List of all available platforms: https://github.com/leetal/ios-cmake/blob/master/README.md
if [[ "${SDK}" == "iphonesimulator" ]]; then
    if [[ "${ARCH}" == "arm64" ]]; then
        PLATFORM="SIMULATORARM64"
    else
        PLATFORM="SIMULATOR64"
    fi
else
    PLATFORM="OS64"
fi

INSTALL_DIR="$BUILD_DIR/$PLATFORM.sdk"
CONF_LOG="$INSTALL_DIR/logs/conf-BCG729-${VERSION}.log"
BUILD_LOG="$INSTALL_DIR/logs/build-BCG729-${VERSION}.log"
INSTALL_LOG="$INSTALL_DIR/logs/install-BCG729-${VERSION}.log"

_build() {
    rm -rf $INSTALL_DIR
    mkdir -p $INSTALL_DIR/logs 

    echo "Configurating bcg729-${VERSION} for sdk: $SDK arch: $ARCH platform: $PLATFORM ..."
    echo Xcode path: $DEVELOPER
    
    cmake . -G Xcode \
        -DPLATFORM=$PLATFORM \
        -DDEPLOYMENT_TARGET=${MIN_IOS_VERSION} \
        -DCMAKE_TOOLCHAIN_FILE="${CURRENTPATH}/ios.toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
        -DENABLE_BITCODE=0 \
        > "${CONF_LOG}" 2>&1

    echo "Building bcg729-${VERSION} ..."

    cmake --build . --config Release > "${BUILD_LOG}" 2>&1

    echo "Installing bcg729-${VERSION} ..."
    cmake --install . --config Release > "${INSTALL_LOG}" 2>&1

    rm $INSTALL_DIR/lib/*.dylib
    rm -rf $INSTALL_DIR/lib/pkgconfig
}

_copy_to_destination() {
    cp $INSTALL_DIR/lib/*.a $DESTINATION_LIB_DIR
    cp $INSTALL_DIR/include/bcg729/*.h $DESTINATION_HEADERS_DIR
}

rm -rf $DESTINATION_DIR
mkdir -p $DESTINATION_LIB_DIR
mkdir -p $DESTINATION_HEADERS_DIR

if [ -z $DEV_MODE ] || [ ! -d "$INSTALL_DIR" ];
then
    echo "Unpacking BCG729"
    rm -rf $SOURCE_DIR
    mkdir -p $SOURCE_DIR 
    tar zxf bcg729-${VERSION}.tar.gz -C "${CURRENTPATH}/src"
    cd $SOURCE_DIR

    echo "Building BCG729 ${VERSION} for $PLATFORM"
    _build
    _copy_to_destination
    echo "Building done."
else
    echo DEV mode is enabled and BCG729 library for $SDK and $ARCH already built. Reusing it.
    _copy_to_destination 
fi







