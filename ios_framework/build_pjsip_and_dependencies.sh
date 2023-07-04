#!/bin/bash
set -e

for i in "$@"
do
case $i in
    --min-ios-version=*)
    min_ios_version="${i#*=}"
    shift
    ;;
    --output-dir=*)
    output_dir="${i#*=}"
    shift
    ;;
    --source-dir=*)
    source_dir="${i#*=}"
    shift
    ;;
    --bcg729=*)
    bcg729_dir="${i#*=}"
    shift
    ;;
    --openssl=*)
    openssl_dir="${i#*=}"
    shift
    ;;
    --opus=*)
    opus_dir="${i#*=}"
    shift
    ;;
    --config-site=*)
    config_site="${i#*=}"
    shift
    ;;
    *)
    ;;
esac
done

function check_or_exit() {
    if [ -z "$2" ]; then
        echo Please specify "$1"
        exit 1
    fi
}

check_or_exit "minimum iOS version using --min-ios-version" $min_ios_version
check_or_exit "output directory using --output-dir" $output_dir
check_or_exit "source directory using --source-dir" $source_dir
check_or_exit "source BCG729 directory using --bcg729" $bcg729_dir
check_or_exit "source OpenSSL directory using --openssl" $openssl_dir
check_or_exit "source Opus directory using --opus" $opus_dir
check_or_exit "config site using --config-site" $config_site

export MIN_IOS_VERSION=$min_ios_version
BUILD_ARCHS=(
    "arm64"
    "arm64e"
    "x86_64"
)
PJSIP_LIB_PATHS=(
    "pjlib/lib"
    "pjlib-util/lib"
    "pjmedia/lib"
    "pjnath/lib"
    "pjsip/lib"
    "third_party/lib"
)
BUILD_DIR="$PWD/tmp"

function build_dependency() {
    if [ -z $DEV_MODE ] || [ ! -d "$1/build" ]; then
        echo "Building $1"
        cd $2
        sh "$1.sh"
        echo "$1 build completed."
    fi
}

function build_archs () {
    echo "Building ABIs"

    cp $config_site "$source_dir/pjlib/include/pj"
    mkdir -p $BUILD_DIR/archs

    for arch in "${BUILD_ARCHS[@]}"; do
        configure_"$arch"
        _build $arch
        _collect $arch
    done

    echo "Done building the ABIs"
    echo "============================="
}

function configure_arm64 () {
    echo "Configure for arm64"
    export CFLAGS="-miphoneos-version-min=$min_ios_version"
    export LDFLAGS=
}

function configure_arm64e () {
    echo "Configure for arm64e"
    export CFLAGS="-miphoneos-version-min=$min_ios_version"
    export LDFLAGS=
}

function configure_x86_64 () {
    echo "Configure for x86_64"
    export CFLAGS="-O2 -m32 -mios-simulator-version-min=$min_ios_version"
    export LDFLAGS="-O2 -m32 -mios-simulator-version-min=$min_ios_version"
}

function _build () {
    pushd . > /dev/null
    cd $source_dir

    arch=$1
    clean_pjsip_libs $arch

    configure="./configure-iphone"
    configure="$configure --with-ssl=$openssl_dir/build"
    configure="$configure --with-opus=$opus_dir/build"
    configure="$configure --with-bcg729=$bcg729_dir/build"
    configure="$configure --disable-libyuv --disable-speex --disable-video"
    
    export CFLAGS="${CFLAGS} -I$openssl_dir/build/include"
    export LDFLAGS="${LDFLAGS} -L$openssl_dir/build/lib -lstdc++"
    export C_INCLUDE_PATH="${C_INCLUDE_PATH} "
    export DEVPATH=`xcrun -sdk iphonesimulator --show-sdk-platform-path`/Developer
    export CC=gcc
    export MIN_IOS="-miphoneos-version-min=$min_ios_version"

    echo "Building for $arch"
    echo Run configure command: $configure

    ARCH="-arch ${arch}" $configure
    make dep
    make clean
    make

    echo "Done building for $arch"
    echo "============================="

    popd > /dev/null
}

function _collect () {
    echo "COLLECT for $1"
    destination_dir="$BUILD_DIR/archs/$1"
    mkdir -p $destination_dir

    name_prefix=$1

    if [ "$1" == "arm64e" ]; then
        name_prefix="arm64"
    fi

    for x in `find $source_dir -name *${name_prefix}*.a`; do
        cp -v $x $destination_dir
    done
}

function assemble_final_libs () {
    echo "Assemble pjsip libs ..."

    mkdir -p $output_dir
    pj_filename="$output_dir/libpjsip-apple-darwin-ios.a"

    # remove pjsua2
    a_excluded_files=`find $BUILD_DIR/$1 -name libpjsua2*.a -exec printf '%s ' {} +`
    rm -rf $a_excluded_files

    a_files=`find $BUILD_DIR/archs -name *darwin_ios.a -exec printf '%s ' {} +`

    libtool -o $pj_filename $a_files

    lipo -info $pj_filename

    echo "Copy openssl ..."
    cp -r $openssl_dir/build/lib/* $output_dir

    echo "Copy opus ..."
    cp -r $opus_dir/build/lib/* $output_dir

    echo "Copy G729 ..."
    cp -r $bcg729_dir/build/lib/*.a $output_dir
}

function clean_pjsip_libs () {
    arch=$1

    echo "Clean $arch lib directory"

    for src_dir in ${PJSIP_LIB_PATHS[*]}; do
        dir="$PJSIP_SRC_DIR/${src_dir}"
        if [ -d $dir ]; then
            rm -rf $dir
        fi

        dir="$PJSIP_SRC_DIR/${src_dir}-${arch}"
        if [ -d $dir ]; then
            rm -rf $dir
        fi
    done
}

function cleanup() {
    rm -rf $BUILD_DIR
}

build_dependency "openssl" $openssl_dir
build_dependency "opus" $opus_dir
build_dependency "bcg729" $bcg729_dir
build_archs
assemble_final_libs
cleanup
