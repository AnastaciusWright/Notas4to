/* Unix.c
 * This file contains compatibility routines.
 *
 * This file Copyright 1991 Ken Shirriff.  It may be used according to the
 * fractint license conditions, blah blah blah.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include "port.h"

#define FILE_MAX_PATH  256       /* max length of path+filename  */
#define FILE_MAX_DIR   256       /* max length of directory name */
#define FILE_MAX_DRIVE  3       /* max length of drive letter   */
#define FILE_MAX_FNAME  9       /* max length of filename       */
#define FILE_MAX_EXT    5       /* max length of extension      */

int iocount;

/*
 *----------------------------------------------------------------------
 *
 * clock_ticks --
 *
 *      Return time in CLK_TCK ticks.
 *
 * Results:
 *      Time.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
long
clock_ticks()
{
    struct timeval tim;
    gettimeofday(&tim,NULL);
    return tim.tv_sec*CLK_TCK + tim.tv_usec*CLK_TCK/1000000;
}

/* stub */
intdos() {}

/*
 *----------------------------------------------------------------------
 *
 * kbhit --
 *
 *      Get a key.
 *
 * Results:
 *      1 if key, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
kbhit()
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * stackavail --
 *
 *      Returns amout of stack available.
 *
 * Results:
 *      Available stack.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
long
stackavail()
{
    return 8192;
}

#ifndef HAVESTRI
/*
 *----------------------------------------------------------------------
 *
 * stricmp --
 *
 *      Compare strings, ignoring case.
 *
 * Results:
 *      -1,0,1.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
stricmp(s1, s2)
    register char *s1, *s2;		/* Strings to compare. */
{
    int c1, c2;

    while (1) {
	c1 = *s1++;
	c2 = *s2++;
	if (isupper(c1)) c1 = tolower(c1);
	if (isupper(c2)) c2 = tolower(c2);
	if (c1 != c2) {
	    return c1 - c2;
	}
	if (c1 == 0) {
	    return 0;
	}
    }
}
/*
 *----------------------------------------------------------------------
 *
 * strnicmp --
 *
 *      Compare strings, ignoring case.  Maximum length is specified.
 *
 * Results:
 *      -1,0,1.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
strnicmp(s1, s2, numChars)
    register char *s1, *s2;		/* Strings to compare. */
    register int numChars;		/* Max number of chars to compare. */
{
    register char c1, c2;

    for ( ; numChars > 0; --numChars) {
	c1 = *s1++;
	c2 = *s2++;
	if (isupper(c1)) c1 = tolower(c1);
	if (isupper(c2)) c2 = tolower(c2);
	if (c1 != c2) {
	    return c1 - c2;
	}
	if (c1 == '\0') {
	    return 0;
	}
    }
    return 0;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * strlwr --
 *
 *      Convert string to lower case.
 *
 * Results:
 *      The string.
 *
 * Side effects:
 *      Modifies the string.
 *
 *----------------------------------------------------------------------
 */
char *
strlwr(s)
    char *s;
{
    register char *sptr=s;
    while (*sptr != '\0') {
	if (isupper(*sptr)) {
	    *sptr = tolower(*sptr);
	}
	sptr++;
    }
    return s;
}
/*
 *----------------------------------------------------------------------
 *
 * strupr --
 *
 *      Convert string to upper case.
 *
 * Results:
 *      The string.
 *
 * Side effects:
 *      Modifies the string.
 *
 *----------------------------------------------------------------------
 */
char *
strupr(s)
    char *s;
{
    register char *sptr=s;
    while (*sptr != '\0') {
	if (islower(*sptr)) {
	    *sptr = toupper(*sptr);
	}
	sptr++;
    }
    return s;
}
/*
 *----------------------------------------------------------------------
 *
 * memicmp --
 *
 *      Compare memory (like memcmp), but ignoring case.
 *
 * Results:
 *      -1,0,1.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
memicmp(s1, s2, n)
        register char *s1, *s2;
        register n;
{
        register char c1,c2;
        while (--n >= 0)
		c1 = *s1++;
		if (isupper(c1)) c1 = tolower(c1);
		c2 = *s2++;
		if (isupper(c2)) c2 = tolower(c2);
                if (c1 != c2)
                        return (c1 - c2);
        return (0);
}

/*
 *----------------------------------------------------------------------
 *
 * findpath --
 *
 *      Find where a file is.
 *	We return filename if it is an absolute path.
 *	Otherwise we first try FRACTDIR/filename, SRCDIR/filename,
 *      and then ./filename.
 *
 * Results:
 *      Returns full pathname in fullpathname.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
findpath(filename, fullpathname)
char *filename, *fullpathname;
{
    int fd;
    char *fractdir;

    if (filename[0]=='/') {
	strcpy(fullpathname,filename);
	return;
    }
    fractdir = getenv("FRACTDIR");
    if (fractdir != NULL) {
	strcpy(fullpathname,fractdir);
	strcat(fullpathname,"/");
	strcat(fullpathname,filename);
	fd = open(fullpathname,O_RDONLY);
	if (fd != -1) {
	    close(fd);
	    return;
	}
    }
    strcpy(fullpathname,SRCDIR);
    strcat(fullpathname,"/");
    strcat(fullpathname,filename);
    fd = open(fullpathname,O_RDONLY);
    if (fd != -1) {
	close(fd);
	return;
    }
    strcpy(fullpathname,"./");
    strcat(fullpathname,filename);
}

/*
 *----------------------------------------------------------------------
 *
 * ltoa --
 *
 *      Convert long to string.
 *
 * Results:
 *      0.
 *
 * Side effects:
 *      Prints number into the string.
 *
 *----------------------------------------------------------------------
 */
int ltoa(num,str,len)
long num;
char *str;
int len;
{
    sprintf(str,"%10d",num);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * filelength --
 *
 *      Find length of a file.
 *
 * Results:
 *      Length.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int filelength(fd)
int fd;
{
    struct stat buf;
    fstat(fd,&buf);
    return buf.st_size;
}

/*
 *----------------------------------------------------------------------
 *
 * splitpath --
 *
 *      This is the splitpath code from prompts.c
 *
 * Results:
 *      Returns drive, dir, base, and extension.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
splitpath(char *template,char *drive,char *dir,char *fname,char *ext)
{
   int length;
   int len;
   int offset;
   char *tmp;

   if(drive)
      drive[0] = 0;
   if(dir)
      dir[0]   = 0;
   if(fname)
      fname[0] = 0;
   if(ext)
      ext[0]   = 0;

   if((length = strlen(template)) == 0)
      return(0);
   offset = 0;

   /* get drive */
   if(length >= 2)
      if(template[1] == ':')
      {
	 if(drive)
	 {
	    drive[0] = template[offset++];
	    drive[1] = template[offset++];
	    drive[2] = 0;
	 }
	 else
	 {
	    offset++;
	    offset++;
	 }
      }

   /* get dir */
   if(offset < length)
   {
      tmp = strrchr(template,SLASHC);
      if(tmp)
      {
	 tmp++;  /* first character after slash */
	 len = tmp - &template[offset];
	 if(len >=0 && len < FILE_MAX_DIR && dir)
	    strncpy(dir,&template[offset],min(len,FILE_MAX_DIR));
	 if(len < FILE_MAX_DIR && dir)
	    dir[len] = 0;
	 offset += len;
      }
   }
   else
      return(0);

   /* get fname */
   if(offset < length)
   {
      tmp = strrchr(template,'.');
      if(tmp < strrchr(template,SLASHC) || tmp < strrchr(template,':'))
	 tmp = 0; /* in this case the '.' must be a directory */
      if(tmp)
      {
	 /* tmp++; */ /* first character past "." */
	 len = tmp - &template[offset];
	 if((len > 0) && (offset+len < length) && fname)
	 {
	    strncpy(fname,&template[offset],min(len,FILE_MAX_FNAME));
            if(len < FILE_MAX_FNAME)
               fname[len] = 0;
            else
               fname[FILE_MAX_FNAME-1] = 0;
	 }
	 offset += len;
	 if((offset < length) && ext)
	 {
	    strncpy(ext,&template[offset],FILE_MAX_EXT);
            ext[FILE_MAX_EXT-1] = 0;
	 }
      }
      else if((offset < length) && fname)
      {
         strncpy(fname,&template[offset],FILE_MAX_FNAME);
         fname[FILE_MAX_FNAME-1] = 0;
      }
   }
   return(0);
}

_splitpath(char *template,char *drive,char *dir,char *fname,char *ext)
{
    return splitpath(template,drive,dir,fname,ext);
}

/* This ftime simulation routine is from Frank Chen */
void ftimex(tp)
struct timebx    *tp;
{
        struct timeval  timep;
        struct timezone timezp;

        if ( gettimeofday(&timep,&timezp) != 0) {
                perror("error in gettimeofday");
                exit(0);
        }
        tp->time = timep.tv_sec;
        tp->millitm = timep.tv_usec/1000;
        tp->timezone = timezp.tz_minuteswest;
        tp->dstflag = timezp.tz_dsttime;
}

unsigned short _rotl(unsigned short num, short bits)
{
   unsigned long ll;
   ll = (((unsigned long)num << 16) + num) << (bits&15);
   return((unsigned short)(ll>>16)); 
}

/* sound.c file prototypes */
int get_sound_params(void)
{
}
void soundon(int i)
{
}
void soundoff(void)
{
}

void mute(void)
{
}

int initfm(void)
{
  return(0);
}

/* tenths of millisecond timewr routine */
static struct timeval tv_start;

void restart_uclock(void)
{
   gettimeofday(&tv_start, NULL);
}

typedef unsigned long uclock_t;
uclock_t usec_clock(void)
{
   uclock_t result;

   struct timeval tv, elapsed;
   gettimeofday(&tv, NULL);

   elapsed.tv_usec  = tv.tv_usec -  tv_start.tv_sec;
   elapsed.tv_sec   = tv.tv_sec -   tv_start.tv_sec;

   if(elapsed.tv_usec < 0)
   {
      /* "borrow */
      elapsed.tv_usec += 1000000;
      elapsed.tv_sec--;
   }
   result  = (unsigned long)(elapsed.tv_sec*10000 +  elapsed.tv_usec/100);
   return(result); 
}

