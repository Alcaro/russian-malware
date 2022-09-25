#pragma once
#include "global.h"
#include "linqbase.h"

#include "simd.h"
#include "hash.h"
#include "array.h"
#include "string.h"

#include "endian.h"
#include "function.h"
#include "set.h"

#include "bytepipe.h"
#include "file.h"
#include "linq.h"
#include "os.h"
#include "random.h"
#include "stringconv.h"

#include "thread/thread.h" //no ifdef on this one, it contains some dummy implementations if threads are disabled
#include "bml.h"
#include "json.h"

#include "argparse.h"
#include "base64.h"
#include "bytestream.h"
#include "crc32.h"
#include "deflate.h"
#include "image.h"
#include "prioqueue.h"
#include "regex.h"
#include "runloop2.h"
#include "serialize.h"
#include "staticmap.h"
#include "terminal.h"
#include "test.h"
#include "time.h"
#include "zip.h"

#ifdef ARLIB_GUI
void arlib_init();
bool arlib_try_init();
//#include "gui/window.h"
#endif

#ifdef ARLIB_GAME
#include "game.h"
#endif

#ifdef ARLIB_OPENGL
#include "opengl/aropengl.h"
#endif

#ifdef ARLIB_WUTF
#include "wutf/wutf.h"
#endif

#ifdef ARLIB_SOCKET
#include "socket.h"
#include "http.h"
#include "socks5.h"
#include "websocket.h"
#endif
