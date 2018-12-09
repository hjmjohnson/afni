/*
 * DO NOT EDIT THIS FILE! It was created automatically by m4.
 */

/*
 * Copyright (c) 1994 The Board of Trustees of The Leland Stanford
 * Junior University.  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice and this permission notice appear in
 * all copies of this software and that you do not sell the software.
 * Commercial licensing is available by contacting the author.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author:
 *    Phil Lacroute
 *    Computer Systems Laboratory
 *    Electrical Engineering Dept.
 *    Stanford University
 */
/*
 * vp_warpA.m4
 *
 * One-pass image warping routine for affine transformations.
 *
 * Copyright (c) 1994 The Board of Trustees of The Leland Stanford
 * Junior University.  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice and this permission notice appear in
 * all copies of this software and that you do not sell the software.
 * Commercial licensing is available by contacting the author.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author:
 *    Phil Lacroute
 *    Computer Systems Laboratory
 *    Electrical Engineering Dept.
 *    Stanford University
 */

/*
 * $Date$
 * $Revision$
 */

#include "vp_global.h"














































/* convert a float in the interval [0-1) to a 31-bit fixed point */
#define FLTFRAC_TO_FIX31(f)	((int)((f) * 2147483648.))

/* convert a 31-bit fixed point to a weight table index */
#define FIX31_TO_WGTIND(f)	((f) >> (31 - WARP_WEIGHT_INDEX_BITS))

extern float VPBilirpWeight[WARP_WEIGHT_ENTRIES][WARP_WEIGHT_ENTRIES][4];

/*
 * VPWarpA301N
 *
 * One-pass warper.  Transforms in_image to out_image according to
 * the affine warp specified by warp_matrix.  The resampling filter
 * is a bilirp (suitable for upsampling only).
 */

void
VPWarpA301N (in_image, in_width, in_height, in_bytes_per_scan,
	  out_image, out_width, out_height, out_bytes_per_scan,
	  warp_matrix)
