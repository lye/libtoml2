#!/bin/sh -e

CC=${CC:-clang37}
OBJ_FILES=

if `which ccache > /dev/null 2>&1` ; then
	CC="ccache $CC"
fi

LIBS="
	-L/usr/local/lib
	-licuuc
	-lcxxrt
"

BUILD_FLAGS="
	-g
	-Wall -Werror -Wno-unused-function
	-fno-omit-frame-pointer
	-fsanitize=undefined
	-fno-sanitize=alignment
	-std=c99
	-isystem/usr/local/include
	-Iinc
"

mkdir -p bin
mkdir -p obj

rm -f obj/*.o

for C_FILE in src/*.c ; do
	OBJ_FILE=`echo $C_FILE | sed -e 's#\.c$#.o#' | sed -e 's#^src/#obj/#'`

	$CC \
		-c \
		-o $OBJ_FILE \
		$BUILD_FLAGS \
		$C_FILE

	OBJ_FILES="$OBJ_FILES $OBJ_FILE"
done

ar -rc bin/libtoml2.a $OBJ_FILES

$CC \
	$BUILD_FLAGS \
	$LIBS \
	$OBJ_FILES \
	-lcheck test/*.c \
	./bin/libtoml2.a \
	-o bin/libtoml2.test

env MALLOC_OPTIONS=J ./bin/libtoml2.test
