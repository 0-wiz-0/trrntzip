// Copyright (C) 2005 TorrentZip Team (StatMat,shindakun,Ultrasubmarine,r3nh03k)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"
#include "util.h"

static FILE *OpenLog(const char *szFileName);

// Global var to store if the logprint func is expecting more
// data to complete a line. This prevents the timestamp being
// multipally inserted before a line terminates in a logfile.
static char continueline = 0;

// Used to print to screen and file at the same time
void logprint(FILE *stdf, FILE *f, char *format, ...) {
  time_t now;
  struct tm *t;
  va_list arglist;
  char szTimeBuffer[2048 + 1];
  char szMessageBuffer[2048 + 1];

  // Only print the timestamp if this is the beginning of a line
  if (!continueline) {
    now = time(NULL);
    t = localtime(&now);

    snprintf(szTimeBuffer, sizeof(szTimeBuffer),
             "[%04d/%02d/%02d - %02d:%02d:%02d] ", t->tm_year + 1900,
             t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  } else {
    szTimeBuffer[0] = 0;
  }

  // Look for a newline in the passed data
  continueline = !strchr(format, '\n');

  va_start(arglist, format);
  vsnprintf(szMessageBuffer, sizeof(szMessageBuffer), format, arglist);
  va_end(arglist);

  // Print to stdout or stderr
  if (stdf) {
    fprintf(stdf, "%s", szMessageBuffer);
    fflush(stdf);
  }

  // Print to logfile
  if (f) {
    fprintf(f, "%s%s", szTimeBuffer, szMessageBuffer);
    fflush(f);
  }
}

// Used to print to screen and two files at the same time
void logprint3(FILE *stdf, FILE *f1, FILE *f2, char *format, ...) {
  time_t now;
  struct tm *t;
  va_list arglist;
  char szTimeBuffer[2048 + 1];
  char szMessageBuffer[2048 + 1];

  // Only print the timestamp if this is the beginning of a line
  if (!continueline) {
    now = time(NULL);
    t = localtime(&now);

    snprintf(szTimeBuffer, sizeof(szTimeBuffer),
             "[%04d/%02d/%02d - %02d:%02d:%02d] ", t->tm_year + 1900,
             t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  } else {
    szTimeBuffer[0] = 0;
  }

  // Look for a newline in the passed data
  continueline = !strchr(format, '\n');

  va_start(arglist, format);
  vsnprintf(szMessageBuffer, sizeof(szMessageBuffer), format, arglist);
  va_end(arglist);

  // Print to stdout or stderr
  if (stdf) {
    fprintf(stdf, "%s", szMessageBuffer);
  }

  // Print to logfile 1
  if (f1) {
    fprintf(f1, "%s%s", szTimeBuffer, szMessageBuffer);
    fflush(f1);
  }

  // Print to logfile 2
  if (f2) {
    fprintf(f2, "%s%s", szTimeBuffer, szMessageBuffer);
    fflush(f2);
  }
}

int OpenProcessLog(const char *pszWritePath, const char *pszRelPath,
                   MIGRATE *mig) {
  int iPathLen = 0;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char szLogname[MAX_PATH + 1];
  char szRelPathBuf[MAX_PATH + 1];
  char *pszDirname = NULL;

  if (pszRelPath) {
    iPathLen = strlen(pszRelPath);

    // Strip off trailing slashes
    while (iPathLen > 0 && pszRelPath[iPathLen - 1] == DIRSEP)
      iPathLen--;
#ifdef WIN32
    if (iPathLen > 0 && pszRelPath[iPathLen - 1] == ':')
      iPathLen--;
#endif
    snprintf(szRelPathBuf, sizeof(szRelPathBuf), "%.*s", iPathLen, pszRelPath);

    pszDirname = strrchr(szRelPathBuf, DIRSEP);
    if (pszDirname)
      pszDirname++;
    else
      pszDirname = szRelPathBuf;

    if (!*pszDirname)
      pszDirname = "-";
  } else {
    pszDirname = "-";
  }

  snprintf(szLogname, sizeof(szLogname),
           "%s[%s]_[%04d-%02d-%02d - %02d-%02d-%02d].log", pszWritePath,
           pszDirname, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
           t->tm_min, t->tm_sec);

  mig->fProcessLog = OpenLog(szLogname);

  if (mig->fProcessLog) {
    fprintf(mig->fProcessLog, "TorrentZip processing logfile for : \"%s\"\n",
            pszRelPath);
  }

  return mig->fProcessLog ? TZ_OK : TZ_CRITICAL;
}

int SetupErrorLog(WORKSPACE *ws, char qGUILaunch) {
  struct stat istat;
  int rc;

  if (!ws->pszErrorLogFile) {
    static const char szErrorLogName[] = "error.log";
    static const char sep[2] = {DIRSEP, 0};
    size_t dir_len = strlen(ws->pszStartPath);
    int has_sep = !dir_len || ws->pszStartPath[dir_len - 1] == DIRSEP;
    size_t sz = dir_len + 1 - has_sep + sizeof(szErrorLogName);

    if (!(ws->pszErrorLogFile = malloc(sz))) {
      fprintf(stderr,"Error allocating memory!\n");
      return TZ_CRITICAL;
    }
    snprintf(ws->pszErrorLogFile, sz, "%s%s%s", ws->pszStartPath,
             sep + has_sep, szErrorLogName);
  }

  rc = stat(ws->pszErrorLogFile, &istat);

  if (!rc && istat.st_size && !qGUILaunch) {
    fprintf(stderr,
            "There is a previous \"%s\".\n"
            "Are you sure you have dealt with the problems encountered\n"
            "last time this program was run?\n"
            "(Press 'y' to continue or any other key to exit.)\n",
            ws->pszErrorLogFile);

    if (tolower(getch()) != 'y') {
      fprintf(stderr, "Exiting.\n");
      return TZ_CRITICAL;
    }
  }

  return TZ_OK;
}

FILE *ErrorLog(WORKSPACE *ws) {
  if (!ws->fErrorLog && ws->pszErrorLogFile) {
    ws->fErrorLog = OpenLog(ws->pszErrorLogFile);
    if (!ws->fErrorLog) {
      // Don't retry on failure
      free(ws->pszErrorLogFile);
      ws->pszErrorLogFile = NULL;
    }
  }

  return ws->fErrorLog;
}

static FILE *OpenLog(const char *szFileName) {
  FILE *f = fopen(szFileName, "a");

  if (!f) {
    fprintf(stderr, "Could not open log file '%s'!\n", szFileName);
  }

  return f;
}
