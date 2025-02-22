/*
        encoder.c - GIF Encoder and associated routines
*/

#include <string.h>
#include <limits.h>
#ifndef XFRACT
#include <io.h>
#endif
  /* see Fractint.c for a description of the "include"  hierarchy */
#include "port.h"
#include "prototyp.h"
#include "fractype.h"

static int compress(int rowlimit);
static int _fastcall shftwrite(BYTE far * color, int numcolors);
static int _fastcall extend_blk_len(int datalen);
static int _fastcall put_extend_blk(int block_id, int block_len, char far * block_data);
static int _fastcall store_item_name(char *);
static void _fastcall setup_save_info(struct fractal_info far * save_info);

#ifdef XFRACT
int decode_fractal_info();
#endif

/*
                        Save-To-Disk Routines (GIF)

The following routines perform the GIF encoding when the 's' key is pressed.

The compression logic in this file has been replaced by the classic
UNIX compress code. We have extensively modified the sources to fit
Fractint's needs, but have left the original credits where they
appear. Thanks to the original authors for making available these
classic and reliable sources. Of course, they our not responsible for 
all the changes we have made to integrate their sources into Fractint.

MEMORY ALLOCATION

There are two large arrays:

   long htab[HSIZE]              (5003*4 = 20012 bytes)
   unsigned short codetab[HSIZE] (5003*2 = 10006 bytes)
   
At the moment these arrays reuse extraseg and strlocn, respectively.

*/

static int numsaves = 0;        /* For adjusting 'save-to-disk' filenames */
static FILE *g_outfile;
static int last_colorbar;
static int save16bit;
static int outcolor1s, outcolor2s;
static int startbits;

static BFCODE paletteBW[] =
{                               /* B&W palette */
   0, 0, 0, 63, 63, 63,
};

static BFCODE paletteCGA[] =
{                               /* 4-color (CGA) palette  */
   0, 0, 0, 21, 63, 63, 63, 21, 63, 63, 63, 63,
};

static BFCODE paletteEGA[] =
{                               /* 16-color (EGA/CGA) pal */
   0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42,
   42, 0, 0, 42, 0, 42, 42, 21, 0, 42, 42, 42,
   21, 21, 21, 21, 21, 63, 21, 63, 21, 21, 63, 63,
   63, 21, 21, 63, 21, 63, 63, 63, 21, 63, 63, 63,
};

