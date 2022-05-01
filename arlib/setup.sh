#!/bin/bash
[ -e Makefile ] && echo 'Already configured' && exit

[ -e arlib ] || ln -s $(readlink -f $(dirname $0)) .

program=$1
[[ -z $program ]] && program=$(basename $(pwd))

mkdir obj
echo Signature: 8a477f597d28d172789f06886806bc55 > obj/CACHEDIR.TAG

cat > arlib.h <<EOF
#pragma once
#if defined(ARLIB_OPENGL) && defined(ARLIB_OPT)
#define AROPENGL_SLIM
#endif

#include "arlib/arlib.h"

#ifdef AROPENGL_SLIM
#include "obj/glsym-slim.h"
#endif
EOF

cat > main.cpp <<EOF
#include "arlib.h"

int main(int argc, char** argv)
{
	puts("Hello world");
	return 0;
}
EOF

cat > .gitignore <<-EOF
	obj/
	$program
	$program.exe
EOF

cat > Makefile <<-EOF
	include arlib/Makefile-head
	
	PROGRAM = $program
	ARTYPE = exe
	ARGUI = 0
	ARGAME = 0
	AROPENGL = 0
	ARTHREAD = 0
	ARWUTF = 0
	ARSOCKET = 0
	ARSOCKET_SSL = openssl
	ARSOCKET_SSL_WINDOWS = schannel
	ARTERMINAL = 1
	
	include arlib/Makefile
EOF

#while Windows is supported, these batch files aren't relevant to my current workflow
#cat > z0_run.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	del $program.exe
#	if exist $program.exe goto h
#	mingw32-make -j4
#	$program.exe
#	goto h
#EOF
#
#cat > z1_job1.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	del $program.exe
#	if exist $program.exe goto h
#	mingw32-make -j1
#	$program.exe
#	goto h
#EOF
#
#cat > z6_run64.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	del ${program}64.exe
#	if exist ${program}64.exe goto h
#	mingw32-make CC=gcc64 CXX=g++64 LD=g++64 RCFLAGS="-Fpe-x86-64" OBJSUFFIX="-64" -j4 OUTNAME=${program}64.exe
#	${program}64.exe
#	goto h
#EOF
#
#cat > zc_clean.bat <<-EOF
#	del /q obj\*
#	del $program*.exe
#EOF
#
#cat > zd_debug.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	del $program.exe
#	if exist $program.exe goto h
#	mingw32-make -j4
#	if not exist $program.exe goto h
#	gdb $program.exe
#	goto h
#EOF
#
#cat > zk_keepgoing.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	del $program.exe
#	if exist $program.exe goto h
#	mingw32-make -j4 -k
#	$program.exe
#	goto h
#EOF
#
#cat > zm_makerelease.bat <<-EOF
#	rem del /q obj\*
#	del $program.exe
#	mingw32-make OBJSUFFIX="-opt" OPT=1 -j4 OUTNAME=$program.exe
#	
#	rem del $program64.exe
#	rem mingw32-make CC=gcc64 CXX=g++64 LD=g++64 RCFLAGS="-Fpe-x86-64" OBJSUFFIX="-opt64" OPT=1 -j4 OUTNAME=$program64.exe
#	pause
#EOF
#
#cat > zt_testsuite.bat <<-EOF
#	goto q
#	:h
#	pause
#	:q
#	cls
#	mingw32-make -j4 test
#	pause
#	goto h
#EOF

echo 'Done'
