/* sc-investigate.c - A tool to look around on smartcards.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define JNLIB_NEED_LOG_LOGV
#include "scdaemon.h"
#include <gcrypt.h>

#include "apdu.h" /* for open_reader */
#include "atr.h"
#include "app-common.h"
#include "iso7816.h"

#define _(a) (a)

#define CONTROL_D ('D' - 'A' + 1)


enum cmd_and_opt_values 
{ 
  oInteractive    = 'i',
  oVerbose	  = 'v',
  oReaderPort     = 500,
  octapiDriver,
  oDebug,
  oDebugAll,

  oDisableCCID,


  oGenRandom,

aTest };


static ARGPARSE_OPTS opts[] = {
  
  { 301, NULL, 0, "@Options:\n " },

  { oInteractive, "interactive", 0, "start in interactive explorer mode"},
  { oVerbose, "verbose",   0, "verbose" },
  { oReaderPort, "reader-port", 2, "|N|connect to reader at port N"},
  { octapiDriver, "ctapi-driver", 2, "NAME|use NAME as ctAPI driver"},
  { oDisableCCID, "disable-ccid", 0,
#ifdef HAVE_LIBUSB
                                    "do not use the internal CCID driver"
#else
                                    "@"
#endif
  },
  { oDebug,	"debug"     ,4|16, "set debugging flags"},
  { oDebugAll, "debug-all" ,0, "enable full debugging"},
  { oGenRandom, "gen-random", 4, "|N|generate N bytes of random"},
  {0}
};


static void interactive_shell (int slot);


static const char *
my_strusage (int level)
{
  const char *p;
  switch (level)
    {
    case 11: p = "sc-investigate (GnuPG)";
      break;
    case 13: p = VERSION; break;
    case 17: p = PRINTABLE_OS_NAME; break;
    case 19: p = _("Please report bugs to <" PACKAGE_BUGREPORT ">.\n");
      break;
    case 1:
    case 40: p =  _("Usage: sc-investigate [options] (-h for help)\n");
      break;
    case 41: p =  _("Syntax: sc-investigate [options] [args]]\n"
                    "Have a look at smartcards\n");
    break;
    
    default: p = NULL;
    }
  return p;
}

/* Used by gcry for logging */
static void
my_gcry_logger (void *dummy, int level, const char *fmt, va_list arg_ptr)
{
  /* translate the log levels */
  switch (level)
    {
    case GCRY_LOG_CONT: level = JNLIB_LOG_CONT; break;
    case GCRY_LOG_INFO: level = JNLIB_LOG_INFO; break;
    case GCRY_LOG_WARN: level = JNLIB_LOG_WARN; break;
    case GCRY_LOG_ERROR:level = JNLIB_LOG_ERROR; break;
    case GCRY_LOG_FATAL:level = JNLIB_LOG_FATAL; break;
    case GCRY_LOG_BUG:  level = JNLIB_LOG_BUG; break;
    case GCRY_LOG_DEBUG:level = JNLIB_LOG_DEBUG; break;
    default:            level = JNLIB_LOG_ERROR; break;  
    }
  log_logv (level, fmt, arg_ptr);
}


