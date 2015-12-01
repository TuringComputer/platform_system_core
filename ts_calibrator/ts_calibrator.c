/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <cutils/properties.h>

#define LOG_BUF_MAX 512

#ifndef TS_DEVICE
#error "no touch screen device defined"
#endif

#define DEV1_(x) #x
#define DEV_(x) DEV1_(x)
#define TS_INPUT_DEV DEV_(TS_DEVICE)

#ifdef TS_DEVICE_ALT
#define TS_INPUT_DEV_ALT DEV_(TS_DEVICE_ALT)
#endif

#define DA9052_DEV_NAME "da9052_tsi"

#define MAX_LEN 256

static const char fb_dev[] = "/dev/graphics/fb0";
static const char input_dev[] = "/dev/input/event";
static const char cf_file[] = "/data/system/calibration";
static const char log[] = "/ts.log";
static const char dev_name[] = TS_INPUT_DEV;
#ifdef TS_DEVICE_ALT
static const char dev_name_alt[] = TS_INPUT_DEV_ALT;
#endif
static int log_fd;
static struct fb_var_screeninfo info;
static void *scrbuf;
static int fb_fd, ts_fd, cf_fd;
static int cal_val[7];

static void log_write(const char *fmt, ...)
{
    char buf[LOG_BUF_MAX];
    va_list ap;

    if (log_fd < 0) return;

    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_MAX, fmt, ap);
    buf[LOG_BUF_MAX - 1] = 0;
    va_end(ap);
    write(log_fd, buf, strlen(buf));
}

static int find_ts_device(char *name, char *dev)
{
    int i;
    int found = 0;

    for (i = 0; ; i++) {
        struct stat s;
        int fd;
        
	    sprintf(dev, "%s%d", input_dev, i);

	    if (stat(dev, &s) != 0) {
	        break;
	    }

	    fd = open(dev, O_RDWR);
	    if (fd < 0) {
	        continue;
	    }

	    ioctl(fd, EVIOCGNAME(MAX_LEN), name);
	    close(fd);
	    
	    if (strncmp(name, dev_name, strlen(dev_name)) == 0) {
	        found = 1;
	        break;
#ifdef TS_DEVICE_ALT
	    /* Dual PMIC support: look for alternative name */
	    } else if (strncmp(name, dev_name_alt, strlen(dev_name_alt)) == 0) {
		    found = 1;
		    break;
#endif
	    } else {
            continue;
	    }
    }
    
    return found;
}

static void write_conf(int *data)
{
    char param_path[MAX_LEN];
    char buf[200];
    char name[MAX_LEN] = "";
    char dev[MAX_LEN] = "";
    int fd, len;
    struct stat s;
    
    if (!find_ts_device(name, dev)) {
        log_write("write_conf(): device not found\n");
        return;
    }

    sprintf(param_path,
	    "/sys/module/%s/parameters/calibration", name);
	    
	log_write("Opening %s\n", param_path);
	
    fd = open(param_path, O_WRONLY);
    if (fd < 0) {
        log_write("write_conf() error, can not write driver parameters\n");
        return;
    }
    
    len = sprintf(buf, "%d,%d,%d,%d,%d,%d,%d",
			    data[0], data[1], data[2],
			    data[3], data[4], data[5],
			    data[6]);
    log_write("write_conf(), write driver parameters:\n\t%s\n", buf);
    write(fd, buf, len);
    close(fd);
}

static void save_conf(int *data)
{
    int fd;
    char buf[MAX_LEN];
    int len;
    
    fd = open(cf_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
	    log_write("create, open file %s error:%d\n", cf_file, errno);
	    return;
    }

    len = sprintf(buf, "%d\n%d\n%d\n%d\n%d\n%d\n%d",
			    data[0], data[1], data[2],
			    data[3], data[4], data[5],
			    data[6]);

    write(fd, buf, len);
    close(fd);
}

static void get_input(int *px, int *py)
{
    int rd, i;
    struct input_event ev[64];
    int step = 0;
    
    while (1) {

        for (i = 0; i < (int)(rd / sizeof(struct input_event)); i++) {
	    
	        rd = read(ts_fd, &ev[i], sizeof(struct input_event));

	        switch (ev[i].type) {

	        case EV_SYN:
		        if (step)
		            return;
		            
	        case EV_KEY:
		        if (ev[i].code == BTN_TOUCH && ev[i].value == 0)
		            /* get the final touch */
		            step = 1;
		    break;
	       
	        case EV_ABS:
		        if (ev[i].code == REL_X)
		            *px = ev[i].value;
		        else if (ev[i].code == REL_Y)
		            *py = ev[i].value;
		    break;
	        
	        default:
		    break;
	        }
	    }

    }

}

