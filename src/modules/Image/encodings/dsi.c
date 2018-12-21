/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

/*! @module Image
 *!
 *! @module DSI
 *!
 *! Decode-only support for the Dream SNES image file format.
 *!
 *! This is a little-endian 16 bitplane image format that starts with
 *! two 32-bit integers, width and height, followed by w*h*2 bytes of
 *! image data.
 *!
 *! Each pixel is r5g6b5, a special case is the color r=31,g=0,b=31
 *! (full red, full blue, no green), which is transparent
 *! (chroma-keying)
 */
/* Dream SNES Image file */

#include "global.h"
#include "image_machine.h"

#include "object.h"
#include "interpret.h"
#include "svalue.h"
#include "mapping.h"
#include "pike_error.h"
#include "builtin_functions.h"
#include "operators.h"
#include "bignum.h"

#include "image.h"
#include "colortable.h"


#define sp Pike_sp

extern struct program *image_program;

/*! @decl mapping(string:Image.Image) _decode(string data)
 *!  Decode the DSI image.
 *!
 *! This function will treat pixels with full red, full blue, no green
 *! as transparent.
 */
static void f__decode( INT32 args )
{
  int xs, ys, x, y;
  unsigned char *data, *dp;
  size_t len;
  struct object *i, *a;
  struct image *ip, *ap;
  rgb_group black = {0,0,0};
  if( TYPEOF(sp[-args]) != T_STRING )
    Pike_error("Illegal argument 1 to Image.DSI._decode\n");
  data = (unsigned char *)sp[-args].u.string->str;
  len = (size_t)sp[-args].u.string->len;

  if( len < 10 ) Pike_error("Data too short\n");

  xs = data[0] | (data[1]<<8) | (data[2]<<16) | (data[3]<<24);
  ys = data[4] | (data[5]<<8) | (data[6]<<16) | (data[7]<<24);

  if( (xs * ys * 2) != (ptrdiff_t)(len-8) ||
      INT32_MUL_OVERFLOW(xs, ys) || ((xs * ys) & -0x8000000) )
    Pike_error("Not a DSI %d * %d + 8 != %ld\n",
          xs, ys, (long)len);

  push_int( xs );
  push_int( ys );
  push_int( 255 );
  push_int( 255 );
  push_int( 255 );
  a = clone_object( image_program, 5 );
  push_int( xs );
  push_int( ys );
  i = clone_object( image_program, 2 );
  ip = (struct image *)i->storage;
  ap = (struct image *)a->storage;

  dp = data+8;
  for( y = 0; y<ys; y++ )
    for( x = 0; x<xs; x++,dp+=2 )
    {
      unsigned short px = dp[0] | (dp[1]<<8);
      int r, g, b;
      rgb_group p;
      if( px == ((31<<11) | 31) )
        ap->img[ x + y*xs ] = black;
      else
      {
        r = ((px>>11) & 31);
        g = ((px>>5) & 63);
        b = ((px) & 31);
        p.r = (r*255)/31;
        p.g = (g*255)/63;
        p.b = (b*255)/31;
        ip->img[ x + y*xs ] = p;
      }
    }

  push_static_text( "image" );
  push_object( i );
  push_static_text( "alpha" );
  push_object( a );
  f_aggregate_mapping( 4 );
}

/*! @decl Image.Image decode(string data)
 *!  Decode the DSI image, without alpha decoding.
 */
static void f_decode( INT32 args )
{
  f__decode( args );
  push_static_text( "image" );
  f_index( 2 );
}

void init_image_dsi(void)
{
  ADD_FUNCTION("_decode", f__decode, tFunc(tStr,tMapping), 0);
  ADD_FUNCTION("decode", f_decode, tFunc(tStr,tObj), 0);
}


void exit_image_dsi(void)
{
}
/*! @endmodule
 *!
 *! @endmodule
 */