int
main (int argc, char **argv )
{
  ARGPARSE_ARGS pargs;
  int slot, rc;
  const char *reader_port = NULL;
  unsigned long gen_random = 0;
  int interactive = 0;

  set_strusage (my_strusage);
  gcry_control (GCRYCTL_SUSPEND_SECMEM_WARN);
  log_set_prefix ("sc-investigate", 1); 

  /* check that the libraries are suitable.  Do it here because
     the option parsing may need services of the library */
  if (!gcry_check_version (NEED_LIBGCRYPT_VERSION) )
    {
      log_fatal( _("libgcrypt is too old (need %s, have %s)\n"),
                 NEED_LIBGCRYPT_VERSION, gcry_check_version (NULL) );
    }

  gcry_set_log_handler (my_gcry_logger, NULL);
  /* FIXME? gcry_control (GCRYCTL_USE_SECURE_RNDPOOL);*/

  pargs.argc = &argc;
  pargs.argv = &argv;
  pargs.flags=  1;  /* do not remove the args */
  while (arg_parse (&pargs, opts) )
    {
      switch (pargs.r_opt)
        {
        case oVerbose: opt.verbose++; break;
        case oDebug: opt.debug |= pargs.r.ret_ulong; break;
        case oDebugAll: opt.debug = ~0; break;
        case oReaderPort: reader_port = pargs.r.ret_str; break;
        case octapiDriver: opt.ctapi_driver = pargs.r.ret_str; break;
        case oDisableCCID: opt.disable_ccid = 1; break;
        case oGenRandom: gen_random = pargs.r.ret_ulong; break;
        case oInteractive: interactive = 1; break;
        default : pargs.err = 2; break;
	}
    }
  if (log_get_errorcount(0))
    exit(2);

  if (opt.verbose < 2)
    opt.verbose = 2; /* Hack to let select_openpgp print some info. */

  if (argc)
    usage (1);

  slot = apdu_open_reader (reader_port);
  if (slot == -1)
    exit (1);
  
  if (!gen_random)
    {
      rc = atr_dump (slot, stdout); 
      if (rc)
        log_error ("can't dump ATR: %s\n", gpg_strerror (rc));
    }

  if (interactive)
    interactive_shell (slot);
  else
    {
      struct app_ctx_s appbuf;

      /* Fixme: We better use app.c directly. */
      memset (&appbuf, 0, sizeof appbuf);
      appbuf.slot = slot;
      rc = app_select_openpgp (&appbuf);
      if (rc)
        {
          log_info ("selecting openpgp failed: %s\n", gpg_strerror (rc));
          memset (&appbuf, 0, sizeof appbuf);
          appbuf.slot = slot;
          rc = app_select_dinsig (&appbuf);
          if (rc)
            log_info ("selecting dinsig failed: %s\n", gpg_strerror (rc));
          else
            {
              appbuf.initialized = 1;
              log_info ("dinsig application selected\n");
            }
        }
      else
        {
          appbuf.initialized = 1;
          log_info ("openpgp application selected\n");

          if (gen_random)
            {
              size_t nbytes;
              unsigned char *buffer;
          
              buffer = xmalloc (4096);
              do 
                {
                  nbytes = gen_random > 4096? 4096 : gen_random;
                  rc = app_get_challenge (&appbuf, nbytes, buffer);
                  if (rc)
                    log_error ("app_get_challenge failed: %s\n",gpg_strerror (rc));
                  else
                    {
                      if (fwrite (buffer, nbytes, 1, stdout) != 1)
                        log_error ("writing to stdout failed: %s\n",
                                   strerror (errno));
                      gen_random -= nbytes;
                    }
                }
              while (gen_random && !log_get_errorcount (0));
              xfree (buffer);
            }
        }
    }
  
  return log_get_errorcount (0)? 2:0;
}



void
send_status_info (CTRL ctrl, const char *keyword, ...)
{
  /* DUMMY */
}



/* Dump BUFFER of length NBYTES in a nicely human readable format. */ 
static void
dump_buffer (const unsigned char *buffer, size_t nbytes)
{
  int i;

  while (nbytes)
    {
      for (i=0; i < 16 && i < nbytes; i++)
        printf ("%02X%s ", buffer[i], i==8? " ":"");
      for (; i < 16; i++)
        printf ("  %s ", i==8? " ":"");
      putchar (' ');
      putchar (' ');
      for (i=0; i < 16 && i < nbytes; i++)
        if (isprint (buffer[i]))
          putchar (buffer[i]);
        else
          putchar ('.');
      nbytes -= i;
      buffer += i;
      for (; i < 16; i++)
        putchar (' ');
      putchar ('\n');
    }
}


static void
dump_or_store_buffer (const char *arg,
                      const unsigned char *buffer, size_t nbytes)
{
  const char *s = strchr (arg, '>');
  int append;
  FILE *fp;

  if (!s)
    {
      dump_buffer (buffer, nbytes);
      return;
    }
  if ((append = (*++s == '>')))
    s++;
  fp = fopen (s, append? "ab":"wb");
  if (!fp)
    {
      log_error ("failed to create `%s': %s\n", s, strerror (errno));
      return;
    }
  if (nbytes && fwrite (buffer, nbytes, 1, fp) != 1)
      log_error ("failed to write to `%s': %s\n", s, strerror (errno));
  if (fclose (fp))
      log_error ("failed to close `%s': %s\n", s, strerror (errno));
}


/* Convert STRING into a a newly allocated buffer and return the
   length of the buffer in R_LENGTH.  Detect xx:xx:xx... sequence and
   unhexify that one. */
