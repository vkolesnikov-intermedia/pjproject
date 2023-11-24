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
    --arch=*)
    arch="${i#*=}"
    shift
    ;;
    --sdk=*)
    sdk="${i#*=}"
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
check_or_exit "arch (x86_64, arm64) using --arch" $arch
check_or_exit "sdk (iphoneos, iphonesimulator) using --sdk" $sdk
check_or_exit "source BCG729 directory using --bcg729" $bcg729_dir
check_or_exit "source OpenSSL directory using --openssl" $openssl_dir
check_or_exit "source Opus directory using --opus" $opus_dir

export MIN_IOS_VERSION=$min_ios_version
PJSIP_LIB_PATHS=(
    "pjlib/lib"
    "pjlib-util/lib"
    "pjmedia/lib"
    "pjnath/lib"
    "pjsip/lib"
    "third_party/lib"
)

function build_dependency() {
    current_dir=$PWD
    echo "Building $1 for arch: $2 and sdk: $3"
    cd $4
    sh "./$1.sh" $2 $3
    cp -r $4/build/lib/* $output_dir
    echo "$1 build completed."
    cd $current_dir
}

function build_arch () {
    echo "Building ABI for $arch $sdk"

    _build
    _collect

    echo "Done building the ABIs"
    echo "============================="
}

function _build () {
    pushd . > /dev/null
    cd $source_dir

    clean_pjsip_libs $arch

    configure="./configure-iphone"
    configure="$configure --with-ssl=$openssl_dir/build"
    configure="$configure --with-opus=$opus_dir/build"
    configure="$configure --with-bcg729=$bcg729_dir/build"
    configure="$configure --disable-libyuv --disable-speex --disable-video"
    
    export CFLAGS="${CFLAGS} -I$openssl_dir/build/include"
    export LDFLAGS="${LDFLAGS} -L$openssl_dir/build/lib -lstdc++"
    export C_INCLUDE_PATH="${C_INCLUDE_PATH} "
    export DEVPATH=`xcrun -sdk $sdk --show-sdk-platform-path`/Developer
    export CC=gcc

    if [ "$sdk" == "iphonesimulator" ]; then
        echo dev path $DEVPATH
        export CFLAGS="${CFLAGS} -O2 -m64 -mios-simulator-version-min=$min_ios_version"
        export LDFLAGS="${LDFLAGS} -O2 -m64 -mios-simulator-version-min=$min_ios_version"
        export MIN_IOS="-mios-simulator-version-min=$min_ios_version"
    else
        export CFLAGS="${CFLAGS} -mios-version-min=$min_ios_version"
        export MIN_IOS="-mios-version-min=$min_ios_version"
    fi

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
    echo "COLLECT for arch: $arch sdk: $sdk"
    destination_dir="$output_dir/pjlib"  

    mkdir -p $destination_dir

    for x in `find $source_dir -name *${arch}*.a`; do
        cp -v $x $destination_dir
    done
}

function clean_pjsip_libs () {
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

function assemble_final_libs () {
    echo "Assemble pjsip lib ..."

    pj_filename="$output_dir/libpjsip-apple-darwin-ios.a"
    pj_lib=$output_dir/pjlib

    # remove pjsua2
    a_excluded_files=`find $pj_lib -name libpjsua2*.a -exec printf '%s ' {} +`
    echo Remove exluded files $a_excluded_files
    rm -rf $a_excluded_files

    a_files=`find $pj_lib -name *darwin_ios.a -exec printf '%s ' {} +`
    libtool -o $pj_filename $a_files

    rm -rf $pj_lib
}

build_dependency "bcg729" $arch $sdk $bcg729_dir
build_dependency "openssl" $arch $sdk $openssl_dir
build_dependency "opus" $arch $sdk $opus_dir
build_arch
assemble_final_libs