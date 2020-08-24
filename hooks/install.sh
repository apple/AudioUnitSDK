#! /bin/sh

git rev-parse 2> /dev/null
if [ $? != 0 ]; then
	echo "not currently working in a git repository, therefore aborting `basename $0`"
	exit 0
fi

set -e

SOURCE_DIR="`git rev-parse --show-toplevel`/hooks"
INSTALL_DIR=$(git rev-parse --git-path hooks)
FILENAME="pre-commit"

mkdir -p "${INSTALL_DIR}"
cp -f "${SOURCE_DIR}"/${FILENAME} "${INSTALL_DIR}"/${FILENAME}
chmod +x "${INSTALL_DIR}"/${FILENAME}
