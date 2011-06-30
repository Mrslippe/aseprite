/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "app.h"
#include "document.h"
#include "file/file.h"
#include "file/file_format.h"
#include "file/format_options.h"
#include "ini_file.h"
#include "raster/raster.h"

#include <stdio.h>
#include <stdlib.h>

#define PNG_NO_TYPECAST_NULL
#include "png.h"

class PngFormat : public FileFormat
{
  const char* onGetName() const { return "png"; }
  const char* onGetExtensions() const { return "png"; }
  int onGetFlags() const {
    return
      FILE_SUPPORT_LOAD |
      FILE_SUPPORT_SAVE |
      FILE_SUPPORT_RGB |
      FILE_SUPPORT_RGBA |
      FILE_SUPPORT_GRAY |
      FILE_SUPPORT_GRAYA |
      FILE_SUPPORT_INDEXED |
      FILE_SUPPORT_SEQUENCES;
  }

  bool onLoad(FileOp* fop);
  bool onSave(FileOp* fop);
};

FileFormat* CreatePngFormat()
{
  return new PngFormat;
}

static void report_png_error(png_structp png_ptr, png_const_charp error)
{
  fop_error((FileOp *)png_ptr->error_ptr, "libpng: %s\n", error);
}

bool PngFormat::onLoad(FileOp* fop)
{
  png_uint_32 width, height, y;
  unsigned int sig_read = 0;
  png_structp png_ptr;
  png_infop info_ptr;
  int bit_depth, color_type, interlace_type;
  int pass, number_passes;
  int num_palette;
  png_colorp palette;
  png_bytep row_pointer;
  Image *image;
  int imgtype;
  FILE *fp;

  fp = fopen(fop->filename.c_str(), "rb");
  if (!fp)
    return false;

  /* Create and initialize the png_struct with the desired error handler
   * functions.  If you want to use the default stderr and longjump method,
   * you can supply NULL for the last three parameters.  We also supply the
   * the compiler header file version, so that we know if the application
   * was compiled with a compatible version of the library
   */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)fop,
 				   report_png_error, report_png_error);
  if (png_ptr == NULL) {
    fop_error(fop, "png_create_read_struct\n");
    fclose(fp);
    return false;
  }

  /* Allocate/initialize the memory for image information. */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    fop_error(fop, "png_create_info_struct\n");
    fclose(fp);
    png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
    return false;
  }

  /* Set error handling if you are using the setjmp/longjmp method (this is
   * the normal method of doing things with libpng).
   */
  if (setjmp(png_jmpbuf(png_ptr))) {
    fop_error(fop, "Error reading PNG file\n");
    /* Free all of the memory associated with the png_ptr and info_ptr */
    png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
    fclose(fp);
    /* If we get here, we had a problem reading the file */
    return false;
  }

  /* Set up the input control if you are using standard C streams */
  png_init_io(png_ptr, fp);

  /* If we have already read some of the signature */
  png_set_sig_bytes(png_ptr, sig_read);

  /* The call to png_read_info() gives us all of the information from the
   * PNG file before the first IDAT (image data chunk).
   */
  png_read_info(png_ptr, info_ptr);
  
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
	       &interlace_type, int_p_NULL, int_p_NULL);

  
  /* Set up the data transformations you want.  Note that these are all
   * optional.  Only call them if you want/need them.  Many of the
   * transformations only work on specific types of images, and many
   * are mutually exclusive.
   */

  /* tell libpng to strip 16 bit/color files down to 8 bits/color */
  png_set_strip_16(png_ptr);

  /* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
   * byte into separate bytes (useful for paletted and grayscale images).
   */
  png_set_packing(png_ptr);

  /* Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel */
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_gray_1_2_4_to_8(png_ptr);

  /* Turn on interlace handling.  REQUIRED if you are not using
   * png_read_image().  To see how to handle interlacing passes,
   * see the png_read_row() method below:
   */
  number_passes = png_set_interlace_handling(png_ptr);

  /* Optional call to gamma correct and add the background to the palette
   * and update info structure.
   */
  png_read_update_info(png_ptr, info_ptr);

  /* create the output image */
  switch (info_ptr->color_type) {

    case PNG_COLOR_TYPE_RGB_ALPHA:
      fop->seq.has_alpha = true;
    case PNG_COLOR_TYPE_RGB: 
      imgtype = IMAGE_RGB;
      break;

    case PNG_COLOR_TYPE_GRAY_ALPHA:
      fop->seq.has_alpha = true;
    case PNG_COLOR_TYPE_GRAY:
      imgtype = IMAGE_GRAYSCALE;
      break;

    case PNG_COLOR_TYPE_PALETTE:
      imgtype = IMAGE_INDEXED;
      break;

    default:
      fop_error(fop, "Color type not supported\n)");
      png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
      fclose(fp);
      return false;
  }
  
  image = fop_sequence_image(fop, imgtype, info_ptr->width, info_ptr->height);
  if (!image) {
    fop_error(fop, "file_sequence_image %dx%d\n", info_ptr->width, info_ptr->height);
    png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
    fclose(fp);
    return false;
  }

  // Transparent palette entries
  std::vector<bool> trans_entries(256, false);

  // Read the palette
  if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE &&
      png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette)) {
    int c;

    for (c = 0; c < num_palette; c++) {
      fop_sequence_set_color(fop, c,
			     palette[c].red,
			     palette[c].green,
			     palette[c].blue);
    }
    for (; c < 256; c++) {
      fop_sequence_set_color(fop, c, 0, 0, 0);
    }

    // Read transparent entries
    png_bytep trans = NULL;	// Transparent palette entries
    int num_trans = 0;

    png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, NULL);

    for (int i = 0; i < num_trans; ++i) {
      int j = trans[i];
      if (j >= 0 && j < (int)trans_entries.size()) {
	trans_entries[j] = true;
	fop->seq.has_alpha = true; // Is a transparent sprite
      }
    }
  }

  /* Allocate the memory to hold the image using the fields of info_ptr. */

   /* The easiest way to read the image: */
  row_pointer = (png_bytep)png_malloc(png_ptr, png_get_rowbytes(png_ptr, info_ptr));
  for (pass = 0; pass < number_passes; pass++) {
    for (y = 0; y < height; y++) {
      /* read the line */
      png_read_row(png_ptr, row_pointer, (png_byte*)NULL);

      /* RGB_ALPHA */
      if (info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
	register uint8_t* src_address = row_pointer;
	register uint32_t* dst_address = ((uint32_t**)image->line)[y];
	register unsigned int x, r, g, b, a;

	for (x=0; x<width; x++) {
	  r = *(src_address++);
	  g = *(src_address++);
	  b = *(src_address++);
	  a = *(src_address++);
	  *(dst_address++) = _rgba(r, g, b, a);
	}
      }
      /* RGB */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_RGB) {
	register uint8_t* src_address = row_pointer;
	register uint32_t* dst_address = ((uint32_t**)image->line)[y];
	register unsigned int x, r, g, b;

	for (x=0; x<width; x++) {
	  r = *(src_address++);
	  g = *(src_address++);
	  b = *(src_address++);
	  *(dst_address++) = _rgba(r, g, b, 255);
	}
      }
      /* GRAY_ALPHA */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
	register uint8_t* src_address = row_pointer;
	register uint16_t* dst_address = ((uint16_t**)image->line)[y];
	register unsigned int x, k, a;

	for (x=0; x<width; x++) {
	  k = *(src_address++);
	  a = *(src_address++);
	  *(dst_address++) = _graya(k, a);
	}
      }
      /* GRAY */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY) {
	register uint8_t* src_address = row_pointer;
	register uint16_t* dst_address = ((uint16_t**)image->line)[y];
	register unsigned int x, k;

	for (x=0; x<width; x++) {
	  k = *(src_address++);
	  *(dst_address++) = _graya(k, 255);
	}
      }
      /* PALETTE */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
	register uint8_t* src_address = row_pointer;
	register uint8_t* dst_address = ((uint8_t**)image->line)[y];
	register unsigned int x, c;

	for (x=0; x<width; x++) {
	  c = *(src_address++);

	  // All transparent values are converted to entry 0.
	  // TODO Add support for multiple transparent palette entries in Indexed sprites.
	  if (!trans_entries.empty() && trans_entries[c]) {
	    *(dst_address++) = 0;
	  }
	  else {
	    *(dst_address++) = c;
	  }
	}
      }

      fop_progress(fop,
		   (float)((float)pass + (float)(y+1) / (float)(height))
		   / (float)number_passes);

      if (fop_is_stop(fop))
	break;
    }
  }
  png_free(png_ptr, row_pointer);

  /* clean up after the read, and free any memory allocated */
  png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);

  /* close the file */
  fclose(fp);
  return true;
}