RGBIntPixel *in_image;		/* input image data */
int in_width;			/* size of input image */
int in_height;
int in_bytes_per_scan;		/* bytes per scanline in input image */
char *out_image;		/* output image data */
int out_width;			/* size of output image */
int out_height;
int out_bytes_per_scan;		/* bytes per scanline in output image */
vpMatrix3 warp_matrix;		/* [ outx ]                 [ inx ] */
				/* [ outy ] = warp_matrix * [ iny ] */
				/* [   1  ]                 [  1  ] */
{
    Trapezoid full_overlap[9];	/* description of the area of overlap
				   of output image (shrunk by the size
				   of the filter kernel) with input image */
    Trapezoid part_overlap[9];	/* description of the area of overlap
				   of output image (unlarged by the size
				   of the filter kernel) with input image */
    int region;			/* index into full/part_overlap */
    char *out_ptr;		/* pointer to current pixel of output image */
    int out_scan_y;		/* coordinate of current output scanline */
    int scans_to_next_vertex;	/* number of scans left to process before
				   the next vertex is reached */
    RGBIntPixel *in_ptr;	/* pointer to current pixel of input image */
    double x_lft_full, x_rgt_full; /* intersection of scan with full_overlap */
    double x_lft_part, x_rgt_part; /* intersection of scan with part_overlap */
    int no_full_pixels;		/* true if full_overlap is empty for scan */
    double in_x, in_y;		/* exact coordinates in the input image of
				   the current output image pixel */
    int in_x_int, in_y_int;	/* coordinates of the nearest input image
				   pixel to the upper-left of the current
				   output image pixel */
    int xfrac, yfrac;		/* in_x - in_x_int and in_y - in_y_int,
				   stored as a fixed-point number with 31 bits
				   of fraction */
    int xfrac_incr, yfrac_incr;	/* increments to xfrac and yfrac to give
				   the fractions for the next output image
				   pixel in the current scan */
    double in_x_incr, in_y_incr;/* increments to in_x and in_y to give the
				   input image coordinates of the next
				   output image pixel in the current scan
				   (equal to dx_in/dx_out and dy_in/dx_out) */
    int in_x_incr_int, in_y_incr_int; /* integer part of in_x/y_incr */
    int in_x_incr_dlt, in_y_incr_dlt; /* sign of in_x/y_incr */
    float *wptr;		/* pointer into weight table */
    int lft_zero_cnt;		/* # zero pixels on left edge of scan */
    int lft_edge_cnt;		/* # pixels on left w/ part filter overlap */
    int full_cnt;		/* # pixels w/ full filter overlap */
    int rgt_edge_cnt;		/* # pixels on rgt w/ part filter overlap */
    int rgt_zero_cnt;		/* # zero pixels on right edge of scan */
    int x;			/* pixel index */


	float opc_acc; int opc_acc_int;		/* pixel accumulator */
    double denom;
    int c;

#ifdef DEBUG
    {
	int y;

	for (y = 0; y < out_height; y++) {
	    out_ptr = out_image + y*out_bytes_per_scan;
	    for (x = 0; x < out_width; x++) {
		for (c = 0; c < ((0) + (1)); c++)
		    *out_ptr++ = 255;
	    }
	}
    }
#endif

    /* initialize tables */
    VPComputeWarpTables();

    /* compute the intersection of the input image and the output image */
    /* filter width = 2.0 in input image space (triangle filter) */
    VPAffineImageOverlap(in_width, in_height, out_width, out_height,
			 warp_matrix, 2., full_overlap, part_overlap);

    /* compute the output image */
    out_ptr = out_image;
    out_scan_y = 0;
    denom = 1. / (warp_matrix[0][0] * warp_matrix[1][1] -
		  warp_matrix[0][1] * warp_matrix[1][0]);
    in_x_incr = warp_matrix[1][1]*denom;
    in_y_incr = -warp_matrix[1][0]*denom;
    if (in_x_incr < 0) {
	in_x_incr_int = (int)ceil(in_x_incr);
	in_x_incr_dlt = -1;
    } else {
	in_x_incr_int = (int)floor(in_x_incr);
	in_x_incr_dlt = 1;
    }
    if (in_y_incr < 0) {
	in_y_incr_int = (int)ceil(in_y_incr);
	in_y_incr_dlt = -1;
    } else {
	in_y_incr_int = (int)floor(in_y_incr);
	in_y_incr_dlt = 1;
    }
    xfrac_incr = FLTFRAC_TO_FIX31(in_x_incr - in_x_incr_int);
    yfrac_incr = FLTFRAC_TO_FIX31(in_y_incr - in_y_incr_int);
    for (region = 0; region < 9; region++) {
	/* check for empty region */
	if (part_overlap[region].miny >= out_height) {
	    break;
	}

	/* check if this region of the output image is unaffected by
	   the input image */
	if (part_overlap[region].x_top_lft >
	    part_overlap[region].x_top_rgt) {
	    c = (part_overlap[region].maxy - part_overlap[region].miny + 1) *
		out_bytes_per_scan;
	    bzero(out_ptr, c);
	    out_ptr += c;
	    out_scan_y += part_overlap[region].maxy -
			  part_overlap[region].miny + 1;
	    continue;
	}

	/* process scanlines of this region */
	scans_to_next_vertex = part_overlap[region].maxy -
			       part_overlap[region].miny + 1;
	x_lft_full = full_overlap[region].x_top_lft;
	x_rgt_full = full_overlap[region].x_top_rgt;
	x_lft_part = part_overlap[region].x_top_lft;
	x_rgt_part = part_overlap[region].x_top_rgt;
	if (x_lft_full > x_rgt_full)
	    no_full_pixels = 1;
	else
	    no_full_pixels = 0;
	ASSERT(scans_to_next_vertex > 0);
	ASSERT(out_scan_y == part_overlap[region].miny);
	while (scans_to_next_vertex > 0) {
	    /* compute the portions of the scanline which are zero
	       and which intersect the full and partially-full regions */
	    lft_zero_cnt = (int)floor(x_lft_part);
	    if (lft_zero_cnt < 0)
		lft_zero_cnt = 0;
	    else if (lft_zero_cnt > out_width)
		lft_zero_cnt = out_width;
	    if (no_full_pixels) {
		lft_edge_cnt = (int)ceil(x_rgt_part);
		if (lft_edge_cnt < 0)
		    lft_edge_cnt = 0;
		else if (lft_edge_cnt > out_width)
		    lft_edge_cnt = out_width;
		lft_edge_cnt -= lft_zero_cnt;
		if (lft_edge_cnt < 0)
		    lft_edge_cnt = 0;
		full_cnt = 0;
		rgt_edge_cnt = 0;
		rgt_zero_cnt = out_width - lft_zero_cnt - lft_edge_cnt;
	    } else {
		lft_edge_cnt = (int)ceil(x_lft_full);
		if (lft_edge_cnt < 0)
		    lft_edge_cnt = 0;
		else if (lft_edge_cnt > out_width)
		    lft_edge_cnt = out_width;
		lft_edge_cnt -= lft_zero_cnt;
		if (lft_edge_cnt < 0)
		    lft_edge_cnt = 0;
		full_cnt = (int)floor(x_rgt_full);
		if (full_cnt < 0)
		    full_cnt = 0;
		else if (full_cnt > out_width)
		    full_cnt = out_width;
		full_cnt -= lft_edge_cnt + lft_zero_cnt;
		if (full_cnt < 0)
		    full_cnt = 0;
		rgt_edge_cnt = (int)ceil(x_rgt_part);
		if (rgt_edge_cnt < 0)
		    rgt_edge_cnt = 0;
		else if (rgt_edge_cnt > out_width)
		    rgt_edge_cnt = out_width;
		rgt_edge_cnt -= full_cnt + lft_edge_cnt + lft_zero_cnt;
		if (rgt_edge_cnt < 0)
		    rgt_edge_cnt = 0;
		rgt_zero_cnt = out_width - lft_zero_cnt - lft_edge_cnt -
		    	       full_cnt - rgt_edge_cnt;
	    }

	    /* reverse map the first left-edge output pixel coordinate into
	       the input image coordinate system */
	    in_x = ((lft_zero_cnt - warp_matrix[0][2]) * warp_matrix[1][1] -
		    (out_scan_y - warp_matrix[1][2])*warp_matrix[0][1])*denom;
	    in_y = (-(lft_zero_cnt - warp_matrix[0][2]) * warp_matrix[1][0] +
		    (out_scan_y - warp_matrix[1][2])*warp_matrix[0][0])*denom;
	    in_x_int = (int)floor(in_x);
	    in_y_int = (int)floor(in_y);
	    in_ptr = (RGBIntPixel *)(((char *)in_image + in_y_int *
				       in_bytes_per_scan)) + in_x_int;

	    /* compute the weight lookup table indices and increments */
	    xfrac = FLTFRAC_TO_FIX31(in_x - in_x_int);
	    yfrac = FLTFRAC_TO_FIX31(in_y - in_y_int);

	    /* zero out unaffected pixels on left edge of scan */
	    if (lft_zero_cnt > 0) {
		bzero(out_ptr, lft_zero_cnt * ((0) + (1)));
		out_ptr += lft_zero_cnt * ((0) + (1));
	    }

	    /* process left edge case pixels */
	    for (x = lft_zero_cnt; x < lft_zero_cnt + lft_edge_cnt; x++) {
		wptr = VPBilirpWeight[FIX31_TO_WGTIND(yfrac)]
		    		     [FIX31_TO_WGTIND(xfrac)];


	opc_acc = 0;;
		if (in_x_int >= 0 && in_x_int < in_width) {
		    if (in_y_int >= 0 && in_y_int < in_height) {


	opc_acc += (wptr[0]) * (in_ptr[0].opcflt);;
		    }
		    if (in_y_int+1 >= 0 && in_y_int+1 < in_height) {


	opc_acc += (wptr[2]) * (in_ptr[in_width].opcflt);;
		    }
		}
		if (in_x_int+1 >= 0 && in_x_int+1 < in_width) {
		    if (in_y_int >= 0 && in_y_int < in_height) {


	opc_acc += (wptr[1]) * (in_ptr[1].opcflt);;
		    }
		    if (in_y_int+1 >= 0 && in_y_int+1 < in_height) {


	opc_acc += (wptr[3]) * (in_ptr[in_width + 1].opcflt);;
		    }
		}



	opc_acc_int = opc_acc * (float)255.;
	if (opc_acc_int > 255)
	    opc_acc_int = 255;
	((out_ptr)[0]) = opc_acc_int;;
		out_ptr += ((0) + (1));
		xfrac += xfrac_incr;
		yfrac += yfrac_incr;
		if (xfrac < 0) {
		    xfrac &= 0x7fffffff;
		    in_x_int += in_x_incr_int + in_x_incr_dlt;
		    in_ptr += in_x_incr_int + in_x_incr_dlt;
		} else {
		    in_x_int += in_x_incr_int;
		    in_ptr += in_x_incr_int;
		}
		if (yfrac < 0) {
		    yfrac &= 0x7fffffff;
		    in_y_int += in_y_incr_int + in_y_incr_dlt;
		    in_ptr += in_width * (in_y_incr_int + in_y_incr_dlt);
		} else {
		    in_y_int += in_y_incr_int;
		    in_ptr += in_width * in_y_incr_int;
		}
	    }

	    /* process output pixels affected by four input pixels */
	    for (x = lft_zero_cnt + lft_edge_cnt;
		 x < lft_zero_cnt + lft_edge_cnt + full_cnt; x++) {
		ASSERT(in_x_int >= 0 && in_x_int < in_width-1);
		ASSERT(in_y_int >= 0 && in_y_int < in_height-1);
		ASSERT((RGBIntPixel *)(((char *)in_image + in_y_int *
				in_bytes_per_scan)) + in_x_int == in_ptr);
		wptr = VPBilirpWeight[FIX31_TO_WGTIND(yfrac)]
				     [FIX31_TO_WGTIND(xfrac)];



	opc_acc = (wptr[0]) * (in_ptr[0].opcflt) +
		  (wptr[2]) * (in_ptr[in_width].opcflt) +
		  (wptr[1]) * (in_ptr[1].opcflt) +
		  (wptr[3]) * (in_ptr[in_width+1].opcflt);;



	opc_acc_int = opc_acc * (float)255.;
	if (opc_acc_int > 255)
	    opc_acc_int = 255;
	((out_ptr)[0]) = opc_acc_int;;
		out_ptr += ((0) + (1));
		xfrac += xfrac_incr;
		yfrac += yfrac_incr;
		if (xfrac < 0) {
		    xfrac &= 0x7fffffff;
		    in_x_int += in_x_incr_int + in_x_incr_dlt;
		    in_ptr += in_x_incr_int + in_x_incr_dlt;
		} else {
		    in_x_int += in_x_incr_int;
		    in_ptr += in_x_incr_int;
		}
		if (yfrac < 0) {
		    yfrac &= 0x7fffffff;
		    in_y_int += in_y_incr_int + in_y_incr_dlt;
		    in_ptr += in_width * (in_y_incr_int + in_y_incr_dlt);
		} else {
		    in_y_int += in_y_incr_int;
		    in_ptr += in_width * in_y_incr_int;
		}
	    }

	    /* process right edge case pixels */
	    for (x = lft_zero_cnt + lft_edge_cnt + full_cnt;
		 x < lft_zero_cnt + lft_edge_cnt + full_cnt + rgt_edge_cnt;
		 x++) {
		wptr = VPBilirpWeight[FIX31_TO_WGTIND(yfrac)]
				     [FIX31_TO_WGTIND(xfrac)];


	opc_acc = 0;;
		if (in_x_int >= 0 && in_x_int < in_width) {
		    if (in_y_int >= 0 && in_y_int < in_height) {


	opc_acc += (wptr[0]) * (in_ptr[0].opcflt);;
		    }
		    if (in_y_int+1 >= 0 && in_y_int+1 < in_height) {


	opc_acc += (wptr[2]) * (in_ptr[in_width].opcflt);;
		    }
		}
		if (in_x_int+1 >= 0 && in_x_int+1 < in_width) {
		    if (in_y_int >= 0 && in_y_int < in_height) {


	opc_acc += (wptr[1]) * (in_ptr[1].opcflt);;
		    }
		    if (in_y_int+1 >= 0 && in_y_int+1 < in_height) {


	opc_acc += (wptr[3]) * (in_ptr[in_width + 1].opcflt);;
		    }
		}



	opc_acc_int = opc_acc * (float)255.;
	if (opc_acc_int > 255)
	    opc_acc_int = 255;
	((out_ptr)[0]) = opc_acc_int;;
		out_ptr += ((0) + (1));
		xfrac += xfrac_incr;
		yfrac += yfrac_incr;
		if (xfrac < 0) {
		    xfrac &= 0x7fffffff;
		    in_x_int += in_x_incr_int + in_x_incr_dlt;
		    in_ptr += in_x_incr_int + in_x_incr_dlt;
		} else {
		    in_x_int += in_x_incr_int;
		    in_ptr += in_x_incr_int;
		}
		if (yfrac < 0) {
		    yfrac &= 0x7fffffff;
		    in_y_int += in_y_incr_int + in_y_incr_dlt;
		    in_ptr += in_width * (in_y_incr_int + in_y_incr_dlt);
		} else {
		    in_y_int += in_y_incr_int;
		    in_ptr += in_width * in_y_incr_int;
		}
	    }

	    /* zero out unaffected pixels on right edge of scan */
	    if (rgt_zero_cnt > 0) {
		bzero(out_ptr, rgt_zero_cnt * ((0) + (1)));
		out_ptr += rgt_zero_cnt * ((0) + (1));
	    }

	    /* go on to next scan */
	    scans_to_next_vertex--;
	    out_scan_y++;
	    out_ptr += out_bytes_per_scan - out_width * ((0) + (1));
	    x_lft_full += full_overlap[region].x_incr_lft;
	    x_rgt_full += full_overlap[region].x_incr_rgt;
	    x_lft_part += part_overlap[region].x_incr_lft;
	    x_rgt_part += part_overlap[region].x_incr_rgt;
	} /* next scanline in region */
    } /* next region */
    ASSERT(out_scan_y == out_height);
}
