// Copyright (C) 2005 TorrentZip Team (StatMat,shindakun,Ultrasubmarine,r3nh03k)
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <https://www.gnu.org/licenses/>.

#ifndef PLATFORM_DOT_H
#define PLATFORM_DOT_H

#if defined(__APPLE__) && defined(__MACH__)
#define MAC_OS_X
#ifndef PLATFORM_NAME
#define PLATFORM_NAME "Mac OS X"
#endif
#endif

#ifndef WIN32

/* Cygwin doesn't have cfmakeraw */
#if defined(__CYGWIN__)
void cfmakeraw(struct termios *);
#else
#include <termios.h>
#endif /* defined(__CYGWIN__) */

#define DIRSEP '/'

int getch(void);

#else

#define DIRSEP '\\'
#define stat _stati64
#define lstat stat
#define off64_t __int64
#define off_t off64_t
#define strcasecmp stricmp
#define strncasecmp strnicmp

typedef struct dir_s DIR;
#define dirent _finddatai64_t
#define d_name name

DIR *opendir(const char *name);
int closedir(DIR *dirp);
struct dirent *readdir(DIR *dirp);

int mkstemp(char *ntemplate);
#endif

#ifndef HAVE_FOPEN64
#ifndef fopen64
#define fopen64 fopen
#endif
#endif

#ifndef HAVE_FSEEKO64
#ifndef fseeko64
#ifdef HAVE_FSEEKO
#define fseeko64 fseeko
#else
#define fseeko64 fseek
#endif
#endif
#endif

#ifndef HAVE_FTELLO64
#ifndef ftello64
#ifdef HAVE_FTELLO
#define ftello64 ftello
#else
#define ftello64 ftell
#endif
#endif
#endif

#endif
