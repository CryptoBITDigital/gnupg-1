/* delete.c
 * Copyright (C) 2002 Free Software Foundation, Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h> 
#include <time.h>
#include <assert.h>

#include "gpgsm.h"
#include <gcrypt.h>
#include <ksba.h>

#include "keydb.h"
#include "i18n.h"


/* Delete a certificate or an secret key from a key database. */
static int
delete_one (CTRL ctrl, const char *username)
{
  int rc = 0;
  KEYDB_SEARCH_DESC desc;
  KEYDB_HANDLE kh = NULL;
  ksba_cert_t cert = NULL;
  int duplicates = 0;
  
  rc = keydb_classify_name (username, &desc);
  if (rc)
    {
      log_error (_("certificate `%s' not found: %s\n"),
                 username, gpg_strerror (rc));
      gpgsm_status2 (ctrl, STATUS_DELETE_PROBLEM, "1", NULL);
      goto leave;
    }

  kh = keydb_new (0);
  if (!kh)
    {
      log_error ("keydb_new failed\n");
      goto leave;
    }

      
  rc = keydb_search (kh, &desc, 1);
  if (!rc)
    rc = keydb_get_cert (kh, &cert);
  if (!rc)
    {
      char fpr[20];

      gpgsm_get_fingerprint (cert, 0, fpr, NULL);

    next_ambigious:
      rc = keydb_search (kh, &desc, 1);
      if (rc == -1)
        rc = 0;
      else if (!rc)
        {
          ksba_cert_t cert2 = NULL;
          char fpr2[20];

          /* We ignore all duplicated certificates which might have
             been inserted due to program bugs. */
          if (!keydb_get_cert (kh, &cert2))
            {
              gpgsm_get_fingerprint (cert2, 0, fpr2, NULL);
              ksba_cert_release (cert2);
              if (!memcmp (fpr, fpr2, 20))
                {
                  duplicates++;
                  goto next_ambigious;
                }
            }
          rc = gpg_error (GPG_ERR_AMBIGUOUS_NAME);
        }
    }
  if (rc)
    {
      if (rc == -1)
        rc = gpg_error (GPG_ERR_NO_PUBKEY);
      log_error (_("certificate `%s' not found: %s\n"),
                 username, gpg_strerror (rc));
      gpgsm_status2 (ctrl, STATUS_DELETE_PROBLEM, "3", NULL);
      goto leave;
    }

  /* We need to search again to get back to the right position. */
  rc = keydb_lock (kh);
  if (rc)
    {
      log_error (_("error locking keybox: %s\n"), gpg_strerror (rc));
      goto leave;
    }
                   
  do 
    {
      keydb_search_reset (kh);
      rc = keydb_search (kh, &desc, 1);
      if (rc)
        {
          log_error ("problem re-searching certificate: %s\n",
                     gpg_strerror (rc));
          goto leave;
        }
      
      rc = keydb_delete (kh);
      if (rc) 
        goto leave;
      if (opt.verbose)
        {
          if (duplicates)
            log_info (_("duplicated certificate `%s' deleted\n"), username);
          else
            log_info (_("certificate `%s' deleted\n"), username);
        }
    }
  while (duplicates--);

 leave:
  keydb_release (kh);
  ksba_cert_release (cert);
  return rc;
}



/* Delete the certificates specified by NAMES. */
int
gpgsm_delete (CTRL ctrl, STRLIST names)
{
  int rc;

  if (!names)
    {
      log_error ("nothing to delete\n");
      return gpg_error (GPG_ERR_NO_DATA);
    }
  
  for (; names; names=names->next )
    {
      rc = delete_one (ctrl, names->d);
      if (rc)
        {
          log_error (_("deleting certificate \"%s\" failed: %s\n"),
                     names->d, gpg_strerror (rc) );
          return rc;
        }
    }
  
  return 0;
}