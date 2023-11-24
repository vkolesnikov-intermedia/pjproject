#!/bin/bash

#  Automatic build script for libopus
#  for iPhoneOS and iPhoneSimulator

###########################################################################
#  Change values here                                                     #
#                                                                         #
VERSION="1.3.1"   
if test "x${MIN_IOS_VERSION}" = "x"; then
  MIN_IOS_VERSION="14.0"
  echo "$F: MIN_IOS_VERSION is not specified, using ${MIN_IOS_VERSION}"
fi                                                   #
#                                                                         #
###########################################################################
#                                                                         #
# Don't change anything under this line!                                  #
#                                                                         #
###########################################################################

#configure options
OPUS_CONFIGURE_OPTIONS="--enable-float-approx \
                        --disable-shared \
                        --enable-static \
                        --with-pic \
                        --disable-extra-programs \
                        --disable-doc"

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

if [ ! -e opus-${VERSION}.tar.gz ]; then
    echo "Downloading opus-${VERSION}.tar.gz"
    curl -O -L -s https://archive.mozilla.org/pub/opus/opus-${VERSION}.tar.gz
else
    echo "Using opus-${VERSION}.tar.gz"
fi

ARCH=$1
SDK=$2
BUILD_DIR="${CURRENTPATH}/bin"
SOURCE_DIR="${CURRENTPATH}/src"
DESTINATION_DIR="${CURRENTPATH}/build"
DESTINATION_LIB_DIR="$DESTINATION_DIR/lib"
DESTINATION_HEADERS_DIR="$DESTINATION_DIR/include/opus"

if [[ "${ARCH}" == "x86_64" ]];
then
    HOST=x86_64-apple-darwin
else 
    HOST=arm-apple-darwin 
fi

if [[ "${SDK}" == "iphonesimulator" ]];
then
    PLATFORM="iPhoneSimulator"
    ios_version_flag="-mios-simulator-version-min=$MIN_IOS_VERSION"
else 
    PLATFORM="iPhoneOS"
    ios_version_flag="-miphoneos-version-min=$MIN_IOS_VERSION"
fi

INSTALL_DIR="$BUILD_DIR/${PLATFORM}-${ARCH}.sdk"

_build() {
    rm -rf $INSTALL_DIR 
    mkdir -p $INSTALL_DIR 

    export CROSS_TOP=`xcrun -sdk $SDK --show-sdk-platform-path`/Developer
    export CROSS_SDK="${PLATFORM}.sdk"
    export BUILD_TOOLS="${DEVELOPER}"

    echo "Building opus-${VERSION} for ${PLATFORM} ${ARCH}"
    echo Toolchain: $CROSS_TOP
    echo "Please stand by..."

    mkdir -p $INSTALL_DIR
    LOG="$INSTALL_DIR/build-opus-${VERSION}.log"

    export CC="xcrun -sdk ${SDK} clang $ios_version_flag -arch ${ARCH}"
    CFLAGS="-arch ${ARCH} -D__OPTIMIZE__ -fembed-bitcode"

    set +e

    ./configure --host=${HOST} ${OPUS_CONFIGURE_OPTIONS} --prefix="${INSTALL_DIR}" CFLAGS="${CFLAGS}" > "${LOG}" 2>&1

    if [ $? != 0 ];
    then
        echo "Problem while configure - Please check ${LOG}"
        exit 1
    fi

    # add -isysroot to CC=
    sed -ie "s!^CFLAG=!CFLAG=-isysroot ${CROSS_TOP}/SDKs/${CROSS_SDK} $ios_version_flag !" "Makefile"

    if [ "$1" == "verbose" ];
    then
        make -j$(sysctl -n hw.ncpu)
    else
        make -j$(sysctl -n hw.ncpu) >> "${LOG}" 2>&1
    fi

    if [ $? != 0 ];
    then
        echo "Problem while make - Please check ${LOG}"
        exit 1
    fi

    set -e
    make install >> "${LOG}" 2>&1
    make clean >> "${LOG}" 2>&1
}

_copy_to_destination() {
    cp $INSTALL_DIR/lib/*.a $DESTINATION_LIB_DIR
    cp $INSTALL_DIR/include/opus/*.h $DESTINATION_HEADERS_DIR
}

rm -rf $DESTINATION_DIR
mkdir -p $DESTINATION_LIB_DIR
mkdir -p $DESTINATION_HEADERS_DIR

if [ -z $DEV_MODE ] || [ ! -d "$INSTALL_DIR" ];
then
    echo Unpacking Opus
    rm -rf $SOURCE_DIR
    mkdir -p $SOURCE_DIR 
    tar zxf opus-${VERSION}.tar.gz -C "$SOURCE_DIR"
    cd "$SOURCE_DIR/opus-${VERSION}"

    _build
    _copy_to_destination
    echo "Building done."
else
    echo DEV mode is enabled and OPUS library for $SDK and $ARCH already built. Reusing it.
    _copy_to_destination 
fi
