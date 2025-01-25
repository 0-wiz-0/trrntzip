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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define NDEBUG
#include <assert.h>

#include "global.h"
#include "util.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

// The canonical order is case insensitive, but we need a tie-breaker
// to avoid ambiguity
int CanonicalCmp(const char *s1, const char *s2) {
  int res = strcasecmp(s1, s2);
  return res ? res : strcmp(s1, s2);
}

int StringCompare(const void *str1, const void *str2) {
  const char *p1 = *(const char **)str1;
  const char *p2 = *(const char **)str2;
  return CanonicalCmp(p1, p2);
}

int BasenameCompare(const void *str1, const void *str2) {
  const char *p1 = *(const char **)str1;
  const char *p2 = *(const char **)str2;
  const char *b1 = strrchr(p1, '/');
  const char *b2 = strrchr(p2, '/');
  int res = CanonicalCmp(b1 ? b1 + 1 : p1, b2 ? b2 + 1 : p2);

  // Tie-breaker ensures deterministic output. (It isn't needed for correct
  // operation since names of added members must be unique.)
  if (!res && b1 && b2)
    res = CanonicalCmp(p1, p2);

  return res;
}

int EndsWithCaseInsensitive(const char *str, const char *tail) {
  int n1 = strlen(str), n2 = strlen(tail);

  if (n1 < n2)
    return 0;
  else
    return !strcasecmp(str + n1 - n2, tail);
}

// Create a dynamic string array
char **DynamicStringArrayCreate(int iElements) {
  int iCount;
  char **StringArray;

  StringArray = (char **)calloc(sizeof(char *), iElements);
  if (!StringArray)
    return NULL;

  for (iCount = 0; iCount < iElements; iCount++) {
    StringArray[iCount] = (char *)malloc(MAX_PATH + 1);

    // Check for error with above alloc
    // If there is, free up everything we managed to alloc
    // and return error
    if (!StringArray[iCount])
      return DynamicStringArrayDestroy(StringArray, iCount);
    StringArray[iCount][0] = 0;
  }
  return (StringArray);
}

// Destroy a dynamic string array
char **DynamicStringArrayDestroy(char **StringArray, int iElements) {
  int iCount;

  CHECK_DYNAMIC_STRING_ARRAY(StringArray, iElements);

  for (iCount = 0; iCount < iElements; iCount++) {
    free(StringArray[iCount]);
  }

  free(StringArray);

  return NULL;
}

// Resize a dynamic string array
// to have iNewElements in total.
// iNewElements may be larger or smaller than *piElements.
char **DynamicStringArrayResize(char **StringArray, int *piElements,
                                int iNewElements) {
  int iCount;
  char **TmpPtr = NULL;

  CHECK_DYNAMIC_STRING_ARRAY(StringArray, *piElements);

  if (iNewElements < 1) {
    iCount = *piElements;
    *piElements = 0;
    return DynamicStringArrayDestroy(StringArray, iCount);
  }

  for (iCount = iNewElements; iCount < *piElements; iCount++) {
    free(StringArray[iCount]);
  }
  TmpPtr = StringArray;
  iCount = iNewElements < *piElements ? iNewElements : *piElements;
  StringArray = (char **)realloc(StringArray, iNewElements * sizeof(char *));
  if (!StringArray) {
    *piElements = 0;
    return DynamicStringArrayDestroy(TmpPtr, iCount);
  }

  for (iCount = *piElements; iCount < iNewElements; iCount++) {
    StringArray[iCount] = (char *)malloc(MAX_PATH + 1);

    // Check for error with above alloc
    // If there is, free up everything we managed to alloc
    // and return error
    if (!StringArray[iCount]) {
      *piElements = 0;
      return DynamicStringArrayDestroy(StringArray, iCount);
    }
    StringArray[iCount][0] = 0;
  }
  *piElements = iNewElements;

  CHECK_DYNAMIC_STRING_ARRAY(StringArray, *piElements);

  return (StringArray);
}

// Grow a dynamic string array to support at least iMinElements entries.
// Balances number of reallocations and wasted space by using geometric
// growth for tiny expansion requests while obeying large increments
// exactly. Does nothing when the array is already large enough.
char **DynamicStringArrayGrow(char **FileNameArray, int *piElements,
                              int iMinElements) {
  int iNewElements;

  if (*piElements >= iMinElements && FileNameArray)
    return FileNameArray;
  else if (iMinElements - *piElements >= (*piElements + ARRAY_ELEMENTS) / 4)
    iNewElements = iMinElements;
  else // grow geometrically
    iNewElements = *piElements < 2             ? ARRAY_ELEMENTS
                   : *piElements < INT_MAX / 2 ? *piElements * 2
                   : *piElements < INT_MAX     ? INT_MAX
                                               : 0;

  return DynamicStringArrayResize(FileNameArray, piElements, iNewElements);
}

void DynamicStringArrayCheck(char **StringArray, int iElements) {
#ifndef NDEBUG
  // All StringArray elements should be non-null,
  // because they were all allocated by DynamicStringArrayCreate.
  int i, l;
  assert(StringArray || iElements == 0);
  for (i = 0; i < iElements; ++i) {
    assert(StringArray[i]);
    l = strlen(StringArray[i]);
    assert(l >= 0);
    assert(l <= MAX_PATH);
  }
#else
  (void)StringArray;
  (void)iElements;
#endif
}

char *get_cwd(void) {
  char *pszCWD = NULL;
  int cchCWD = 1024;

  pszCWD = malloc(cchCWD);

  if (!pszCWD)
    return NULL;

  while (!getcwd(pszCWD, cchCWD - 2)) {
    cchCWD += 1024;
    free(pszCWD);
    pszCWD = malloc(cchCWD);
    if (!pszCWD)
      return NULL;
  }

  cchCWD = strlen(pszCWD);

  if (pszCWD[cchCWD - 1] != DIRSEP) {
    pszCWD[cchCWD] = DIRSEP;
    pszCWD[cchCWD + 1] = 0;
  }

  return pszCWD;
}

// Replaces file dest with tmpfile.
// Returns NULL on success or an error message.
const char *UpdateFile(const char *dest, const char *tmpfile) {
#ifdef WIN32
  // On WIN32, rename() fails if the destination exists. So we have to remove
  // the destination first.
  if (remove(dest)) {
    if (remove(tmpfile))
      return "Unable to remove either destination or temporary file. "
             "Please replace the file manually.";
    else
      return "Unable to remove destination for replacement (temporary "
             "file removed).";
  }
  if (rename(tmpfile, dest))
    return "The original file has already been deleted, so you must rename "
           "this file manually.";

#else // !WIN32, assume POSIX semantics

  // Try to preserve permissions but ignore errors
  struct stat st;
  if (!stat(dest, &st))
    chmod(tmpfile, st.st_mode & ~S_IFMT);

  // On a POSIX system, rename() atomically replaces the destination if
  // it exists.
  if (rename(tmpfile, dest)) {
    if (remove(tmpfile))
      return "Also could not remove temporary file. "
             "Please replace the file manually.";
    else
      return strerror(errno);
  }
#endif

  return NULL; // success
}