static unsigned char *
pin_to_buffer (const char *string, size_t *r_length)
{
  unsigned char *buffer = xmalloc (strlen (string)+1);
  const char *s;
  size_t n;

  for (s=string, n=0; *s; s += 3)
    {
      if (hexdigitp (s) && hexdigitp (s+1) && (s[2]==':'||!s[2]))
        {
          buffer[n++] = xtoi_2 (s);
          if (!s[2])
            break;
        }
      else
        {
          memcpy (buffer, string, strlen (string));
          *r_length = strlen (string);
          return buffer;
        }
    }
  *r_length = n;
  return buffer;
}


static char *
read_line (int use_readline, char *prompt)
{
  static char buf[256];

#ifdef HAVE_READLINE
  if (use_readline)
    {
      char *line = readline (prompt);
      if (line)
        trim_spaces (line);
      if (line && strlen (line) > 2 )
        add_history (line);
      return line;
    }
#endif
  /* Either we don't have readline or we are not running
     interactively */
#ifndef HAVE_READLINE
  printf ("%s", prompt );
#endif
  fflush(stdout);
  if (!fgets(buf, sizeof(buf), stdin))
    return NULL;
  if (!strlen(buf))
    return NULL;
  if (buf[strlen (buf)-1] == '\n')
    buf[strlen (buf)-1] = 0;
  trim_spaces (buf);
  return buf;
}

