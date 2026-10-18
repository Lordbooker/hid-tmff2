#!/bin/sh

# installs the module via DKMS, run as root:
# sudo ./dkms/dkms-install.sh

if [ "$(id -u)" != 0 ]; then
	echo "You must run this script as root!"
	exit 1
fi

# work from the repository root, regardless of where we were called from
cd "$(dirname "$0")/.." || exit 1

TM_NAME=hid-tmff2
# take the version from dkms.conf so the two can't get out of sync
TM_VER=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms/dkms.conf)
if [ -z "${TM_VER}" ]; then
	echo "Could not read PACKAGE_VERSION from dkms/dkms.conf"
	exit 1
fi

DST="/usr/src/${TM_NAME}-${TM_VER}"

mkdir -p "${DST}" || exit 1
cp -r ./* "${DST}" || exit 1

# copy dkms.conf specifically to be in root dir or build area
cp dkms/dkms.conf "${DST}" || exit 1

dkms add -m "${TM_NAME}" -v "${TM_VER}" &&
dkms build -m "${TM_NAME}" -v "${TM_VER}" &&
dkms install -m "${TM_NAME}" -v "${TM_VER}"

exit $?
