// Copyright (C) 2005 - 2024 TorrentZip Team (StatMat, shindakun,
// Ultrasubmarine, r3nh03k, goosecreature, gordonj, 0-wiz-0, A.Miller)
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

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define NDEBUG
#include <assert.h>

#include "unzip.h"
#include "zip.h"

#include "global.h"
#include "logging.h"
#include "util.h"

#ifndef TZ_VERSION
#error "Build system must define TZ_VERSION"
#endif

#define ENDHEADERMAGIC (0x06054b50)
#define COMMENT_LENGTH 22 // strlen("TORRENTZIPPED-XXXXXXXX")
#define DIVIDER "--------------------------------------------------"
#define TMP_FILENAME "trrntzip-XXXXXX"

// CheckZipStatus (and related) return codes
#define STATUS_FORCE_REZIP -7   // Has proper comment, but rezip is forced
#define STATUS_WRONG_ORDER -6   // Entries aren't in canonical order
#define STATUS_CONTAINS_DIRS -5 // Zip has redundant DIR entries or subdirs
#define STATUS_ALLOC_ERROR -4   // Couldn't allocate memory.
#define STATUS_ERROR -3         // Corrupted zipfile or file is not a zipfile.
#define STATUS_BAD_COMMENT                                                     \
  -2 // No comment or comment is not in the proper format.
#define STATUS_OUT_OF_DATE                                                     \
  -1                // Has proper comment, but zipfile has been changed.
#define STATUS_OK 0 // File is A-Okay.

WORKSPACE *AllocateWorkspace(void);
void FreeWorkspace(WORKSPACE *ws);
static int GetFileList(unzFile UnZipHandle, WORKSPACE *ws);
int CheckZipStatus(unz64_s *UnzipStream, WORKSPACE *ws);
int ShouldFileBeRemoved(int iArray, WORKSPACE *ws);
int ZipHasDirEntry(WORKSPACE *ws);
static int ZipHasSubdirs(WORKSPACE *ws);
static int ZipHasWrongOrder(WORKSPACE *ws);
int MigrateZip(const char *zip_path, const char *pDir, WORKSPACE *ws,
               MIGRATE *mig);
static char **GetDirFileList(DIR *dirp, int *piElements);
static int RecursiveMigrate(const char *pszRelPath, const struct stat *pstat,
                            WORKSPACE *ws, MIGRATE *mig);
int RecursiveMigrateDir(const char *pszRelPath, WORKSPACE *ws);
int RecursiveMigrateTop(const char *pszRelPath, WORKSPACE *ws);
void DisplayMigrateSummary(WORKSPACE *ws, MIGRATE *mig);

// The created zip file global comment used to identify files
// This will be appended with the CRC32 of the central directory
static const char *gszApp = {"TORRENTZIPPED-"};

// The global flags that can be set with commandline parms.
// Setup here so as to avoid having to pass them to a lot of functions.
char qForceReZip = 0;
char qGUILaunch = 0;
char qNoRecursion = 0;
char qQuietMode = 0;
char qStripSubdirs = 0;

// Global flag to determine if any zipfile errors were detected
char qErrors = 0;

WORKSPACE *AllocateWorkspace(void) {
  WORKSPACE *ws = calloc(1, sizeof(WORKSPACE));

  if (ws == NULL)
    return NULL;

  // Allocate buffer for status checking and unpacking files into.
  ws->iBufSize = 64 * 1024;
  ws->pszDataBuf = malloc(ws->iBufSize);

  if (ws->pszDataBuf == NULL) {
    free(ws);
    return NULL;
  }

  // Allocate DynamicStringArray to hold filenames of zipped files.
  ws->iElements = ARRAY_ELEMENTS;
  ws->FileNameArray = DynamicStringArrayCreate(ws->iElements);

  if (!ws->FileNameArray) {
    free(ws->pszDataBuf);
    free(ws);
    return NULL;
  }

  // Set up the dates just like MAMEZip
  // 1996 12 24 23:32 GMT+1 (MAME's first release date)
  ws->zi.tmz_date.tm_sec = 0;
  ws->zi.tmz_date.tm_min = 32;
  ws->zi.tmz_date.tm_hour = 23;
  ws->zi.tmz_date.tm_mday = 24;
  ws->zi.tmz_date.tm_mon = 11;
  ws->zi.tmz_date.tm_year = 1996;

  // Do not set file type (ASCII, BINARY)
  ws->zi.internal_fa = 0;
  // Do not use any RASH (Read only, Archive, System, Hidden) values
  ws->zi.external_fa = 0;
  ws->zi.dosDate = 0;

  return ws;
}

void FreeWorkspace(WORKSPACE *ws) {
  if (ws->fErrorLog)
    fclose(ws->fErrorLog);

  if (ws->FileNameArray)
    DynamicStringArrayDestroy(ws->FileNameArray, ws->iElements);
  free(ws->pszDataBuf);
  free(ws->pszLogDir);
  free(ws->pszErrorLogFile);
  free(ws);
}

