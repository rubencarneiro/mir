#!/usr/bin/env bash

socket=${XDG_RUNTIME_DIR}/miral_socket
port=0
while [ -e "${XDG_RUNTIME_DIR}/wayland-${port}" ]; do
    let port+=1
done
wayland_display=wayland-${port}
miral_server=miral-shell
launcher=qterminal
bindir=$(dirname $0)
vt=4

if [ "$(lsb_release -c -s)" != "xenial" ]
then
  qt_qpa=wayland
  gdk_backend=wayland
  sdl_videodriver=wayland
  enable_mirclient=""
else
  qt_qpa=ubuntumirclient
  gdk_backend=mir
  sdl_videodriver=mir
  enable_mirclient="--enable-mirclient"
fi

while [ $# -gt 0 ]
do
  if [ "$1" == "--help" -o "$1" == "-h" ]
  then
    echo "$(basename $0) - Handy launch script for a miral \"desktop session\""
    echo "Usage: $(basename $0) [options] [shell options]"
    echo "Options are:"
    echo "    -kiosk                    use miral-kiosk instead of ${miral_server}"
    echo "    -launcher <launcher>      use <launcher> instead of '${launcher}'"
    echo "    -vt <termid>              set the virtual terminal [${vt}]"
    echo "    -socket <socket>          set the legacy mir socket [${socket}]"
    echo "    -wayland-display <socket> set the wayland socket [${wayland_display}]"
    echo "    -bindir <bindir>          path to the miral executable [${bindir}]"
    exit 0
  elif [ "$1" == "-kiosk" ];            then miral_server=miral-kiosk
  elif [ "$1" == "-launcher" ];         then shift; launcher=$1
  elif [ "$1" == "-vt" ];               then shift; vt=$1
  elif [ "$1" == "-socket" ];           then shift; socket=$1
  elif [ "$1" == "-wayland-display" ];  then shift; wayland_display=$1
  elif [ "$1" == "-bindir" ];           then shift; bindir=$1
  elif [ "${1:0:2}" == "--" ];          then break
  fi
  shift
done

if [ "${bindir}" != "" ]; then bindir="${bindir}/"; fi

if [ -e "${socket}" ]; then echo "Error: session endpoint '${socket}' already exists"; exit 1 ;fi
if [ -e "${XDG_RUNTIME_DIR}/${wayland_display}" ]; then echo "Error: wayland endpoint '${wayland_display}' already exists"; exit 1 ;fi

vt_login_session=$(who -u | grep tty${vt} | grep ${USER} | wc -l)
if [ "${vt_login_session}" == "0" ]; then echo "Error: please log into tty${vt} first"; exit 1 ;fi

oldvt=$(sudo fgconsole)
sudo --preserve-env sh -c "MIR_SERVER_FILE=${socket} WAYLAND_DISPLAY=${wayland_display} MIR_SERVER_CONSOLE_PROVIDER=vt MIR_SERVER_VT=${vt} MIR_SERVER_ARW_FILE=on ${bindir}${miral_server} ${enable_mirclient} $*&\
    while [ ! -e \"${XDG_RUNTIME_DIR}/${wayland_display}\" ]; do echo \"waiting for ${wayland_display}\"; sleep 1 ;done;\
    su -c \"MIR_SOCKET=${socket} XDG_SESSION_TYPE=mir GDK_BACKEND=${gdk_backend} QT_QPA_PLATFORM=${qt_qpa} SDL_VIDEODRIVER=${sdl_videodriver} WAYLAND_DISPLAY=${wayland_display} NO_AT_BRIDGE=1 dbus-run-session -- ${launcher}\" $USER;\
    (sudo killall -w ${bindir}${miral_server} || sudo killall -w ${bindir}${miral_server}.bin); chvt ${oldvt}"