bool PngFormat::onSave(FileOp* fop)
{
  Image *image = fop->seq.image;
  png_uint_32 width, height, y;
  png_structp png_ptr;
  png_infop info_ptr;
  png_colorp palette = NULL;
  png_bytep row_pointer;
  int color_type = 0;
  int pass, number_passes;
  FILE *fp;

  /* open the file */
  fp = fopen(fop->filename.c_str(), "wb");
  if (fp == NULL)
    return false;

  /* Create and initialize the png_struct with the desired error handler
   * functions.  If you want to use the default stderr and longjump method,
   * you can supply NULL for the last three parameters.  We also check that
   * the library version is compatible with the one used at compile time,
   * in case we are using dynamically linked libraries.  REQUIRED.
   */
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)fop,
				    report_png_error, report_png_error);
  if (png_ptr == NULL) {
    fclose(fp);
    return false;
  }

  /* Allocate/initialize the image information data.  REQUIRED */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    fclose(fp);
    png_destroy_write_struct(&png_ptr, png_infopp_NULL);
    return false;
  }

  /* Set error handling.  REQUIRED if you aren't supplying your own
   * error handling functions in the png_create_write_struct() call.
   */
  if (setjmp(png_jmpbuf(png_ptr))) {
    /* If we get here, we had a problem reading the file */
    fclose(fp);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return false;
  }

  /* set up the output control if you are using standard C streams */
  png_init_io(png_ptr, fp);

  /* Set the image information here.  Width and height are up to 2^31,
   * bit_depth is one of 1, 2, 4, 8, or 16, but valid values also depend on
   * the color_type selected. color_type is one of PNG_COLOR_TYPE_GRAY,
   * PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_PALETTE, PNG_COLOR_TYPE_RGB,
   * or PNG_COLOR_TYPE_RGB_ALPHA.  interlace is either PNG_INTERLACE_NONE or
   * PNG_INTERLACE_ADAM7, and the compression_type and filter_type MUST
   * currently be PNG_COMPRESSION_TYPE_BASE and PNG_FILTER_TYPE_BASE. REQUIRED
   */
  width = image->w;
  height = image->h;

  switch (image->imgtype) {
    case IMAGE_RGB:
      color_type = fop->document->getSprite()->needAlpha() ?
	PNG_COLOR_TYPE_RGB_ALPHA:
	PNG_COLOR_TYPE_RGB;
      break;
    case IMAGE_GRAYSCALE:
      color_type = fop->document->getSprite()->needAlpha() ?
	PNG_COLOR_TYPE_GRAY_ALPHA:
	PNG_COLOR_TYPE_GRAY;
      break;
    case IMAGE_INDEXED:
      color_type = PNG_COLOR_TYPE_PALETTE;
      break;
  }

  png_set_IHDR(png_ptr, info_ptr, width, height, 8, color_type,
	       PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  if (image->imgtype == IMAGE_INDEXED) {
    int c, r, g, b;

#if PNG_MAX_PALETTE_LENGTH != 256
#error PNG_MAX_PALETTE_LENGTH should be 256
#endif

    /* set the palette if there is one.  REQUIRED for indexed-color images */
    palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH
					      * png_sizeof(png_color));
    /* ... set palette colors ... */
    for (c = 0; c < PNG_MAX_PALETTE_LENGTH; c++) {
      fop_sequence_get_color(fop, c, &r, &g, &b);
      palette[c].red   = r;
      palette[c].green = g;
      palette[c].blue  = b;
    }

    png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

    // Index 0 will be the transparent color in the PNG file (only if
    // the sprite does not have a background layer).
    if (fop->document->getSprite()->getBackgroundLayer() == NULL) {
      png_bytep trans = (png_bytep)png_malloc(png_ptr, 1);
      trans[0] = 0;		// Entry 0 is transparent

      png_set_tRNS(png_ptr, info_ptr, trans, 1, NULL);
      png_free(png_ptr, trans);
    }
  }

  /* Write the file header information. */
  png_write_info(png_ptr, info_ptr);

  /* pack pixels into bytes */
  png_set_packing(png_ptr);

  /* non-interlaced */
  number_passes = 1;

  row_pointer = (png_bytep)png_malloc(png_ptr, png_get_rowbytes(png_ptr, info_ptr));

  /* The number of passes is either 1 for non-interlaced images,
   * or 7 for interlaced images.
   */
  for (pass = 0; pass < number_passes; pass++) {
    /* If you are only writing one row at a time, this works */
    for (y = 0; y < height; y++) {
      /* RGB_ALPHA */
      if (info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
	register uint32_t* src_address = ((uint32_t**)image->line)[y];
	register uint8_t* dst_address = row_pointer;
	register unsigned int x, c;

	for (x=0; x<width; x++) {
	  c = *(src_address++);
	  *(dst_address++) = _rgba_getr(c);
	  *(dst_address++) = _rgba_getg(c);
	  *(dst_address++) = _rgba_getb(c);
	  *(dst_address++) = _rgba_geta(c);
	}
      }
      /* RGB */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_RGB) {
	register uint32_t* src_address = ((uint32_t**)image->line)[y];
	register uint8_t* dst_address = row_pointer;
	register unsigned int x, c;

	for (x=0; x<width; x++) {
	  c = *(src_address++);
	  *(dst_address++) = _rgba_getr(c);
	  *(dst_address++) = _rgba_getg(c);
	  *(dst_address++) = _rgba_getb(c);
	}
      }
      /* GRAY_ALPHA */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
	register uint16_t* src_address = ((uint16_t**)image->line)[y];
	register uint8_t* dst_address = row_pointer;
	register unsigned int x, c;

	for (x=0; x<width; x++) {
	  c = *(src_address++);
	  *(dst_address++) = _graya_getv(c);
	  *(dst_address++) = _graya_geta(c);
	}
      }
      /* GRAY */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY) {
	register uint16_t* src_address = ((uint16_t**)image->line)[y];
	register uint8_t* dst_address = row_pointer;
	register unsigned int x, c;

	for (x=0; x<width; x++) {
	  c = *(src_address++);
	  *(dst_address++) = _graya_getv(c);
	}
      }
      /* PALETTE */
      else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
	register uint8_t* src_address = ((uint8_t**)image->line)[y];
	register uint8_t* dst_address = row_pointer;
	register unsigned int x;

	for (x=0; x<width; x++)
	  *(dst_address++) = *(src_address++);
      }

      /* write the line */
      png_write_rows(png_ptr, &row_pointer, 1);

      fop_progress(fop,
		   (float)((float)pass + (float)(y+1) / (float)(height))
		   / (float)number_passes);
    }
  }

  png_free(png_ptr, row_pointer);

  /* It is REQUIRED to call this to finish writing the rest of the file */
  png_write_end(png_ptr, info_ptr);

  /* If you png_malloced a palette, free it here (don't free info_ptr->palette,
     as recommended in versions 1.0.5m and earlier of this example; if
     libpng mallocs info_ptr->palette, libpng will free it).  If you
     allocated it with malloc() instead of png_malloc(), use free() instead
     of png_free(). */
  if (image->imgtype == IMAGE_INDEXED) {
    png_free(png_ptr, palette);
    palette = NULL;
  }

  /* clean up after the write, and free any memory allocated */
  png_destroy_write_struct(&png_ptr, &info_ptr);

  /* close the file */
  fclose(fp);

  /* all right */
  return true;
}
