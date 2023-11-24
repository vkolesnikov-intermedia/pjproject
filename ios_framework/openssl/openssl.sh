#!/bin/bash

#  Automatic build script for libssl and libcrypto
#  for iPhoneOS and iPhoneSimulator

###########################################################################
#  Change values here                                                     #
#                                                                         #
VERSION="1.1.1n"                                                           #  
if test "x${MIN_IOS_VERSION}" = "x"; then
  MIN_IOS_VERSION="14.0"
  echo "$F: MIN_IOS_VERSION is not specified, using ${MIN_IOS_VERSION}"
fi                                                #
#                                                                         #
###########################################################################
#                                                                         #
# Don't change anything under this line!                                  #
#                                                                         #
###########################################################################

set -e

OPENSSL_VERSION="openssl-${VERSION}"
CURRENTPATH=`pwd`
DEVELOPER=`xcode-select -print-path`

#configure options
OPENSSL_CONFIGURE_OPTIONS="-no-shared -no-tests -no-ui-console -fPIC"

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

ARCH=$1
SDK=$2
BUILD_DIR="${CURRENTPATH}/bin"
DESTINATION_DIR="${CURRENTPATH}/build"
DESTINATION_LIB_DIR="$DESTINATION_DIR/lib"
DESTINATION_HEADERS_DIR="$DESTINATION_DIR/include/openssl"

if [[ "${SDK}" == "iphonesimulator" ]]; then
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

    pushd . > /dev/null
    cd "${OPENSSL_VERSION}"

    export $PLATFORM
    export CROSS_TOP=`xcrun -sdk $SDK --show-sdk-platform-path`/Developer
    export CROSS_SDK="${PLATFORM}.sdk"
    export BUILD_TOOLS="${DEVELOPER}"
    export CC="${BUILD_TOOLS}/usr/bin/gcc -fembed-bitcode -arch ${ARCH}"

    BUILD_LOG="${INSTALL_DIR}/build-openssl-${VERSION}.log"
    CONFIG_LOG="${INSTALL_DIR}/config-openssl-${VERSION}.log"

    echo "Building ${OPENSSL_VERSION} || ${PLATFORM} ${ARCH}"
    echo Toolchain: $CROSS_TOP

    if [ "$SDK" == "iphonesimulator" ];
    then
        ./Configure iossimulator-xcrun "-arch $ARCH -fembed-bitcode" ${OPENSSL_CONFIGURE_OPTIONS} --prefix="${INSTALL_DIR}" --openssldir="${INSTALL_DIR}" &> "${CONFIG_LOG}"
    else
        ./Configure iphoneos-cross DSO_LDFLAGS=-fembed-bitcode --prefix="${INSTALL_DIR}" $ios_version_flag ${OPENSSL_CONFIGURE_OPTIONS} --openssldir="${INSTALL_DIR}" &> "${CONFIG_LOG}"
    fi

    if [ $? != 0 ];
    then
        echo "Problem while configure - Please check ${CONFIG_LOG}"
        exit 1
    fi

    # add -isysroot to CC=
    sed -ie "s!^CFLAGS=!CFLAGS=-isysroot ${CROSS_TOP}/SDKs/${CROSS_SDK} $ios_version_flag !" "Makefile"

    perl configdata.pm --dump >> "${CONFIG_LOG}" 2>&1

    make -j $(sysctl -n hw.ncpu) >> "${BUILD_LOG}" 2>&1

    if [ $? != 0 ];
    then
        echo "Problem while make - Please check ${BUILD_LOG}"
        exit 1
    fi

    make install_sw >> "${BUILD_LOG}" 2>&1
    make clean >> "${BUILD_LOG}" 2>&1

    popd > /dev/null
}

_copy_to_destination() {
    cp $INSTALL_DIR/lib/*.a $DESTINATION_LIB_DIR
    cp $INSTALL_DIR/include/openssl/*.h $DESTINATION_HEADERS_DIR
}

if [ ! -e ${OPENSSL_VERSION}.tar.gz ]; then
    echo "Downloading ${OPENSSL_VERSION}.tar.gz"
    curl -O -L -s https://www.openssl.org/source/${OPENSSL_VERSION}.tar.gz
else
    echo "Using ${OPENSSL_VERSION}.tar.gz"
fi

rm -rf $DESTINATION_DIR
mkdir -p $DESTINATION_LIB_DIR
mkdir -p $DESTINATION_HEADERS_DIR

if [ -z $DEV_MODE ] || [ ! -d "$INSTALL_DIR" ];
then
    echo "Unpacking OpenSSL"
    rm -rf $OPENSSL_VERSION
    tar xfz "${OPENSSL_VERSION}.tar.gz"

    echo "Building OpenSSL ${VERSION} for iOS"
    _build
    _copy_to_destination
    echo "Building done."
else
    echo DEV mode is enabled and OpenSSL library for $SDK and $ARCH already built. Reusing it.
    _copy_to_destination 
fi