#define LINE_LEN 16
static void draw_cross(int x, int y, int clear)
{
    int px_byte = info.bits_per_pixel / 8;
    int h_start, v_start;
    int i;
    __u32 pixel = ~(0U);
    __u8 *buf = scrbuf;
    __u16 *buf16;
    __u32 *buf32;

    if (clear)
	pixel = 0;

    h_start = (x + y*info.xres - LINE_LEN/2) * px_byte;
    v_start = (x + (y - LINE_LEN/2)*info.xres) * px_byte;

    switch (info.bits_per_pixel) {

    case 16:
	buf16 = (__u16*)((__u8*)scrbuf + h_start);
	for (i = 0; i <= LINE_LEN; i ++)
	    *buf16++ = (__u16)pixel;
	buf16 = (__u16*)((__u8*)scrbuf + v_start);
	for (i = 0; i <= LINE_LEN; i ++) {
	    *buf16 = (__u16)pixel;
	    buf16 += info.xres;
	}
	break;
    case 24:
	buf += h_start;
	for (i = 0; i <= LINE_LEN; i ++) {
	    *buf++ = *((__u8*)pixel + 2);
	    *buf++ = *((__u8*)pixel + 1);
	    *buf++ = *(__u8*)pixel;
	}
	buf = (__u8*)scrbuf + v_start;
	for (i = 0; i <= LINE_LEN; i ++) {
	    *buf++ = *((__u8*)pixel + 2);
	    *buf++ = *((__u8*)pixel + 1);
	    *buf = *(__u8*)pixel;
	    buf += info.xres * px_byte - 2;
	}
	break;
    case 32:
	buf32 = (__u32*)((__u8*)scrbuf + h_start);
	pixel &= (((1 << info.transp.length) - 1) << info.transp.offset);
	for (i = 0; i <= LINE_LEN; i ++)
	    *buf32++ = pixel;
	buf32 = (__u32*)((__u8*)scrbuf + v_start);
	for (i = 0; i <= LINE_LEN; i ++) {
	    *buf32 = pixel;
	    buf32 += info.xres;
	}
	break;
    default:
	break;
    }

}

static void do_calibration(void)
{
    int i, x, y;
    int dx[3], dy[3];
    int tx[3], ty[3];
    int delta, delta_x[3], delta_y[3];
    
    /* calculate the expected point */
    x = info.xres / 4;
    y = info.yres / 4;

    dx[0] = x;
    dy[0] = info.yres / 2;
    dx[1] = info.xres / 2;
    dy[1] = y;
    dx[2] = info.xres - x;
    dy[2] = info.yres - y;

retry:

    for (i = 0; i < 3; i ++) {
	draw_cross(dx[i], dy[i], 0);
	get_input(&tx[i], &ty[i]);
	log_write("get event: %d,%d -> %d,%d\n",
			tx[i], ty[i], dx[i], dy[i]);
	draw_cross(dx[i], dy[i], 1);
    }

    /* check ok, calulate the result */
    delta = (tx[0] - tx[2]) * (ty[1] - ty[2])
                - (tx[1] - tx[2]) * (ty[0] - ty[2]);
    delta_x[0] = (dx[0] - dx[2]) * (ty[1] - ty[2])
                - (dx[1] - dx[2]) * (ty[0] - ty[2]);
    delta_x[1] = (tx[0] - tx[2]) * (dx[1] - dx[2])
                - (tx[1] - tx[2]) * (dx[0] - dx[2]);
    delta_x[2] = dx[0] * (tx[1] * ty[2] - tx[2] * ty[1]) -
                dx[1] * (tx[0] * ty[2] - tx[2] * ty[0]) +
                dx[2] * (tx[0] * ty[1] - tx[1] * ty[0]);
    delta_y[0] = (dy[0] - dy[2]) * (ty[1] - ty[2])
                - (dy[1] - dy[2]) * (ty[0] - ty[2]);
    delta_y[1] = (tx[0] - tx[2]) * (dy[1] - dy[2])
                - (tx[1] - tx[2]) * (dy[0] - dy[2]);
    delta_y[2] = dy[0] * (tx[1] * ty[2] - tx[2] * ty[1]) -
                dy[1] * (tx[0] * ty[2] - tx[2] * ty[0]) +
                dy[2] * (tx[0] * ty[1] - tx[1] * ty[0]);

    cal_val[0] = delta_x[0];
    cal_val[1] = delta_x[1];
    cal_val[2] = delta_x[2];
    cal_val[3] = delta_y[0];
    cal_val[4] = delta_y[1];
    cal_val[5] = delta_y[2];
    cal_val[6] = delta;

    save_conf(cal_val);
    write_conf(cal_val);
}

