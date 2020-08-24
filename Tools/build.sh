#! /bin/sh

# Assumes the root of the project is the current directory

SDK=$1
DEST="$2"

if [ -z "$DEST" ] ; then
	echo "usage: build.sh SDK DEST"
	exit 1
fi

xcodebuild install -configuration Release -sdk $SDK DSTROOT="$DEST" || exit 1