// Stores file list from the zip file in original order in
// ws->FileNameArray (the old contents will be overwritten).
static int GetFileList(unzFile UnZipHandle, WORKSPACE *ws) {
  int rc = UNZ_END_OF_LIST_OF_FILE;
  size_t iCount;
  unz_global_info64 GlobalInfo;

  if (unzGetGlobalInfo64(UnZipHandle, &GlobalInfo) != UNZ_OK)
    return TZ_ERR;

  if (!(ws->FileNameArray =
        DynamicStringArrayGrow(ws->FileNameArray, &ws->iElements,
                               GlobalInfo.number_entry + 1)))
    return TZ_CRITICAL;

  if (GlobalInfo.number_entry != 0)
    rc = unzGoToFirstFile(UnZipHandle);

  for (iCount = 0;
       rc == UNZ_OK && iCount < GlobalInfo.number_entry;
       iCount++, rc = unzGoToNextFile(UnZipHandle)) {
    unz_file_info64 ZipInfo;

    rc = unzGetCurrentFileInfo64(UnZipHandle, &ZipInfo,
                                 ws->FileNameArray[iCount], MAX_PATH,
                                 NULL, 0, NULL, 0);
    if (rc != UNZ_OK || ZipInfo.size_filename >= MAX_PATH ||
        ZipInfo.size_filename == 0)
      break;
  }
  ws->FileNameArray[iCount][0] = 0;

  return rc == UNZ_END_OF_LIST_OF_FILE && iCount == GlobalInfo.number_entry
         ? TZ_OK : TZ_ERR;
}

int CheckZipStatus(unz64_s *UnzipStream, WORKSPACE *ws) {
  unsigned long checksum, target_checksum = 0;
  off_t ch_length = UnzipStream->size_central_dir;
  off_t ch_offset = UnzipStream->central_pos - UnzipStream->size_central_dir;
  char comment_buffer[COMMENT_LENGTH + 1];
  char *ep = NULL;
  FILE *f = (FILE *)UnzipStream->filestream;

  // Quick check that the file at least appears to be a zip file.
  rewind(f);
  if (fgetc(f) != 'P' || fgetc(f) != 'K')
    return STATUS_ERROR;

  // Assume a TZ style archive comment and read it in. This is located at the
  // very end of the file.
  comment_buffer[COMMENT_LENGTH] = 0;
  if (fseeko64(f, -COMMENT_LENGTH, SEEK_END))
    return STATUS_ERROR;

  if (fread(comment_buffer, 1, COMMENT_LENGTH, f) != COMMENT_LENGTH)
    return STATUS_ERROR;

  // Check static portion of comment.
  if (strncmp(gszApp, comment_buffer, COMMENT_LENGTH - 8))
    return STATUS_BAD_COMMENT;

  // Parse checksum portion of the comment.
  errno = 0;
  target_checksum = strtoul(comment_buffer + COMMENT_LENGTH - 8, &ep, 16);
  // Check to see if stroul was able to parse the entire checksum.
  if (errno || ep != comment_buffer + COMMENT_LENGTH)
    return STATUS_BAD_COMMENT;

  // Comment checks out so skip to start of the central header.
  if (fseeko64(f, ch_offset, SEEK_SET))
    return STATUS_ERROR;

  // Read it in and calculate the crc32.
  checksum = crc32(0L, NULL, 0);
  while (ch_length > 0) {
    size_t read_length = ws->iBufSize < ch_length ? ws->iBufSize : ch_length;
    if (fread(ws->pszDataBuf, 1, read_length, f) != read_length)
      return STATUS_ERROR;

    checksum = crc32(checksum, ws->pszDataBuf, read_length);
    ch_length -= read_length;
  }

  return checksum == target_checksum ? STATUS_OK : STATUS_OUT_OF_DATE;
}

// check if the zip file entry is a directory that should be removed
// directory should not be removed if it is an empty directory
int ShouldFileBeRemoved(int iArray, WORKSPACE *ws) {
  int len;
  const char *entry = ws->FileNameArray[iArray];
  const char *slash = strrchr(entry, '/');

  if (!slash || slash[1]) // not a directory
    return 0;

  len = slash - entry + 1;
  // Although the list is sorted, checking the next entry isn't sufficient.
  // Entries with different case can appear between the directory and the
  // files inside (e.g. A/, a/, A/x, a/y).
  do {
    if (!strncmp(entry, ws->FileNameArray[++iArray], len))
      return 1; // can be removed
  } while (!strncasecmp(entry, ws->FileNameArray[iArray], len));

  return 0;
}

