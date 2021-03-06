/*
droid vnc server - Android VNC server
Copyright (C) 2011 Jose Pereira <onaips@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "gralloc_method.h"
#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/fb.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <cutils/log.h>

#define r(fd, ptr, size) (read((fd), (ptr), (size)) != (int)(size))
#define w(fd, ptr, size) (write((fd), (ptr), (size)) != (int)(size))


static int fill_format(struct fbinfo* info, int format)
{
    // bpp, red, green, blue, alpha

    static const int format_map[][9] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0},   // INVALID
        {32, 0, 8, 8, 8, 16, 8, 24, 8}, // HAL_PIXEL_FORMAT_RGBA_8888
        {32, 0, 8, 8, 8, 16, 8, 0, 0}, // HAL_PIXEL_FORMAT_RGBX_8888
        {24, 16, 8, 8, 8, 0, 8, 0, 0},  // HAL_PIXEL_FORMAT_RGB_888
        {16, 11, 5, 5, 6, 0, 5, 0, 0},  // HAL_PIXEL_FORMAT_RGB_565
        {32, 16, 8, 8, 8, 0, 8, 24, 8}, // HAL_PIXEL_FORMAT_BGRA_8888
        {16, 11, 5, 6, 5, 1, 5, 0, 1},  // HAL_PIXEL_FORMAT_RGBA_5551
        {16, 12, 4, 8, 4, 4, 4, 0, 4}   // HAL_PIXEL_FORMAT_RGBA_4444
    };
    const int *p;

    if (format == 0 || format > HAL_PIXEL_FORMAT_RGBA_4444)
        return -ENOTSUP;

    p = format_map[format];

    info->bpp = *(p++);
    info->red_offset = *(p++);
    info->red_length = *(p++);
    info->green_offset = *(p++);
    info->green_length = *(p++);
    info->blue_offset = *(p++);
    info->blue_length = *(p++);
    info->alpha_offset = *(p++);
    info->alpha_length = *(p++);

    return 0;
}
#if 0
static int readfb_devfb (int fd)
{
    struct fb_var_screeninfo vinfo;
    int fb, offset;
    char x[256];
    int rv = -ENOTSUP;

    struct fbinfo fbinfo;
    unsigned i, bytespp;

    fb = open("/dev/graphics/fb0", O_RDONLY);
    if (fb < 0) goto done;

    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) goto done;
    fcntl(fb, F_SETFD, FD_CLOEXEC);

    bytespp = vinfo.bits_per_pixel / 8;

    fbinfo.bpp = vinfo.bits_per_pixel;
    fbinfo.size = vinfo.xres * vinfo.yres * bytespp;
    fbinfo.width = vinfo.xres;
    fbinfo.height = vinfo.yres;
    fbinfo.red_offset = vinfo.red.offset;
    fbinfo.red_length = vinfo.red.length;
    fbinfo.green_offset = vinfo.green.offset;
    fbinfo.green_length = vinfo.green.length;
    fbinfo.blue_offset = vinfo.blue.offset;
    fbinfo.blue_length = vinfo.blue.length;
    fbinfo.alpha_offset = vinfo.transp.offset;
    fbinfo.alpha_length = vinfo.transp.length;

    /* HACK: for several of our 3d cores a specific alignment
     * is required so the start of the fb may not be an integer number of lines
     * from the base.  As a result we are storing the additional offset in
     * xoffset. This is not the correct usage for xoffset, it should be added
     * to each line, not just once at the beginning */
    offset = vinfo.xoffset * bytespp;

    offset += vinfo.xres * vinfo.yoffset * bytespp;

    rv = 0;

    if (w(fd, &fbinfo, sizeof(fbinfo))) goto done;

    lseek(fb, offset, SEEK_SET);
    for (i = 0; i < fbinfo.size; i += 256) {
      if (r(fb, &x, 256)) goto done;
      if (w(fd, &x, 256)) goto done;
    }

    if (r(fb, &x, fbinfo.size % 256)) goto done;
    if (w(fd, &x, fbinfo.size % 256)) goto done;

done:
    if (fb >= 0) close(fb);
    return rv;
}

#endif
 

struct gralloc_module_t *gralloc;
struct framebuffer_device_t *fbdev = 0;
struct alloc_device_t *allocdev = 0;
buffer_handle_t buf = 0;
unsigned char* data = 0;
int stride;

#define CHECK_RV if (rv != 0){close_gralloc();return -1;}
    
int init_gralloc()
{
    L("--Initializing gralloc access method--\n");
    grallocfb=0;
      
    int linebytes;
    int rv;

    rv = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t**)&gralloc);
    
    CHECK_RV;
  
    rv = framebuffer_open(&gralloc->common, &fbdev);
  
    CHECK_RV;
  
    if (!fbdev->read) {
        rv = -ENOTSUP;
        close_gralloc();
        return rv;
    }
 
    rv = gralloc_open(&gralloc->common, &allocdev);
    
    CHECK_RV;

    rv = allocdev->alloc(allocdev, fbdev->width, fbdev->height,
                         fbdev->format, GRALLOC_USAGE_SW_READ_OFTEN,
                         &buf, &stride);


    rv = fbdev->read(fbdev, buf);

    CHECK_RV;
    
    rv = gralloc->lock(gralloc, buf, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
                       fbdev->width, fbdev->height, (void**)&data);
    CHECK_RV;
    
    rv = fill_format(&displayInfo, fbdev->format);
    
    CHECK_RV;

    stride *= (displayInfo.bpp >> 3);
    linebytes = fbdev->width * (displayInfo.bpp >> 3);
    displayInfo.size = linebytes * fbdev->height;
    displayInfo.width = fbdev->width;
    displayInfo.height = fbdev->height;

    // point of no return: don't attempt alternative means of reading
    // after this
    rv = 0;

    grallocfb=malloc(displayInfo.size);

    L("Stride=%d   Linebytes=%d %p\n",stride,linebytes,fbdev->setUpdateRect);
    
     memcpy(grallocfb,data,displayInfo.size);
    
    if (data)
        gralloc->unlock(gralloc, buf);
    
    L("Copy %d bytes\n",displayInfo.size);
     
    L("Returning rv=%d\n",rv);
    return rv;
}


void close_gralloc()
{
    if (buf)
        allocdev->free(allocdev, buf);
    if (allocdev)
        gralloc_close(allocdev);
    if (fbdev)
        framebuffer_close(fbdev);
}




int readfb_gralloc ()
{
    int rv;


    rv = fbdev->read(fbdev, buf);

    CHECK_RV;
    
    rv = gralloc->lock(gralloc, buf, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
                       fbdev->width, fbdev->height, (void**)&data);
    CHECK_RV;

    memcpy(grallocfb,data,displayInfo.size);
    
    if (data)
        gralloc->unlock(gralloc, buf);

    return rv;
}


