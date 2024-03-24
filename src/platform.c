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
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "platform.h"

int mkstemp(char *ntemplate) {
  int i, fd = -1;
  char *copy = strdup(ntemplate);

  for (i = 0; i < 10; i++) {
    if (!mktemp(ntemplate))
      break;

    fd = open(ntemplate, O_RDWR | O_CREAT | O_EXCL, S_IREAD | S_IWRITE);
    if (fd >= 0 || errno != EEXIST)
      break;

    if (!copy) {
      errno = ENOMEM;
      break;
    }
    strcpy(ntemplate, copy);
  }
  free(copy);
  return fd;
}

#else

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "platform.h"

#if defined(__CYGWIN__)
/* Workaround for Cygwin, which is missing cfmakeraw */
/* Pasted from man page; added in serial.c arbitrarily */
void cfmakeraw(struct termios *termios_p) {
  termios_p->c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  termios_p->c_oflag &= ~OPOST;
  termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  termios_p->c_cflag &= ~(CSIZE | PARENB);
  termios_p->c_cflag |= CS8;
}
#endif /* defined(__CYGWIN__) */

// Not sure if this is the best way to implement this, but it works.
int getch(void) {
  struct termios t, t2;
  int c;

  tcgetattr(1, &t);
  t2 = t;
  cfmakeraw(&t2);
  tcsetattr(1, TCSANOW, &t2);
  c = getchar();
  tcsetattr(1, TCSANOW, &t);

  return c;
}

#endif