// find if the zipfiles contains any dir entries that should be removed
int ZipHasDirEntry(WORKSPACE *ws) {
  int iArray = 0;
  for (iArray = 0; strlen(ws->FileNameArray[iArray]); iArray++) {
    if (ShouldFileBeRemoved(iArray, ws))
      return 1;
  }
  return 0;
}

static int ZipHasSubdirs(WORKSPACE *ws) {
  int iArray;
  for (iArray = 0; *ws->FileNameArray[iArray]; iArray++)
    if (strchr(ws->FileNameArray[iArray], '/'))
      return 1;
  return 0;
}

// detect zipfiles that aren't in canonical order
// older trrntzip didn't always sort properly
static int ZipHasWrongOrder(WORKSPACE *ws) {
  int iArray;
  if (*ws->FileNameArray[0])
    for (iArray = 1; *ws->FileNameArray[iArray]; iArray++)
      if (CanonicalCmp(ws->FileNameArray[iArray - 1],
                       ws->FileNameArray[iArray]) >= 0)
        return 1;
  return 0;
}

int MigrateZip(const char *zip_path, const char *pDir, WORKSPACE *ws,
               MIGRATE *mig) {
  unz_file_info64 ZipInfo;
  unzFile UnZipHandle = NULL;
  unz64_s *UnzipStream = NULL;
  zipFile ZipHandle = NULL;
  int zip64 = 0;
  int tmpfd;

  // Used for CRC32 calc of central directory during rezipping
  zip64_internal *zintinfo;
  linkedlist_datablock_internal *ldi;

  // Used for our dynamic filename array
  int iArray = 0;

  int rc = 0;
  int error = 0;

  char szTmpBuf[MAX_PATH + 1];
  char szFileName[MAX_PATH + 1];
  char szZipFileName[MAX_PATH + 1];
  char szTmpZipFileName[MAX_PATH + 1];
  char *pszZipName = NULL;

  int iBytesRead = 0;

  off_t cTotalBytesInZip = 0;
  unsigned int cTotalFilesInZip = 0;

  // Use to store the CRC32 of the central directory
  unsigned long crc = 0;

  if (strcmp(pDir, ".") == 0) {
    snprintf(szTmpZipFileName, sizeof(szTmpZipFileName), "%s", TMP_FILENAME);
    snprintf(szZipFileName, sizeof(szZipFileName), "%s", zip_path);
  } else {
    snprintf(szTmpZipFileName, sizeof(szTmpZipFileName), "%s%c%s", pDir, DIRSEP,
             TMP_FILENAME);
    snprintf(szZipFileName, sizeof(szZipFileName), "%s%c%s", pDir, DIRSEP,
             zip_path);
  }

  if (access(szZipFileName, R_OK | W_OK)) {
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              "Error opening \"%s\". %s.\n", szZipFileName, strerror(errno));
    return TZ_ERR;
  }

  if ((UnZipHandle = unzOpen64(szZipFileName)) == NULL) {
    logprint3(
        stderr, mig->fProcessLog, ErrorLog(ws),
        "Error opening \"%s\", zip format problem. Unable to process zip.\n",
        szZipFileName);
    return TZ_ERR;
  }

  UnzipStream = (unz64_s *)UnZipHandle;

  // Check if zip is non-TZ or altered-TZ
  rc = CheckZipStatus(UnzipStream, ws);

  switch (rc) {
  case STATUS_ERROR:
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              "Unable to process \"%s\". It seems to be corrupt.\n",
              szZipFileName);
    unzClose(UnZipHandle);
    return TZ_ERR;

  case STATUS_ALLOC_ERROR:
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              "Error allocating memory!\n");
    unzClose(UnZipHandle);
    return TZ_CRITICAL;

  case STATUS_OK:
  case STATUS_OUT_OF_DATE:
  case STATUS_BAD_COMMENT:
    // Continue to Re-zip this zip.
    break;

  default:
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              "Bad return on CheckZipStatus!\n");
    unzClose(UnZipHandle);
    return TZ_CRITICAL;
  }

  CHECK_DYNAMIC_STRING_ARRAY(ws->FileNameArray, ws->iElements);
  // Get the filelist from the zip file in original order in ws->FileNameArray
  rc = GetFileList(UnZipHandle, ws);
  if (rc != TZ_OK) {
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              rc == TZ_CRITICAL ? "Error allocating memory!\n" :
              "Could not list contents of \"%s\". File is corrupted or "
              "contains entries with bad names.\n", szZipFileName);
    unzClose(UnZipHandle);
    return rc;
  }
  CHECK_DYNAMIC_STRING_ARRAY(ws->FileNameArray, ws->iElements);

  // GetFileList couldn't allocate enough memory to store the filelist
  if (!ws->FileNameArray) {
    logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
              "Error allocating memory!\n");
    unzClose(UnZipHandle);
    return TZ_CRITICAL;
  }

  if (rc == STATUS_OK && qForceReZip)
    rc = STATUS_FORCE_REZIP;

  if (rc == STATUS_OK && ZipHasWrongOrder(ws))
    rc = STATUS_WRONG_ORDER;

  // Sort filelist into canonical order
  for (iArray = 0; iArray < ws->iElements && ws->FileNameArray[iArray][0];
       iArray++)
    ;
  qsort(ws->FileNameArray, iArray, sizeof(char *),
        qStripSubdirs ? BasenameCompare : StringCompare);

  // Check if the zip has redundant directories
  if (rc == STATUS_OK && qStripSubdirs ? ZipHasSubdirs(ws) : ZipHasDirEntry(ws))
    rc = STATUS_CONTAINS_DIRS;

  // All checks passed, zip is up to date - skip it!
  if (rc == STATUS_OK) {
    if (!qQuietMode) {
      logprint(stdout, mig->fProcessLog,
               "Skipping, already TorrentZipped - %s\n", szZipFileName);
    }
    unzClose(UnZipHandle);
    return TZ_SKIPPED;
  }

  // ReZip it!
  logprint(stdout, mig->fProcessLog, "Rezipping - %s\n", szZipFileName);
  logprint(stdout, mig->fProcessLog, "%s\n", DIVIDER);

  tmpfd = mkstemp(szTmpZipFileName);
  if (tmpfd < 0) {
    logprint3(
        stderr, mig->fProcessLog, ErrorLog(ws),
        "!!!! Couldn't create a unique temporary file. %s. !!!!\n",
        strerror(errno));
    unzClose(UnZipHandle);
    return TZ_CRITICAL;
  }
  // Close the file and let zipOpen64() reopen it. It can't be accidentally
  // claimed by a different process since it already exists on disk. If an
  // attacker is able to replace it, we've lost anyway.
  close(tmpfd);

  if ((ZipHandle = zipOpen64(szTmpZipFileName, 0)) == NULL) {
    logprint3(
        stderr, mig->fProcessLog, ErrorLog(ws),
        "Error opening temporary zip file %s. Unable to process \"%s\"\n",
        szTmpZipFileName, szZipFileName);
    unzClose(UnZipHandle);
    remove(szTmpZipFileName);
    return TZ_ERR;
  }

  for (iArray = 0; iArray < ws->iElements && ws->FileNameArray[iArray][0];
       iArray++) {
    strcpy(szFileName, ws->FileNameArray[iArray]);
    rc = unzLocateFile(UnZipHandle, szFileName, 0);
    zip64 = 0;

    if (rc == UNZ_OK) {
      rc = unzGetCurrentFileInfo64(UnZipHandle, &ZipInfo, szFileName, MAX_PATH,
                                   NULL, 0, NULL, 0);
      if (rc == UNZ_OK)
        rc = unzOpenCurrentFile(UnZipHandle);
    }

    if (rc != UNZ_OK) {
      logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                "Unable to open \"%s\" from \"%s\"\n", szFileName,
                szZipFileName);
      error = 1;
      break;
    }

    // files >= 4G need to be zip64
    if (ZipInfo.uncompressed_size >= 0xFFFFFFFF)
      zip64 = 1;

    if (qStripSubdirs) {
      // To strip off path if there is one
      pszZipName = strrchr(szFileName, '/');

      if (pszZipName) {
        if (!*++pszZipName) {
          // Last char was '/' so is dir entry. Skip it.
          logprint(stdout, mig->fProcessLog, "Directory %s Removed\n",
                   szFileName);
          continue;
        }

        strcpy(ws->FileNameArray[iArray], pszZipName);
      } else
        pszZipName = szFileName;
    } else {
      pszZipName = szFileName;

      // check if the file is a DIR entry that should be removed
      if (ShouldFileBeRemoved(iArray, ws)) {
        // remove this file.
        logprint(stdout, mig->fProcessLog, "Directory %s Removed\n",
                 szFileName);
        continue;
      }
    }

    // Check for duplicate files (but allow files differing only in case)
    if (iArray > 0 && !strcmp(pszZipName, ws->FileNameArray[iArray - 1])) {
      logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                "Zip file \"%s\" contains more than one file named \"%s\"\n",
                szZipFileName, pszZipName);
      error = 1;
      break;
    }

    logprint(stdout, mig->fProcessLog,
             "Adding - %s (%" PRIu64 " bytes%s%s%s)...", pszZipName,
             ZipInfo.uncompressed_size, (zip64 ? ", Zip64" : ""),
             (pszZipName == szFileName ? "" : ", was: "),
             (pszZipName == szFileName ? "" : szFileName));

    rc = zipOpenNewFileInZip64(ZipHandle, pszZipName, &ws->zi, NULL, 0, NULL, 0,
                               NULL, Z_DEFLATED, Z_BEST_COMPRESSION, zip64);

    if (rc != ZIP_OK) {
      logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                "Unable to open \"%s\" in replacement zip \"%s\"\n", pszZipName,
                szTmpZipFileName);
      error = 1;
      break;
    }

    for (;;) {
      iBytesRead =
          unzReadCurrentFile(UnZipHandle, ws->pszDataBuf, ws->iBufSize);

      if (!iBytesRead) { // All bytes have been read.
        break;
      }

      if (iBytesRead < 0) // Error.
      {
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "Error while reading \"%s\" from \"%s\"\n", szFileName,
                  szZipFileName);
        error = 1;
        break;
      }

      rc = zipWriteInFileInZip(ZipHandle, ws->pszDataBuf, iBytesRead);

      if (rc != ZIP_OK) {
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "Error while adding \"%s\" to replacement zip \"%s\"\n",
                  pszZipName, szTmpZipFileName);
        error = 1;
        break;
      }

      cTotalBytesInZip += iBytesRead;
    }

    if (error)
      break;

    rc = unzCloseCurrentFile(UnZipHandle);

    if (rc != UNZ_OK) {
      if (rc == UNZ_CRCERROR)
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "CRC error in \"%s\" in \"%s\"!\n", szFileName,
                  szZipFileName);
      else
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "Error while closing \"%s\" in \"%s\"!\n", szFileName,
                  szZipFileName);

      error = 1;
      break;
    }

    rc = zipCloseFileInZip(ZipHandle);

    if (rc != ZIP_OK) {
      logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                "Error closing \"%s\" in new zip file \"%s\"!\n", pszZipName,
                szTmpZipFileName);
      error = 1;
      break;
    }

    logprint(stdout, mig->fProcessLog, "Done\n");

    cTotalFilesInZip++;
  }

  // If there was an error above then clean up and return.
  if (error) {
    fprintf(mig->fProcessLog, "Not done\n");
    unzClose(UnZipHandle);
    zipClose(ZipHandle, NULL, zip64);
    remove(szTmpZipFileName);
    return TZ_ERR;
  }

  logprint(stdout, mig->fProcessLog, "%s\n", DIVIDER);

  unzClose(UnZipHandle);

  // Before we close the file, we need to calc the CRC32 of
  // the central directory (for detecting a changed TZ file later)
  zintinfo = (zip64_internal *)ZipHandle;
  crc = crc32(0L, Z_NULL, 0);
  ldi = zintinfo->central_dir.first_block;
  while (ldi != NULL) {
    crc = crc32(crc, ldi->data, ldi->filled_in_this_block);
    ldi = ldi->next_datablock;
  }

  // Set the global file comment, so that we know to skip this file in future
  snprintf(szTmpBuf, sizeof(szTmpBuf), "%s%08lX", gszApp, crc);

  rc = zipClose(ZipHandle, szTmpBuf, zip64);

  if (rc == UNZ_OK) {
    const char *pErr = UpdateFile(szZipFileName, szTmpZipFileName);
    if (pErr) {
      logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                "!!!! Could not rename temporary file \"%s\" to \"%s\". %s\n",
                szTmpZipFileName, szZipFileName, pErr);
      return TZ_CRITICAL;
    }
  } else {
    logprint3(
        stderr, mig->fProcessLog, ErrorLog(ws),
        "Unable to close temporary zip file \"%s\" - cannot process \"%s\"!\n",
        szTmpZipFileName, szZipFileName);
    remove(szTmpZipFileName);
    return TZ_ERR;
  }

  logprint(stdout, mig->fProcessLog,
           "Rezipped %u compressed file%s totaling %" PRIu64 " bytes.\n",
           cTotalFilesInZip, cTotalFilesInZip != 1 ? "s" : "",
           cTotalBytesInZip);

  return TZ_OK;
}

