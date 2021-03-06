#ifndef SETTINGS_H
#define SETTINGS_H


#include "types.h"

#include <string>

const _i32 BUFFERSIZE=1024;
const _i32 NBUFFERS=32;
const _i32 READSIZE=32768;
const _i32 SENDSIZE=16384;
const uchar FILESERV_VERSION=36;

#ifndef _WIN32
#define DISABLE_WINDOW_SIZE
#endif
//#define LOG_FILE
//#define LOG_CONSOLE
//#define LOG_OFF
#define LOG_SERVER
#define DLL_EXPORT
#define CHECK_IDENT
#define BACKGROUND_PRIORITY
#define BACKUP_SEM

#endif