int savetodisk(char *filename)      /* save-to-disk routine */
{
   char tmpmsg[41];                 /* before openfile in case of overrun */
   char openfile[FILE_MAX_PATH], openfiletype[10];
   char tmpfile[FILE_MAX_PATH];
   char *period;
   int newfile;
   int i, j, interrupted, outcolor1, outcolor2;
restart:

   save16bit = disk16bit;
   if (gif87a_flag)             /* not storing non-standard fractal info */
      save16bit = 0;

   strcpy(openfile, filename);  /* decode and open the filename */
   strcpy(openfiletype, DEFAULTFRACTALTYPE);    /* determine the file
                                                 * extension */
   if (save16bit)
      strcpy(openfiletype, ".pot");

   if ((period = has_ext(openfile)) != NULL)
   {
      strcpy(openfiletype, period);
      *period = 0;
   }
   if (resave_flag != 1)
      updatesavename(filename); /* for next time */

   strcat(openfile, openfiletype);

   strcpy(tmpfile, openfile);
   if (access(openfile, 0) != 0)/* file doesn't exist */
      newfile = 1;
   else
   {                                  /* file already exists */
      static FCODE fractint_tmp[] = {"fractint.tmp"};
      if (overwrite == 0)
      {
         if (resave_flag == 0)
            goto restart;
         if (started_resaves == 0)
         {                      /* first save of a savetime set */
            updatesavename(filename);
            goto restart;
         }
      }
      if (access(openfile, 2) != 0)
      {
         sprintf(tmpmsg, s_cantwrite, openfile);
         stopmsg(0, tmpmsg);
         return -1;
      }
      newfile = 0;
      i = strlen(tmpfile);
      while (--i >= 0 && tmpfile[i] != SLASHC)
         tmpfile[i] = 0;
      far_strcat(tmpfile, fractint_tmp);
   }

   started_resaves = (resave_flag == 1) ? 1 : 0;
   if (resave_flag == 2)        /* final save of savetime set? */
      resave_flag = 0;

   if ((g_outfile = fopen(tmpfile, "wb")) == NULL)
   {
      sprintf(tmpmsg, s_cantcreate, tmpfile);
      stopmsg(0, tmpmsg);
      return -1;
   }

   if (dotmode == 11)
   {                            /* disk-video */
      char buf[61];
      extract_filename(tmpmsg, openfile);

      sprintf(buf, "Saving %s", tmpmsg);
      dvid_status(1, buf);
   }
#ifdef XFRACT
   else
   {
      putstring(3, 0, 0, "Saving to:");
      putstring(4, 0, 0, openfile);
      putstring(5, 0, 0, "               ");
   }
#endif

   busy = 1;

   if (debugflag != 200)
      interrupted = encoder();
   else
      interrupted = timer(2, NULL);     /* invoke encoder() via timer */

   busy = 0;

   fclose(g_outfile);

   if (interrupted)
   {
      static FCODE s_delete[] = 
         {"delete the file,\ncontinue to keep the partial image."};
      static FCODE s_retain[] = 
         {"retain the original file,\ncontinue to replace original with new partial image."};
      
      char buf[200];
      sprintf(buf, "Save of %s interrupted.\nCancel to ", openfile);
      if (newfile)
         far_strcat(buf,s_delete);
      else
         far_strcat(buf,s_retain);
      interrupted = 1;
      if (stopmsg(2, buf) < 0)
      {
         interrupted = -1;
         unlink(tmpfile);
      }
   }

   if (newfile == 0 && interrupted >= 0)
   {                            /* replace the real file */
      unlink(openfile);         /* success assumed since we checked */
      rename(tmpfile, openfile);/* earlier with access              */
   }

   if (dotmode != 11)
   {                            /* supress this on disk-video */
      if (active_system == 0)
      {                         /* no bars in Windows version */
         outcolor1 = outcolor1s;
         outcolor2 = outcolor2s;
         for (j = 0; j <= last_colorbar; j++)
         {
            if ((j & 4) == 0)
            {
               if (++outcolor1 >= colors)
                  outcolor1 = 0;
               if (++outcolor2 >= colors)
                  outcolor2 = 0;
            }
            for (i = 0; 250 * i < xdots; i++)
            {  /* clear vert status bars */
               putcolor(i, j, getcolor(i, j) ^ outcolor1);
               putcolor(xdots - 1 - i, j, 
                  getcolor(xdots - 1 - i, j) ^ outcolor2);
            }
         }
      }
#ifdef XFRACT
      putstring(5, 0, 0, "Saving done\n");
#endif
   }
   else                         /* disk-video */
      dvid_status(1, "");

   if (interrupted)
   {
      static FCODE msg[] = {" *interrupted* save "};
      texttempmsg(msg);
      if (initbatch >= 1)
         initbatch = 3;         /* if batch mode, set error level */
      return -1;
   }
   if (timedsave == 0)
   {
      buzzer(0);
      if (initbatch == 0)
      {
         extract_filename(tmpfile, openfile);
         sprintf(tmpmsg, " File saved as %s ", tmpfile);
         texttempmsg(tmpmsg);
      }
   }
   if (initsavetime < 0)
      goodbye();
   return 0;
}