// Get the filelist from the open dirp directory in canonical order
// Returns a sorted array
static char **GetDirFileList(DIR *dirp, int *piElements) {
  int iCount = 0;
  struct dirent *direntp = NULL;
  char **FileNameArray = 0;

  *piElements = ARRAY_ELEMENTS;
  FileNameArray = DynamicStringArrayCreate(*piElements);
  if (!FileNameArray)
    return NULL;

  while ((direntp = readdir(dirp))) {
    if (!(FileNameArray =
          DynamicStringArrayGrow(FileNameArray, piElements, iCount + 1)))
        return NULL;

    snprintf(FileNameArray[iCount], MAX_PATH + 1, "%s", direntp->d_name);
    iCount++;
  }

  FileNameArray[iCount][0] = 0;

  // Sort the dynamic array into canonical order
  qsort(FileNameArray, iCount, sizeof(char *), StringCompare);

  return (FileNameArray);
}

// Function to convert a dir or zip
static int RecursiveMigrate(const char *pszRelPath, const struct stat *pstat,
                            WORKSPACE *ws, MIGRATE *mig) {
  int rc = 0;

  char szRelPathBuf[MAX_PATH + 1];
  const char *pszFileName = NULL;

  pszFileName = strrchr(pszRelPath, DIRSEP);
  if (pszFileName) {
    memcpy(szRelPathBuf, pszRelPath, pszFileName - pszRelPath);
    szRelPathBuf[pszFileName - pszRelPath] = 0;
    pszFileName++;
  } else {
    snprintf(szRelPathBuf, sizeof(szRelPathBuf), ".");
    pszFileName = pszRelPath;
  }

  if (S_ISDIR(pstat->st_mode)) {
    // Get our execution time (in seconds)
    // for the conversion process so far
    mig->ExecTime += difftime(time(NULL), mig->StartTime);

    rc = RecursiveMigrateDir(pszRelPath, ws);

    // Restart the timing for this instance of RecursiveMigrate()
    mig->StartTime = time(NULL);
  } else { // if (S_ISREG(pstat->st_mode))? Users get what they ask for.
    mig->cEncounteredZips++;

    if (!mig->fProcessLog) {
      if (strcmp(szRelPathBuf, ".") == 0)
        rc = OpenProcessLog(ws->pszLogDir, pszFileName, mig);
      else
        rc = OpenProcessLog(ws->pszLogDir, szRelPathBuf, mig);

      if (rc != TZ_OK)
        return TZ_CRITICAL;
    }

    // minimum size of an empty zip file is 22 bytes, non-empty 98 bytes
    if (pstat->st_size >= 22) {
      rc = MigrateZip(pszFileName, szRelPathBuf, ws, mig);

      switch (rc) {
      case TZ_OK:
        mig->cRezippedZips++;
        break;
      case TZ_ERR:
        mig->cErrorZips++;
        mig->bErrorEncountered = 1;
        break;
      case TZ_CRITICAL:
        break;
      case TZ_SKIPPED:
        mig->cOkayZips++;
      }
    } else { // Too small to be a valid zip file.
      if (pstat->st_size)
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "\"%s\" is too small (%d byte%s). File may be corrupt.\n",
                  pszRelPath, (int)pstat->st_size,
                  pstat->st_size == 1 ? "" : "s");
      else
        logprint3(stderr, mig->fProcessLog, ErrorLog(ws),
                  "\"%s\" is empty. Skipping.\n", pszRelPath);
      mig->cErrorZips++;
      mig->bErrorEncountered = 1;
    }
  }

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