/* Run a shell for interactive exploration of the card. */
static void
interactive_shell (int slot)
{
  enum cmdids
    {
      cmdNOP = 0,
      cmdQUIT, cmdHELP,
      cmdSELECT,
      cmdCHDIR,
      cmdLS,
      cmdAPP,
      cmdREAD,
      cmdREADREC,
      cmdDEBUG,
      cmdVERIFY,
      cmdCHANGEREF,

      cmdINVCMD
    };
  static struct 
  {
    const char *name;
    enum cmdids id;
    const char *desc;
  } cmds[] = {
    { "quit"   , cmdQUIT  , "quit this menu" },
    { "q"      , cmdQUIT  , NULL   },
    { "help"   , cmdHELP  , "show this help" },
    { "?"      , cmdHELP  , NULL   },
    { "debug"  , cmdDEBUG, "set debugging flags" },
    { "select" , cmdSELECT, "select file (EF)" },
    { "s"      , cmdSELECT, NULL },
    { "chdir"  , cmdCHDIR, "change directory (select DF)"},
    { "cd"     , cmdCHDIR,  NULL },
    { "ls"     , cmdLS,    "list directory (some cards only)"},
    { "app"    , cmdAPP,   "select application"},
    { "read"   , cmdREAD,  "read binary" },
    { "rb"     , cmdREAD,  NULL },
    { "readrec", cmdREADREC,  "read record(s)" },
    { "rr"     , cmdREADREC,  NULL },
    { "verify" , cmdVERIFY, "verify CHVNO PIN" },
    { "ver"    , cmdVERIFY, NULL },
    { "changeref", cmdCHANGEREF, "change reference data" },
    { NULL, cmdINVCMD } 
  };
  enum cmdids cmd = cmdNOP;
  int use_readline = isatty (fileno(stdin));
  char *line;
  gpg_error_t err = 0;
  unsigned char *result = NULL;
  size_t resultlen;

#ifdef HAVE_READLINE
  if (use_readline)
    using_history ();
#endif

  for (;;)
    {
      int arg_number;
      const char *arg_string = "";
      const char *arg_next = "";
      char *p;
      int i;
      
      if (err)
        printf ("command failed: %s\n", gpg_strerror (err));
      err = 0;
      xfree (result);
      result = NULL;

      printf ("\n");
      do
        {
          line = read_line (use_readline, "cmd> ");
	}
      while ( line && *line == '#' );

      arg_number = 0; 
      if (!line || *line == CONTROL_D)
        cmd = cmdQUIT; 
      else if (!*line)
        cmd = cmdNOP;
      else {
        if ((p=strchr (line,' ')))
          {
            char *endp;

            *p++ = 0;
            trim_spaces (line);
            trim_spaces (p);
            arg_number = strtol (p, &endp, 0);
            arg_string = p;
            if (endp != p)
              {
                arg_next = endp;
                while ( spacep (arg_next) )
                  arg_next++;
              }
          }

        for (i=0; cmds[i].name; i++ )
          if (!ascii_strcasecmp (line, cmds[i].name ))
            break;
        
        cmd = cmds[i].id;
      }
      
      switch (cmd)
        {
        case cmdHELP:
          for (i=0; cmds[i].name; i++ )
            if (cmds[i].desc)
              printf("%-10s %s\n", cmds[i].name, _(cmds[i].desc) );
          break;

        case cmdQUIT:
          goto leave;

        case cmdNOP:
          break;

        case cmdDEBUG:
          if (!*arg_string)
            opt.debug = opt.debug? 0 : 2048;
          else
            opt.debug = arg_number;
          break;

        case cmdSELECT:
          err = iso7816_select_file (slot, arg_number, 0, NULL, NULL);
          break;

        case cmdCHDIR:
          err = iso7816_select_file (slot, arg_number, 1, NULL, NULL);
          break;

        case cmdLS:
          err = iso7816_list_directory (slot, 1, &result, &resultlen);
          if (!err || gpg_err_code (err) == GPG_ERR_ENOENT)
            err = iso7816_list_directory (slot, 0, &result, &resultlen);
          /* FIXME: Do something with RESULT. */
          break;

        case cmdAPP:
          {
            app_t app;

            app = select_application (NULL, slot, *arg_string? arg_string:NULL);
            if (app)
              {
                char *sn;

                app_get_serial_and_stamp (app, &sn, NULL);
                log_info ("application `%s' ready; sn=%s\n",
                          app->apptype?app->apptype:"?", sn? sn:"[none]");
                release_application (app);
              }
          }
          break;

        case cmdREAD:
          err = iso7816_read_binary (slot, 0, 0, &result, &resultlen);
          if (!err)
            dump_or_store_buffer (arg_string, result, resultlen);
          break;

        case cmdREADREC:
          if (*arg_string == '*' && (!arg_string[1] || arg_string[1] == ' '))
            {
              /* Fixme: Can't write to a file yet. */
              for (i=1, err=0; !err; i++)
                {
                  xfree (result); result = NULL;
                  err = iso7816_read_record (slot, i, 1, &result, &resultlen);
                  if (!err)
                    dump_buffer (result, resultlen);
                }
              if (gpg_err_code (err) == GPG_ERR_NOT_FOUND)
                err = 0;
            }
          else
            {
              err = iso7816_read_record (slot, arg_number, 1,
                                         &result, &resultlen);
              if (!err)
                dump_or_store_buffer (arg_string, result, resultlen);
            }
          break;

        case cmdVERIFY:
          if (arg_number < 0 || arg_number > 255 || (arg_number & 127) > 31)
            printf ("error: invalid CHVNO\n");
          else 
            {
              unsigned char *pin;
              size_t pinlen;

              pin = pin_to_buffer (arg_next, &pinlen);
              err = iso7816_verify (slot, arg_number, pin, pinlen);
              xfree (pin);
            }
            break;

        case cmdCHANGEREF:
          {
            const char *newpin = arg_next;
            
            while ( *newpin && !spacep (newpin) )
              newpin++;
            while ( spacep (newpin) )
              newpin++;

            if (arg_number < 0 || arg_number > 255 || (arg_number & 127) > 31)
              printf ("error: invalid CHVNO\n");
            else if (!*arg_next || !*newpin || newpin == arg_next)
              printf ("usage: changeref CHVNO OLDPIN NEWPIN\n");
            else
              {
                char *oldpin = xstrdup (arg_next);
                unsigned char *oldpin_buf, *newpin_buf;
                size_t oldpin_len, newpin_len;

                for (p=oldpin; *p && !spacep (p); p++ )
                  ;
                *p = 0;
                oldpin_buf = pin_to_buffer (oldpin, &oldpin_len);
                newpin_buf = pin_to_buffer (newpin, &newpin_len);

                err = iso7816_change_reference_data (slot, arg_number,
                                                     oldpin_buf, oldpin_len,
                                                     newpin_buf, newpin_len);

                xfree (newpin_buf);
                xfree (oldpin_buf);
                xfree (oldpin);
              }
          }
          break;

        case cmdINVCMD:
        default:
          printf ("\n");
          printf ("Invalid command  (try \"help\")\n");
          break;
        } /* End command switch. */
    } /* End of main menu loop. */

 leave:
  ;
}