int encoder()
{
   int i, width, rowlimit, interrupted;
   BYTE bitsperpixel, x;
   struct fractal_info save_info;

   if (initbatch)               /* flush any impending keystrokes */
      while (keypressed())
         getakey();

   setup_save_info(&save_info);

#ifndef XFRACT
   bitsperpixel = 0;            /* calculate bits / pixel */
   for (i = colors; i >= 2; i /= 2)
      bitsperpixel++;

   startbits = bitsperpixel + 1;/* start coding with this many bits */
   if (colors == 2)
      startbits++;              /* B&W Klooge */
#else
   if (colors == 2)
   {
      bitsperpixel = 1;
      startbits = 3;
   }
   else
   {
      bitsperpixel = 8;
      startbits = 9;
   }
#endif

   if (gif87a_flag == 1)
   {
      if (fwrite("GIF87a", 6, 1, g_outfile) != 1)
         goto oops;             /* old GIF Signature */
   }
   else
   {
      if (fwrite("GIF89a", 6, 1, g_outfile) != 1)
         goto oops;             /* new GIF Signature */
   }

   width = xdots;
   rowlimit = ydots;
   if (save16bit)
   {
      /* pot16bit info is stored as: file:    double width rows, right side
       * of row is low 8 bits diskvid: ydots rows of colors followed by ydots
       * rows of low 8 bits decoder: returns (row of color info then row of
       * low 8 bits) * ydots */
      rowlimit <<= 1;
      width <<= 1;
   }
   if (write2(&width, 2, 1, g_outfile) != 1)
      goto oops;                /* screen descriptor */
   if (write2(&ydots, 2, 1, g_outfile) != 1)
      goto oops;
   x = (BYTE) (128 + ((6 - 1) << 4) + (bitsperpixel - 1));      /* color resolution == 6
                                                                 * bits worth */
   if (write1(&x, 1, 1, g_outfile) != 1)
      goto oops;
   if (fputc(0, g_outfile) != 0)
      goto oops;                /* background color */
   i = 0;

   if (viewwindow                               /* less than full screen?  */
       && (viewxdots == 0 || viewydots == 0))   /* and we picked the dots? */
      i = (int) (((double) sydots / (double) sxdots) * 64.0 / screenaspect - 14.5);
   else   /* must risk loss of precision if numbers low */
      i = (int) ((((double) ydots / (double) xdots) / finalaspectratio) * 64 - 14.5);
   if (i < 1)
      i = 1;
   if (i > 255)
      i = 255;
   if (gif87a_flag)
      i = 0;                    /* for some decoders which can't handle
                                 * aspect */
   if (fputc(i, g_outfile) != i)
      goto oops;                /* pixel aspect ratio */

#ifndef XFRACT
   if (colors == 256)
   {                            /* write out the 256-color palette */
      if (gotrealdac)
      {                         /* got a DAC - must be a VGA */
         if (!shftwrite((BYTE far *) dacbox, colors))
            goto oops;
#else
   if (colors > 2)
   {
      if (gotrealdac)
      {                         /* got a DAC - must be a VGA */
         if (!shftwrite((BYTE far *) dacbox, 256))
            goto oops;
#endif
      }
      else
      {                         /* uh oh - better fake it */
         for (i = 0; i < 256; i += 16)
            if (!shftwrite((BYTE far *)paletteEGA, 16))
               goto oops;
      }
   }
   if (colors == 2)
   {                            /* write out the B&W palette */
      if (!shftwrite((BYTE far *)paletteBW, colors))
         goto oops;
   }
#ifndef XFRACT
   if (colors == 4)
   {                            /* write out the CGA palette */
      if (!shftwrite((BYTE far *)paletteCGA, colors))
         goto oops;
   }
   if (colors == 16)
   {                            /* Either EGA or VGA */
      if (gotrealdac)
      {
         if (!shftwrite((BYTE *) dacbox, colors))
            goto oops;
      }
      else
      {                         /* no DAC - must be an EGA */
         if (!shftwrite((BYTE far *)paletteEGA, colors))
            goto oops;
      }
   }
#endif

   if (fwrite(",", 1, 1, g_outfile) != 1)
      goto oops;                /* Image Descriptor */
   i = 0;
   if (write2(&i, 2, 1, g_outfile) != 1)
      goto oops;
   if (write2(&i, 2, 1, g_outfile) != 1)
      goto oops;
   if (write2(&width, 2, 1, g_outfile) != 1)
      goto oops;
   if (write2(&ydots, 2, 1, g_outfile) != 1)
      goto oops;
   if (write1(&i, 1, 1, g_outfile) != 1)
      goto oops;

   bitsperpixel = (BYTE) (startbits - 1);

   if (write1(&bitsperpixel, 1, 1, g_outfile) != 1)
      goto oops;

   interrupted = compress(rowlimit);

   if(ferror(g_outfile))
      goto oops;

   if (fputc(0, g_outfile) != 0)
      goto oops;

   if (gif87a_flag == 0)
   {                            /* store non-standard fractal info */
      /* loadfile.c has notes about extension block structure */
      if (interrupted)
         save_info.calc_status = 0;     /* partial save is not resumable */
      save_info.tot_extend_len = 0;
      if (resume_info != 0 && save_info.calc_status == 2)
      {
         /* resume info block, 002 */
         save_info.tot_extend_len += extend_blk_len(resume_len);
         MoveFromMemory((BYTE *)block,(U16)1,(long)resume_len,0,resume_info);
         if (!put_extend_blk(2, resume_len, (char far *)block))
            goto oops;
      }
      if (save_info.fractal_type == FORMULA || save_info.fractal_type == FFORMULA)
         save_info.tot_extend_len += store_item_name(FormName);
      if (save_info.fractal_type == LSYSTEM)
         save_info.tot_extend_len += store_item_name(LName);
      if (save_info.fractal_type == IFS || save_info.fractal_type == IFS3D)
         save_info.tot_extend_len += store_item_name(IFSName);
      if (display3d <= 0 && rangeslen)
      {
         /* ranges block, 004 */
         save_info.tot_extend_len += extend_blk_len(rangeslen * 2);
#ifdef XFRACT
         fix_ranges(ranges, rangeslen, 0);
         put_extend_blk(4, rangeslen * 2, (char far *) ranges);
#else
         if (!put_extend_blk(4, rangeslen * 2, (char far *) ranges))
            goto oops;
#endif
      }
      /* Extended parameters block 005 */
      if (bf_math)
      {
         save_info.tot_extend_len += extend_blk_len(22 * (bflength + 2));
         /* note: this assumes variables allocated in order starting with
          * bfxmin in init_bf2() in BIGNUM.C */
         if (!put_extend_blk(5, 22 * (bflength + 2), (char far *) bfxmin))
            goto oops;
      }

      /* Extended parameters block 006 */
      if (evolving&1)
      {
          struct evolution_info esave_info;
          int i;
          struct evolution_info resume_e_info;
          GENEBASE gene[NUMGENES];
          MoveFromMemory((BYTE *)&gene, (U16)sizeof(gene), 1L, 0L, gene_handle);
          if (evolve_handle == 0) {
             esave_info.paramrangex     = paramrangex;
             esave_info.paramrangey     = paramrangey;
             esave_info.opx             = opx;
             esave_info.opy             = opy;
             esave_info.odpx            = (short)odpx;
             esave_info.odpy            = (short)odpy;
             esave_info.px              = (short)px;
             esave_info.py              = (short)py;
             esave_info.sxoffs          = (short)sxoffs;
             esave_info.syoffs          = (short)syoffs;
             esave_info.xdots           = (short)xdots;
             esave_info.ydots           = (short)ydots;
             esave_info.gridsz          = (short)gridsz;
             esave_info.evolving        = (short)evolving;
             esave_info.this_gen_rseed  = (unsigned short)this_gen_rseed;
             esave_info.fiddlefactor    = fiddlefactor;
             esave_info.ecount          = (short) (gridsz * gridsz); /* flag for done */
          }
          else { /* we will need the resuming information */
             MoveFromMemory((BYTE *)&resume_e_info,(U16)sizeof(resume_e_info),1L,0L,evolve_handle);
             esave_info.paramrangex     = resume_e_info.paramrangex;
             esave_info.paramrangey     = resume_e_info.paramrangey;
             esave_info.opx             = resume_e_info.opx;
             esave_info.opy             = resume_e_info.opy;
             esave_info.odpx            = (short)resume_e_info.odpx;
             esave_info.odpy            = (short)resume_e_info.odpy;
             esave_info.px              = (short)resume_e_info.px;
             esave_info.py              = (short)resume_e_info.py;
             esave_info.sxoffs          = (short)resume_e_info.sxoffs;
             esave_info.syoffs          = (short)resume_e_info.syoffs;
             esave_info.xdots           = (short)resume_e_info.xdots;
             esave_info.ydots           = (short)resume_e_info.ydots;
             esave_info.gridsz          = (short)resume_e_info.gridsz;
             esave_info.evolving        = (short)resume_e_info.evolving;
             esave_info.this_gen_rseed  = (unsigned short)resume_e_info.this_gen_rseed;
             esave_info.fiddlefactor    = resume_e_info.fiddlefactor;
             esave_info.ecount          = resume_e_info.ecount;
          }
          for (i = 0; i < NUMGENES; i++)
             esave_info.mutate[i] = (short)gene[i].mutate;

          for (i = 0; i < sizeof(esave_info.future) / sizeof(short); i++)
             esave_info.future[i] = 0;

          /* some XFRACT logic for the doubles needed here */
#ifdef XFRACT
          decode_evolver_info(&esave_info, 0);
#endif
          /* evolution info block, 006 */
          put_extend_blk(6, sizeof(esave_info), (char far *) &esave_info);
          save_info.tot_extend_len += extend_blk_len(sizeof(esave_info));
      }

      /* main and last block, 001 */
      save_info.tot_extend_len += extend_blk_len(FRACTAL_INFO_SIZE);
#ifdef XFRACT
      decode_fractal_info(&save_info, 0);
#endif
      if (!put_extend_blk(1, FRACTAL_INFO_SIZE, (char far *) &save_info))
      {
         goto oops;
      }
   }

   if (fwrite(";", 1, 1, g_outfile) != 1)
      goto oops;                /* GIF Terminator */

   return (interrupted);

oops:
   {
      static FCODE msg[] = {"Error Writing to disk (Disk full?)"};
      fflush(g_outfile);
      stopmsg(0,msg);
      return 1;
   }
}

/* shift IBM colors to GIF */
static int _fastcall shftwrite(BYTE far * color, int numcolors)
{
   BYTE thiscolor;
   int i, j;
   for (i = 0; i < numcolors; i++)
      for (j = 0; j < 3; j++)
      {
         thiscolor = color[3 * i + j];
         thiscolor = (BYTE) (thiscolor << 2);
         thiscolor = (BYTE) (thiscolor + (BYTE) (thiscolor >> 6));
         if (fputc(thiscolor, g_outfile) != (int) thiscolor)
            return (0);
      }
   return (1);
}

static int _fastcall extend_blk_len(int datalen)
{
   return (datalen + (datalen + 254) / 255 + 15);
   /* data   +     1.per.block   + 14 for id + 1 for null at end  */
}

static int _fastcall put_extend_blk(int block_id, int block_len, char far * block_data)
{
   int i, j;
   char header[15];
   strcpy(header, "!\377\013fractint");
   sprintf(&header[11], "%03u", block_id);
   if (fwrite(header, 14, 1, g_outfile) != 1)
      return (0);
   i = (block_len + 254) / 255;
   while (--i >= 0)
   {
      block_len -= (j = min(block_len, 255));
      if (fputc(j, g_outfile) != j)
         return (0);
      while (--j >= 0)
         fputc(*(block_data++), g_outfile);
   }
   if (fputc(0, g_outfile) != 0)
      return (0);
   return (1);
}

static int _fastcall store_item_name(char *nameptr)
{
   struct formula_info fsave_info;
   int i;
   for (i = 0; i < 40; i++)
      fsave_info.form_name[i] = 0;      /* initialize string */
   strcpy(fsave_info.form_name, nameptr);
   if (fractype == FORMULA || fractype == FFORMULA)
   {
      fsave_info.uses_p1 = (short) uses_p1;
      fsave_info.uses_p2 = (short) uses_p2;
      fsave_info.uses_p3 = (short) uses_p3;
      fsave_info.uses_ismand = (short) uses_ismand;
      fsave_info.ismand = (short) ismand;
   }
   else
   {
      fsave_info.uses_p1 = 0;
      fsave_info.uses_p2 = 0;
      fsave_info.uses_p3 = 0;
      fsave_info.uses_ismand = 0;
      fsave_info.ismand = 0;
   }
   for (i = 0; i < sizeof(fsave_info.future) / sizeof(short); i++)
      fsave_info.future[i] = 0;
   /* formula/lsys/ifs info block, 003 */
   put_extend_blk(3, sizeof(fsave_info), (char far *) &fsave_info);
   return (extend_blk_len(sizeof(fsave_info)));
}

static void _fastcall setup_save_info(struct fractal_info far * save_info)
{
   int i;
   if (fractype != FORMULA && fractype != FFORMULA)
      maxfn = 0;
   /* set save parameters in save structure */
   far_strcpy(save_info->info_id, INFO_ID);
   save_info->version = 13;     /* file version, independent of system */
   /* increment this EVERY time the fractal_info structure changes */

   if (maxit <= SHRT_MAX)
      save_info->iterationsold = (short) maxit;
   else
      save_info->iterationsold = (short) SHRT_MAX;

   save_info->fractal_type = (short) fractype;
   save_info->xmin = xxmin;
   save_info->xmax = xxmax;
   save_info->ymin = yymin;
   save_info->ymax = yymax;
   save_info->creal = param[0];
   save_info->cimag = param[1];
   save_info->videomodeax = (short) videoentry.videomodeax;
   save_info->videomodebx = (short) videoentry.videomodebx;
   save_info->videomodecx = (short) videoentry.videomodecx;
   save_info->videomodedx = (short) videoentry.videomodedx;
   save_info->dotmode = (short) (videoentry.dotmode % 100);
   save_info->xdots = (short) videoentry.xdots;
   save_info->ydots = (short) videoentry.ydots;
   save_info->colors = (short) videoentry.colors;
   save_info->parm3 = 0;        /* pre version==7 fields */
   save_info->parm4 = 0;
   save_info->dparm3 = param[2];
   save_info->dparm4 = param[3];
   save_info->dparm5 = param[4];
   save_info->dparm6 = param[5];
   save_info->dparm7 = param[6];
   save_info->dparm8 = param[7];
   save_info->dparm9 = param[8];
   save_info->dparm10 = param[9];
   save_info->fillcolor = (short) fillcolor;
   save_info->potential[0] = (float) potparam[0];
   save_info->potential[1] = (float) potparam[1];
   save_info->potential[2] = (float) potparam[2];
   save_info->rflag = (short) rflag;
   save_info->rseed = (short) rseed;
   save_info->inside = (short) inside;
   if (LogFlag <= SHRT_MAX)
      save_info->logmapold = (short) LogFlag;
   else
      save_info->logmapold = (short) SHRT_MAX;
   save_info->invert[0] = (float) inversion[0];
   save_info->invert[1] = (float) inversion[1];
   save_info->invert[2] = (float) inversion[2];
   save_info->decomp[0] = (short) decomp[0];
   save_info->biomorph = (short) usr_biomorph;
   save_info->symmetry = (short) forcesymmetry;
   for (i = 0; i < 16; i++)
      save_info->init3d[i] = (short) init3d[i];
   save_info->previewfactor = (short) previewfactor;
   save_info->xtrans = (short) xtrans;
   save_info->ytrans = (short) ytrans;
   save_info->red_crop_left = (short) red_crop_left;
   save_info->red_crop_right = (short) red_crop_right;
   save_info->blue_crop_left = (short) blue_crop_left;
   save_info->blue_crop_right = (short) blue_crop_right;
   save_info->red_bright = (short) red_bright;
   save_info->blue_bright = (short) blue_bright;
   save_info->xadjust = (short) xadjust;
   save_info->yadjust = (short) yadjust;
   save_info->eyeseparation = (short) eyeseparation;
   save_info->glassestype = (short) glassestype;
   save_info->outside = (short) outside;
   save_info->x3rd = xx3rd;
   save_info->y3rd = yy3rd;
   save_info->calc_status = (short) calc_status;
   save_info->stdcalcmode = (char) ((three_pass && stdcalcmode == '3') ? 127 : stdcalcmode);
   if (distest <= 32000)
      save_info->distestold = (short) distest;
   else
      save_info->distestold = 32000;
   save_info->floatflag = floatflag;
   if (bailout >= 4 && bailout <= 32000)
      save_info->bailoutold = (short) bailout;
   else
      save_info->bailoutold = 0;

   save_info->calctime = calctime;
   save_info->trigndx[0] = trigndx[0];
   save_info->trigndx[1] = trigndx[1];
   save_info->trigndx[2] = trigndx[2];
   save_info->trigndx[3] = trigndx[3];
   save_info->finattract = (short) finattract;
   save_info->initorbit[0] = initorbit.x;
   save_info->initorbit[1] = initorbit.y;
   save_info->useinitorbit = useinitorbit;
   save_info->periodicity = (short) periodicitycheck;
   save_info->pot16bit = (short) disk16bit;
   save_info->faspectratio = finalaspectratio;
   save_info->system = (short) save_system;

   if (check_back())
      save_info->release = (short) min(save_release, release);
   else
      save_info->release = (short) release;

   save_info->flag3d = (short) display3d;
   save_info->ambient = (short) Ambient;
   save_info->randomize = (short) RANDOMIZE;
   save_info->haze = (short) haze;
   save_info->transparent[0] = (short) transparent[0];
   save_info->transparent[1] = (short) transparent[1];
   save_info->rotate_lo = (short) rotate_lo;
   save_info->rotate_hi = (short) rotate_hi;
   save_info->distestwidth = (short) distestwidth;
   save_info->mxmaxfp = mxmaxfp;
   save_info->mxminfp = mxminfp;
   save_info->mymaxfp = mymaxfp;
   save_info->myminfp = myminfp;
   save_info->zdots = (short) zdots;
   save_info->originfp = originfp;
   save_info->depthfp = depthfp;
   save_info->heightfp = heightfp;
   save_info->widthfp = widthfp;
   save_info->distfp = distfp;
   save_info->eyesfp = eyesfp;
   save_info->orbittype = (short) neworbittype;
   save_info->juli3Dmode = (short) juli3Dmode;
   save_info->maxfn = maxfn;
   save_info->inversejulia = (short) ((major_method << 8) + minor_method);      /* MVS */
   save_info->bailout = bailout;
   save_info->bailoutest = (short) bailoutest;
   save_info->iterations = maxit;
   save_info->bflength = (short) bnlength;
   save_info->bf_math = (short) bf_math;
   save_info->old_demm_colors = (short) old_demm_colors;
   save_info->logmap = LogFlag;
   save_info->distest = distest;
   save_info->dinvert[0] = inversion[0];
   save_info->dinvert[1] = inversion[1];
   save_info->dinvert[2] = inversion[2];
   save_info->logcalc = (short) Log_Fly_Calc;
   save_info->stoppass = (short) stoppass;
   save_info->quick_calc = (short) quick_calc;
   save_info->closeprox = closeprox;
   for (i = 0; i < sizeof(save_info->future) / sizeof(short); i++)
      save_info->future[i] = 0;
      
}

/***************************************************************************
 *
 *  GIFENCOD.C       - GIF Image compression routines
 *
 *  Lempel-Ziv compression based on 'compress'.  GIF modifications by
 *  David Rowley (mgardi@watdcsu.waterloo.edu). 
 *  Thoroughly massaged by the Stone Soup team for Fractint's purposes.
 * 
 ***************************************************************************/

#define BITSF   12
#define HSIZE  5003            /* 80% occupancy */

/*
 *
 * GIF Image compression - modified 'compress'
 *
 * Based on: compress.c - File compression ala IEEE Computer, June 1984.
 *
 * By Authors:  Spencer W. Thomas       (decvax!harpo!utah-cs!utah-gr!thomas)
 *              Jim McKie               (decvax!mcvax!jim)
 *              Steve Davies            (decvax!vax135!petsd!peora!srd)
 *              Ken Turkowski           (decvax!decwrl!turtlevax!ken)
 *              James A. Woods          (decvax!ihnp4!ames!jaw)
 *              Joe Orost               (decvax!vax135!petsd!joe)
 *
 */

/* prototypes */

static void _fastcall output(int code);
static void _fastcall char_out(int c);
static void _fastcall flush_char(void);
static void _fastcall cl_block(void);

static int n_bits;                        /* number of bits/code */
static int maxbits = BITSF;                /* user settable max # bits/code */
static int maxcode;                  /* maximum code, given n_bits */
static int maxmaxcode = (int)1 << BITSF; /* should NEVER generate this code */
# define MAXCODE(n_bits)        (((int) 1 << (n_bits)) - 1)

#ifdef XFRACT
unsigned int strlocn[10240];
BYTE block[4096];
#endif

static long far *htab;
static unsigned short *codetab = (unsigned short *)strlocn;

/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type
 * as the codetab.  The tab_suffix table needs 2**BITSF characters.  We
 * get this from the beginning of htab.  The output stack uses the rest
 * of htab, and contains characters.  There is plenty of room for any
 * possible stack (stack used to be 8000 characters).
 */

#define tab_prefixof(i)   codetab[i]
#define tab_suffixof(i)   ((char_type far *)(htab))[i]
#define de_stack          ((char_type far *)&tab_suffixof((int)1<<BITSF))

static int free_ent;                  /* first unused entry */

/*
 * block compression parameters -- after all codes are used up,
 * and compression rate changes, start over.
 */
static int clear_flg = 0;

/*
 * compress stdin to stdout
 *
 * Algorithm:  use open addressing double hashing (no chaining) on the 
 * prefix code / next character combination.  We do a variant of Knuth's
 * algorithm D (vol. 3, sec. 6.4) along with G. Knott's relatively-prime
 * secondary probe.  Here, the modular division first probe is gives way
 * to a faster exclusive-or manipulation.  Also do block compression with
 * an adaptive reset, whereby the code table is cleared when the compression
 * ratio decreases, but after the table fills.  The variable-length output
 * codes are re-sized at this point, and a special CLEAR code is generated
 * for the decompressor.  Late addition:  construct the table according to
 * file size for noticeable speed improvement on small files.  Please direct
 * questions about this implementation to ames!jaw.
 */

static int ClearCode;
static int EOFCode;
static int a_count; /* Number of characters so far in this 'packet' */
static unsigned long cur_accum = 0;
static int  cur_bits = 0;

/*
 * Define the storage for the packet accumulator
 */
static char *accum; /* 256 bytes */

static int compress(int rowlimit)
{
   int outcolor1, outcolor2;
   long fcode;
   int i = 0;
   int ent;
   int disp;
   int hsize_reg;
   int hshift;
   int ydot, xdot, color;
   int rownum;
   int in_count = 0;
   int interrupted = 0;
   int tempkey;
   char accum_stack[256];
   accum = accum_stack;
   htab = (long far *)MK_FP(extraseg,0);
   
   outcolor1 = 0;               /* use these colors to show progress */
   outcolor2 = 1;               /* (this has nothing to do with GIF) */

   if (colors > 2)
   {
      outcolor1 = 2;
      outcolor2 = 3;
   }
   if (((++numsaves) & 1) == 0)
   {                            /* reverse the colors on alt saves */
      i = outcolor1;
      outcolor1 = outcolor2;
      outcolor2 = i;
   }
   outcolor1s = outcolor1;
   outcolor2s = outcolor2;

   /*
    * Set up the necessary values
    */
   cur_accum = 0;
   cur_bits = 0;
   clear_flg = 0;
   ydot = 0;
   ent = 0;
   maxcode = MAXCODE(n_bits = startbits);

   ClearCode = (1 << (startbits - 1));
   EOFCode = ClearCode + 1;
   free_ent = ClearCode + 2;

   a_count = 0;
   hshift = 0;
   for (fcode = (long) HSIZE;  fcode < 65536L; fcode *= 2L)
       hshift++;
   hshift = 8 - hshift;                /* set hash code range bound */

   far_memset(htab,0xff,(unsigned)HSIZE*sizeof(long));
   hsize_reg = HSIZE;

   output((int)ClearCode);

   for (rownum = 0; rownum < ydots; rownum++)
   {                            /* scan through the dots */
      for (ydot = rownum; ydot < rowlimit; ydot += ydots)
      {
         for (xdot = 0; xdot < xdots; xdot++)
         {
            if (save16bit == 0 || ydot < ydots)
               color = getcolor(xdot, ydot);
            else
               color = readdisk(xdot + sxoffs, ydot + syoffs);
            if(in_count == 0)
            {
               in_count = 1;
               ent = color;
               continue;
            }
            fcode = (long) (((long) color << maxbits) + ent);
            i = (((int)color << hshift) ^ ent);    /* xor hashing */
     
            if (htab[i] == fcode) 
            {
                ent = codetab[i];
                continue;
            } else if ((long)htab[i] < 0)      /* empty slot */
                goto nomatch;
            disp = hsize_reg - i;           /* secondary hash (after G. Knott) */
            if (i == 0)
                disp = 1;
probe:
            if ((i -= disp) < 0)
               i += hsize_reg;
     
            if (htab[i] == fcode) 
            {
                ent = codetab[i];
                continue;
            }
            if ((long)htab[i] > 0) 
                goto probe;
nomatch:
            output ((int) ent);
            ent = color;
            if (free_ent < maxmaxcode) 
            {
                /* code -> hashtable */
                codetab[i] = (unsigned short)free_ent++; 
                htab[i] = fcode;
            } 
            else
              cl_block();
         } /* end for xdot */
         if (dotmode != 11      /* supress this on disk-video */
             && active_system == 0      /* and in Windows version     */
             && ydot == rownum)
         {
            if ((ydot & 4) == 0)
            {
               if (++outcolor1 >= colors)
                  outcolor1 = 0;
               if (++outcolor2 >= colors)
                  outcolor2 = 0;
            }
            for (i = 0; 250 * i < xdots; i++)
            {  /* display vert status bars */
               /* (this is NOT GIF-related)  */
               putcolor(i, ydot, getcolor(i, ydot) ^ outcolor1);
               putcolor(xdots - 1 - i, ydot, 
                  getcolor(xdots - 1 - i, ydot) ^ outcolor2);
            }
            last_colorbar = ydot;
         } /* end if dotmode != 11 */
         tempkey = keypressed();
         if (tempkey && (tempkey != (int)'s'))  /* keyboard hit - bail out */
         {
            interrupted = 1;
            rownum = ydots;
            break;
         }
         if (tempkey == (int)'s')
            getakey();   /* eat the keystroke */
      } /* end for ydot */
   } /* end for rownum */
   
   /*
    * Put out the final code.
    */
   output((int)ent);
   output((int) EOFCode);
   return (interrupted);
}

/*****************************************************************
 * TAG(output)
 *
 * Output the given code.
 * Inputs:
 *      code:   A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *              that n_bits =< (long)wordsize - 1.
 * Outputs:
 *      Outputs code to the file.
 * Assumptions:
 *      Chars are 8 bits long.
 * Algorithm:
 *      Maintain a BITSF character long buffer (so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */


static void _fastcall output(int code)
{
   static ULFCODE masks[] = 
      { 0x0000, 0x0001, 0x0003, 0x0007, 0x000F,
                0x001F, 0x003F, 0x007F, 0x00FF,
                0x01FF, 0x03FF, 0x07FF, 0x0FFF,
                0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF };
                
   cur_accum &= masks[ cur_bits ];

   if(cur_bits > 0)
      cur_accum |= ((long)code << cur_bits);
   else
      cur_accum = code;
   
   cur_bits += n_bits;

   while(cur_bits >= 8) 
   {
      char_out((unsigned int)(cur_accum & 0xff));
      cur_accum >>= 8;
      cur_bits -= 8;
   }

   /*
    * If the next entry is going to be too big for the code size,
    * then increase it, if possible.
    */
   if (free_ent > maxcode || clear_flg) 
   {
      if(clear_flg) 
      {
         maxcode = MAXCODE (n_bits = startbits);
         clear_flg = 0;
      
      } 
      else 
      {
         n_bits++;
         if (n_bits == maxbits)
            maxcode = maxmaxcode;
         else
            maxcode = MAXCODE(n_bits);
      }
   }
   
   if(code == EOFCode) 
   {
      /*
       * At EOF, write the rest of the buffer.
       */
       while(cur_bits > 0) 
       {
          char_out((unsigned int)(cur_accum & 0xff));
          cur_accum >>= 8;
          cur_bits -= 8;
       }

       flush_char();
  
       fflush(g_outfile);
   }
}

/*
 * Clear out the hash table
 */
static void _fastcall cl_block(void)             /* table clear for block compress */
{
        far_memset(htab,0xff,(unsigned)HSIZE*sizeof(long));
        free_ent = ClearCode + 2;
        clear_flg = 1;
        output((int)ClearCode);
}

/*
 * Add a character to the end of the current packet, and if it is 254
 * characters, flush the packet to disk.
 */
static void _fastcall char_out(int c)
{
   accum[ a_count++ ] = (char)c;
   if(a_count >= 254) 
      flush_char();
}

/*
 * Flush the packet to disk, and reset the accumulator
 */
static void _fastcall flush_char(void)
{
   if(a_count > 0) {
      fputc(a_count, g_outfile);
      fwrite(accum, 1, a_count, g_outfile);
      a_count = 0;
   }
}  