// Function to convert the contents of a directory.
// This function only receives directories, not files or zips.
int RecursiveMigrateDir(const char *pszRelPath, WORKSPACE *ws) {
  int rc = 0;

  char szTmpBuf[MAX_PATH + 1];
  int iElements = 0;
  char **FileNameArray = NULL;
  int iCounter = 0;
  int FileNameStartPos;

  DIR *dirp = NULL;
  MIGRATE mig = {};

  // Get our start time for the conversion process of this dir/zip
  mig.StartTime = time(NULL);

  // Couldn't access specified path
  dirp = opendir(pszRelPath);
  if (!dirp) {
    logprint(stderr, ErrorLog(ws), "Could not access subdir \"%s\"! %s\n",
             pszRelPath, strerror(errno));
    mig.bErrorEncountered = 1;
  } else {
    FileNameArray = GetDirFileList(dirp, &iElements);
    closedir(dirp);

    if (!FileNameArray) {
      logprint(stderr, ErrorLog(ws), "Error allocating memory!\n");
      rc = TZ_CRITICAL;
    } else {
      if (strcmp(pszRelPath, ".") == 0) {
        szTmpBuf[0] = 0;
        FileNameStartPos = 0;
      } else {
        FileNameStartPos =
            snprintf(szTmpBuf, sizeof(szTmpBuf), "%s%c", pszRelPath, DIRSEP);
      }

      for (iCounter = 0; (iCounter < iElements && FileNameArray[iCounter][0]);
           iCounter++) {
        struct stat istat;

        // Construct full path
        snprintf(szTmpBuf + FileNameStartPos,
                 sizeof(szTmpBuf) - FileNameStartPos, "%s",
                 FileNameArray[iCounter]);

        // Don't follow symlinks during recursion
        if (lstat(szTmpBuf, &istat)) {
          logprint3(stderr, mig.fProcessLog, ErrorLog(ws),
                    "Could not stat \"%s\". %s\n", szTmpBuf, strerror(errno));
          continue;
        }

        // Only process regular .zip files and directories (unless recursion is
        // disabled). Skip other files right here.
        if (S_ISDIR(istat.st_mode)) {
          if (qNoRecursion || !strcmp(FileNameArray[iCounter], ".") ||
              !strcmp(FileNameArray[iCounter], ".."))
            continue;
        } else if (!S_ISREG(istat.st_mode) ||
                   !EndsWithCaseInsensitive(FileNameArray[iCounter], ".zip")) {
          continue;
        }

        rc = RecursiveMigrate(szTmpBuf, &istat, ws, &mig);
        if (rc == TZ_CRITICAL)
          break;
      }
    }

    DynamicStringArrayDestroy(FileNameArray, iElements);
  }

  // Get our execution time (in seconds) for the conversion process
  mig.ExecTime += difftime(time(NULL), mig.StartTime);

  if (rc != TZ_CRITICAL)
    DisplayMigrateSummary(ws, &mig);
  if (rc != TZ_OK || mig.bErrorEncountered)
    qErrors = 1;
  if (mig.fProcessLog)
    fclose(mig.fProcessLog);

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

void DisplayMigrateSummary(WORKSPACE *ws, MIGRATE *mig) {
  double ExecTime;

  if (mig->fProcessLog) {
    ExecTime = mig->ExecTime;
    // Output it in hours, minutes and seconds
    logprint(stdout, mig->fProcessLog, "Execution time %d hours ",
             (int)ExecTime / (60 * 60));
    ExecTime = fmod(ExecTime, (60 * 60));
    logprint(stdout, mig->fProcessLog, "%d mins ", (int)ExecTime / 60);
    ExecTime = fmod(ExecTime, 60);
    logprint(stdout, mig->fProcessLog, "%d secs\n\n", (int)ExecTime);
    logprint(stdout, mig->fProcessLog, "Checked %u zip file%s.\n",
             mig->cEncounteredZips, mig->cEncounteredZips != 1 ? "s" : "");
    if (mig->cRezippedZips)
      logprint(stdout, mig->fProcessLog, "  %u file%s successfully rezipped.\n",
               mig->cRezippedZips, mig->cRezippedZips != 1 ? "s were" : " was");
    if (mig->cOkayZips)
      logprint(stdout, mig->fProcessLog, "  %u file%s already up to date.\n",
               mig->cOkayZips, mig->cOkayZips != 1 ? "s were" : " was");
    if (mig->cErrorZips)
      logprint(stdout, mig->fProcessLog, "  %u file%s had errors.\n",
               mig->cErrorZips, mig->cErrorZips != 1 ? "s" : "");

    if (mig->bErrorEncountered) {
      if (ws->fErrorLog)
        fprintf(mig->fProcessLog,
                "!!!! There were problems! See \"%s\" for details! !!!!\n",
                ws->pszErrorLogFile);
      else
        fprintf(mig->fProcessLog, "!!!! There were problems! !!!!\n");
    }
  }
}

int RecursiveMigrateTop(const char *pszRelPath, WORKSPACE *ws) {
  int rc;
  char szRelPathBuf[MAX_PATH + 1];
  int n;
  struct stat istat;
  MIGRATE mig = {};

  mig.StartTime = time(NULL);

  // Follow symlinks for direct command line arguments. Process any file
  // regardless of type and name.
  if (stat(pszRelPath, &istat)) {
    logprint(stderr, ErrorLog(ws), "Could not stat \"%s\". %s\n", pszRelPath,
             strerror(errno));
    qErrors = 1;
    return TZ_ERR;
  }

  n = strlen(pszRelPath);
  if (n > 0 && pszRelPath[n - 1] == DIRSEP) {
    snprintf(szRelPathBuf, sizeof(szRelPathBuf), "%s", pszRelPath);
    szRelPathBuf[n - 1] = 0;
    pszRelPath = szRelPathBuf;
  }

  rc = RecursiveMigrate(pszRelPath, &istat, ws, &mig);

  // Get our execution time (in seconds) for the conversion process
  mig.ExecTime += difftime(time(NULL), mig.StartTime);

  if (rc != TZ_CRITICAL)
    DisplayMigrateSummary(ws, &mig);
  if (rc != TZ_OK || mig.bErrorEncountered)
    qErrors = 1;
  if (mig.fProcessLog)
    fclose(mig.fProcessLog);

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

int main(int argc, char **argv) {
  WORKSPACE *ws;
  const char *logdir = NULL, *errlog = NULL;
  int iCount = 0;
  int iOptionsFound = 0;
  int rc = 0;

  for (iCount = 1; iCount < argc; iCount++) {
    if (argv[iCount][0] == '-') {
      iOptionsFound++;

      switch (tolower(argv[iCount][1])) {
      case '?':
      case 'h':
        fprintf(stdout, "%s",
                "TorrentZip v" TZ_VERSION "\n\n"
                "Copyright (C) 2005 - 2024 TorrentZip Team:\n"
                "\tStatMat, shindakun, Ultrasubmarine, r3nh03k, goosecreature, "
                "gordonj,\n\t0-wiz-0, A.Miller\n"
                "Homepage: https://github.com/0-wiz-0/trrntzip\n\n"
                "Usage: trrntzip [-dfghqsv] [-eFILE] [-lDIR] [PATH/ZIP FILE]\n\n"
                "Options:\n"
                "\t-h\t: show this help\n"
                "\t-d\t: strip sub-directories from zips\n"
                "\t-eFILE\t: write error log to FILE\n"
                "\t-f\t: force re-zip\n"
                "\t-g\t: skip interactive prompts\n"
                "\t-lDIR\t: write log files in DIR (empty to disable)\n"
                "\t-q\t: quiet mode\n"
                "\t-s\t: prevent sub-directory recursion\n"
                "\t-v\t: show version\n");
        return TZ_OK;

      case 'd':
        // Strip subdirs from zips
        qStripSubdirs = 1;
        break;

      case 'e':
        // Error log file
        errlog = &argv[iCount][2];
        break;

      case 'f':
        // Force rezip
        qForceReZip = 1;
        break;

      case 'g':
        // GUI launch process
        qGUILaunch = 1;
        break;

      case 'l':
        // Log directory
        logdir = &argv[iCount][2];
        break;

      case 'q':
        // Quiet mode - show less messages while running
        qQuietMode = 1;
        break;

      case 's':
        // Disable dir recursion
        qNoRecursion = 1;
        break;

      case 'v':
        // GUI requesting TZ version
        fprintf(stdout, "TorrentZip v%s\n", TZ_VERSION);
        return TZ_OK;

      default:
        fprintf(stderr, "Unknown option : %s\n", argv[iCount]);
      }
    }
  }

  if (argc < 2 || iOptionsFound == (argc - 1)) {
    fprintf(stderr, "trrntzip: missing path\n");
    fprintf(stderr, "Usage: trrntzip [-dfghqsv] [-eFILE] [-lDIR] [PATH/ZIP FILE]\n");
    return TZ_ERR;
  }

  ws = AllocateWorkspace();

  if (ws == NULL) {
    fprintf(stderr, "Error allocating memory!\n");
    return TZ_CRITICAL;
  }

  if (logdir) {
    // Must be empty or end with DIRSEP. In case we have to add DIRSEP,
    // strdup() with the leading option char and overprint.
    size_t len = strlen(logdir);
    int need_sep = len && logdir[len - 1] != DIRSEP;
    ws->pszLogDir = strdup(logdir - need_sep);
    if (need_sep && ws->pszLogDir)
      sprintf(ws->pszLogDir, "%s%c", logdir, DIRSEP);
  } else {
#ifdef WIN32
    // Must get trrntzip.exe path from argv[0].
    // Under windows, if you drag a dir to the exe, it will use the
    // user's "Documents and Settings" dir if we don't do this.
    const char *ptr = strrchr(argv[0], DIRSEP);
    if (ptr) {
      ws->pszLogDir = malloc(ptr - argv[0] + 2);
      if (ws->pszLogDir) {
        memcpy(ws->pszLogDir, argv[0], ptr - argv[0] + 1);
        ws->pszLogDir[ptr - argv[0] + 1] = 0;
      }
    } else {
      // get_cwd() seems unnecessary, we could use relative paths instead.
      ws->pszLogDir = get_cwd();
    }
#else
    // We could use relative paths.
    ws->pszLogDir = get_cwd();
#endif
  }

  if (!ws->pszLogDir) {
    fprintf(stderr, "Could not get log directory!\n");
    FreeWorkspace(ws);
    return TZ_ERR;
  }

  if (errlog) {
    ws->pszErrorLogFile = strdup(errlog);
    if (!ws->pszErrorLogFile) {
      fprintf(stderr, "Error allocating memory!\n");
      FreeWorkspace(ws);
      return TZ_CRITICAL;
    }
  }
  rc = SetupErrorLog(ws, qGUILaunch);

  if (rc == TZ_OK) {
    // Start process for each passed path/zip file
    for (iCount = iOptionsFound + 1; iCount < argc; iCount++) {
      rc = RecursiveMigrateTop(argv[iCount], ws);
      if (rc == TZ_CRITICAL)
        break;
    }

    if (qErrors) {
      if (ws->fErrorLog)
        fprintf(stderr,
                "!!!! There were problems! See \"%s\" for details! !!!!\n",
                ws->pszErrorLogFile);
      else
        fprintf(stderr, "!!!! There were problems! !!!!\n");
    }

#ifdef WIN32
    if (qErrors && !qGUILaunch) {
      // This is only needed on Windows, to keep the
      // command window window from disappearing when
      // the program completes.
      fprintf(stdout, "Press any key to exit.\n");
      fflush(stdout);
      getch();
    }
#endif
  }

  FreeWorkspace(ws);

  return rc;
}