static void do_calibration_da9052(int do_calib)
{
    int i, x, y;
    int dx[3], dy[3];
    int tx[3], ty[3];
    int delta, delta_x[3], delta_y[3];
    struct input_absinfo absX, absY;
    
    if (do_calib) {
        /* calculate the expected point */
        x = info.xres / 4;
        y = info.yres / 4;

        dx[0] = x;
        dy[0] = info.yres / 2;
        dx[1] = info.xres / 2;
        dy[1] = y;
        dx[2] = info.xres - x;
        dy[2] = info.yres - y;

        for (i = 0; i < 3; i ++) {
        draw_cross(dx[i], dy[i], 0);
        get_input(&tx[i], &ty[i]);
	        log_write("Received x,y -> Expected x,y\n");
	        log_write("%d,%d -> %d,%d\n",
		        tx[i], ty[i], dx[i], dy[i]);
        draw_cross(dx[i], dy[i], 1);
        }

        /* check ok, calulate the result */
        delta = (tx[0] - tx[2]) * (ty[1] - ty[2])
                    - (tx[1] - tx[2]) * (ty[0] - ty[2]);
        delta_x[0] = (dx[0] - dx[2]) * (ty[1] - ty[2])
                    - (dx[1] - dx[2]) * (ty[0] - ty[2]);
        delta_x[1] = (tx[0] - tx[2]) * (dx[1] - dx[2])
                    - (tx[1] - tx[2]) * (dx[0] - dx[2]);
        delta_x[2] = dx[0] * (tx[1] * ty[2] - tx[2] * ty[1]) -
                    dx[1] * (tx[0] * ty[2] - tx[2] * ty[0]) +
                    dx[2] * (tx[0] * ty[1] - tx[1] * ty[0]);
        delta_y[0] = (dy[0] - dy[2]) * (ty[1] - ty[2])
                    - (dy[1] - dy[2]) * (ty[0] - ty[2]);
        delta_y[1] = (tx[0] - tx[2]) * (dy[1] - dy[2])
                    - (tx[1] - tx[2]) * (dy[0] - dy[2]);
        delta_y[2] = dy[0] * (tx[1] * ty[2] - tx[2] * ty[1]) -
                    dy[1] * (tx[0] * ty[2] - tx[2] * ty[0]) +
                    dy[2] * (tx[0] * ty[1] - tx[1] * ty[0]);

        cal_val[0] = delta_x[0];
        cal_val[1] = delta_x[1];
        cal_val[2] = delta_x[2];
        cal_val[3] = delta_y[0];
        cal_val[4] = delta_y[1];
        cal_val[5] = delta_y[2];
        cal_val[6] = delta;

        save_conf(cal_val);
        write_conf(cal_val);
    }

    /* Android input framework expects values based on the absmax.
	 Now that the touchscreen is calibrated set the input maximum
	 according to screen resolution, not the touch sensor resolution */
	if (ioctl(ts_fd, EVIOCGABS(ABS_X), &absX) == -1) {
		log_write("EVIOCGABS( failed\n");
	} else {
		log_write("Modifying x.max:%i -> %i\n", absX.maximum, info.xres);
		absX.maximum = info.xres;
		if (ioctl(ts_fd, EVIOCSABS(ABS_X), &absX) == -1)
			log_write("EVIOCSABS( failed\n");
	}

	if (ioctl(ts_fd, EVIOCGABS(ABS_Y), &absY) == -1) {
		log_write("EVIOCGABS( failed\n");
	} else {
		log_write("Modifying y.max:%i -> %i\n", absY.maximum, info.yres);
		absY.maximum = info.yres;
		if (ioctl(ts_fd, EVIOCSABS(ABS_Y), &absY) == -1)
			log_write("EVIOCSABS( failed\n");
	}
}

