/* srv.h
 * Copyright (C) 2003 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * GNUPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _SRV_H_
#define _SRV_H_

#ifdef _WIN32
#include <windows.h>
#else
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif

#ifndef MAXDNAME
#define MAXDNAME 1025
#endif

struct srventry
{
  u16 priority;
  u16 weight;
  u16 port;
  int run_count;
  char target[MAXDNAME];
};

int getsrv(const char *name,struct srventry **list);

#endif /* !_SRV_H_ */