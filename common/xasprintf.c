/* xasprintf.c
 *	Copyright (C) 2003 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdlib.h>
#include <errno.h>

#include "util.h"
#include "iobuf.h"

/* Same as asprintf but return an allocated buffer suitable to be
   freed using xfree.  This function simply dies on memory failure,
   thus no extra check is required. */
char *
xasprintf (const char *fmt, ...)
{
  va_list ap;
  char *buf, *p;

  va_start (ap, fmt);
  if (vasprintf (&buf, fmt, ap) < 0)
    log_fatal ("asprintf failed: %s\n", strerror (errno));
  va_end (ap);
  p = xstrdup (buf);
  free (buf);
  return p;
}