static void test_calibration(void)
{
    int sample[3][2] = {
	{ 200, 200 },
	{ 100, 400 },
	{ 600, 330 },
    };
    int tx[3];
    int ty[3];
    int i, x, y;

    for (i = 0; i < 3; i ++) {
	draw_cross(sample[i][0], sample[i][1], 0);
	get_input(&tx[i], &ty[i]);
	x = (cal_val[0] * tx[i]) +
		(cal_val[1] * ty[i]) +
		cal_val[2];
	y = (cal_val[3] * tx[i]) +
		(cal_val[4] * ty[i]) +
		cal_val[5];
	log_write("get event: %d,%d\n", x/cal_val[6], y/cal_val[6]);
	draw_cross(sample[i][0], sample[i][1], 1);
    }
}

static int check_conf(void)
{
    int data[7];
    char *buffer;
    int ret;
    struct stat s;
    
    /* check conf file */
    if (stat(cf_file, &s) == 0) {
	    /* conf file already existed */
	    cf_fd = open(cf_file, O_RDWR);
	    if (cf_fd >= 0) {
	        buffer = calloc(1, s.st_size + 1);
	        read(cf_fd, buffer, s.st_size);
	        ret = sscanf(buffer, "%d\n%d\n%d\n%d\n%d\n%d\n%d",
				    &data[0], &data[1], &data[2],
				    &data[3], &data[4], &data[5],
				    &data[6]);
	        if (ret == 7) {
		        free(buffer);
		        /* write to driver */
		        write_conf(data);
		        close(cf_fd);
		        return 1;
	        }
	        log_write("Failed to get datas from conf file: %d\n", ret);
	        free(buffer);
	        close(cf_fd);
	    }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct fb_fix_screeninfo finfo;
    struct stat s;
    int i;
    int do_calib = 1;
    char runme[PROPERTY_VALUE_MAX];
    char name[MAX_LEN] = "";
    char dev[MAX_LEN] = "";
    int found;

    property_get("ro.calibration", runme, "");
    if (runme[0] != '1')
	    return 0;

    /* open log */
    log_fd = open(log, O_WRONLY | O_CREAT | O_TRUNC);

    if (check_conf())
        do_calib = 0;

    /* read framebuffer for resolution */
    fb_fd = open(fb_dev, O_RDWR);
    if (fb_fd <= 0) {
	    log_write("Failed to open %s\n", fb_dev);
	    goto err_log;
    }
    if (-1 == ioctl(fb_fd, FBIOGET_VSCREENINFO, &info)) {
	    log_write("Failed to get screen info\n");
	    goto err_fb;
    }
    log_write("Screen resolution: %dx%d\n", info.xres, info.yres);
    /* map buffer */
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
	    log_write("Failed to get screen info: %d\n", errno);
        goto err_fb;
    }
    scrbuf = (__u16*) mmap(0, finfo.smem_len,
			    PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    fb_fd, 0);
    if (scrbuf== MAP_FAILED) {
	    log_write("Failed to map screen\n");
	    goto err_fb;
    }
    
    /* Clear screen */
    memset(scrbuf, 0, finfo.smem_len);

    /* Find touchscreen device */
    found = find_ts_device(name, dev);
    if (!found) {
        log_write("can not find ts device\n");
        goto err_map;
    }
    
    ts_fd = open(dev, O_RDWR);
    if (ts_fd < 0) {
        log_write("Failed to open %s\n", dev);
        goto err_map;
    }

    /* Special calibration function for the DA905x PMIC */
    if (strcmp(name, DA9052_DEV_NAME) == 0) {
        log_write("do_calibration_da9052\n");
        do_calibration_da9052(do_calib);
    } else {
        log_write("do_calibration\n");
        if (do_calib) {
            do_calibration();
        }
    }


    log_write("Calibration done!!\n");

    //test_calibration();

    close(ts_fd);
err_map:
    munmap(scrbuf, finfo.smem_len);
err_fb:
    close(fb_fd);
err_log:
    close(log_fd);

    return 0;
}
