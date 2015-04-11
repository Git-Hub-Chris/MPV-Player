/*
 * video output driver for libdrm
 *
 * by rr- <rr-@sakuya.pl>
 *
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libswscale/swscale.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "common/msg.h"
#include "video/mp_image.h"
#include "video/sws_utils.h"
#include "sub/osd.h"
#include "vo.h"

#define BUF_COUNT 2

//TODO: change path_to_device to option
//TODO: change modeset_dev to option
const char *path_to_device = "/dev/dri/card0";
const unsigned int connector_id = 0;

struct modeset_buf {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size;
    uint32_t handle;
    uint8_t *map;
    uint32_t fb;
};

struct modeset_dev {
    struct modeset_buf bufs[BUF_COUNT];
    drmModeModeInfo mode;
    uint32_t conn;
    uint32_t crtc;
    int front_buf;
};

struct priv {
    int fd;
    struct modeset_dev *dev;
    drmModeCrtc *old_crtc;

    int32_t x, y;
    struct mp_rect src;
    struct mp_rect dst;
    struct mp_osd_res osd;
    struct mp_sws_context *sws;
};

static int modeset_open(struct vo *vo, int *out, const char *node)
{
    int fd;
    uint64_t has_dumb;
    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot open \"%s\": %s.\n", node, errstr);
        return -errno;
    }
    if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0) {
        MP_ERR(vo, "Device \"%s\" does not support dumb buffers.\n", node);
        return -EOPNOTSUPP;
    }
    *out = fd;
    return 0;
}

static int modeset_create_fb(struct vo *vo, int fd, struct modeset_buf *buf)
{
    struct drm_mode_create_dumb creq;
    struct drm_mode_destroy_dumb dreq;
    struct drm_mode_map_dumb mreq;
    int ret;

    // create dumb buffer
    memset(&creq, 0, sizeof(creq));
    creq.width = buf->width;
    creq.height = buf->height;
    creq.bpp = 32;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret < 0) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot create dumb buffer: %s\n", errstr);
        return -errno;
    }
    buf->stride = creq.pitch;
    buf->size = creq.size;
    buf->handle = creq.handle;

    // create framebuffer object for the dumb-buffer
    ret = drmModeAddFB(fd, buf->width, buf->height, 24, 32, buf->stride,
                       buf->handle, &buf->fb);
    if (ret) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot create framebuffer: %s\n", errstr);
        ret = -errno;
        goto err_destroy;
    }

    // prepare buffer for memory mapping
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = buf->handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot map dumb buffer: %s\n", errstr);
        ret = -errno;
        goto err_fb;
    }

    // perform actual memory mapping
    buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, mreq.offset);
    if (buf->map == MAP_FAILED) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot map dumb buffer: %s\n", errstr);
        ret = -errno;
        goto err_fb;
    }

    memset(buf->map, 0, buf->size);

    return 0;

err_fb:
    drmModeRmFB(fd, buf->fb);
err_destroy:
    memset(&dreq, 0, sizeof(dreq));
    dreq.handle = buf->handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    return ret;
}

static int modeset_find_crtc(struct vo *vo, int fd, drmModeRes *res,
                             drmModeConnector *conn, struct modeset_dev *dev)
{
    drmModeEncoder *enc;
    unsigned int i, j;

    for (i = 0; i < conn->count_encoders; ++i) {
        enc = drmModeGetEncoder(fd, conn->encoders[i]);
        if (!enc) {
            char *errstr = mp_strerror(errno);
            MP_WARN(vo, "Cannot retrieve encoder %u:%u: %s\n",
                    i, conn->encoders[i], errstr);
            continue;
        }

        // iterate all global CRTCs
        for (j = 0; j < res->count_crtcs; ++j) {
            // check whether this CRTC works with the encoder
            if (!(enc->possible_crtcs & (1 << j)))
                continue;

            drmModeFreeEncoder(enc);
            dev->crtc = res->crtcs[j];
            return 0;
        }

        drmModeFreeEncoder(enc);
    }

    MP_ERR(vo, "DRM connector %u has no suitable CRTC\n", conn->connector_id);
    return -ENOENT;
}

static void modeset_destroy_fb(int fd, struct modeset_buf *buf)
{
    struct drm_mode_destroy_dumb dreq;

    munmap(buf->map, buf->size);

    drmModeRmFB(fd, buf->fb);

    memset(&dreq, 0, sizeof(dreq));
    dreq.handle = buf->handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

static int modeset_prepare_dev(struct vo *vo, int fd, int conn_id,
                               struct modeset_dev **out)
{
    struct modeset_dev *dev = NULL;
    drmModeConnector *conn = NULL;
    drmModeRes *res = NULL;
    int ret = 0;

    *out = NULL;

    res = drmModeGetResources(fd);
    if (!res) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot retrieve DRM resources: %s\n", errstr);
        ret = -errno;
        goto end;
    }

    if (conn_id < 0 || conn_id >= res->count_connectors) {
        MP_ERR(vo, "Bad DRM connector ID. Max valid DRM connector ID = %u",
               res->count_connectors);
        ret = -ENODEV;
        goto end;
    }

    conn = drmModeGetConnector(fd, res->connectors[conn_id]);
    if (!conn) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot retrieve DRM connector %u:%u: %s\n",
               conn_id, res->connectors[conn_id], errstr);
        ret = -errno;
        goto end;
    }

    dev = talloc_size(vo->priv, sizeof(*dev));
    dev->conn = conn->connector_id;
    dev->front_buf = 0;

    if (conn->connection != DRM_MODE_CONNECTED) {
        MP_ERR(vo, "DRM connector %u is disconnected\n", conn_id);
        ret = -ENODEV;
        goto end;
    }

    if (conn->count_modes == 0) {
        MP_ERR(vo, "DRM connector %u has no valid modes\n", conn_id);
        ret = -ENODEV;
        goto end;
    }

    memcpy(&dev->mode, &conn->modes[0], sizeof(dev->mode));
    dev->bufs[0].width = conn->modes[0].hdisplay;
    dev->bufs[0].height = conn->modes[0].vdisplay;
    dev->bufs[1].width = conn->modes[0].hdisplay;
    dev->bufs[1].height = conn->modes[0].vdisplay;

    MP_INFO(vo, "DRM connector using mode %ux%u\n",
            dev->bufs[0].width, dev->bufs[0].height);

    ret = modeset_find_crtc(vo, fd, res, conn, dev);
    if (ret) {
        MP_ERR(vo, "DRM connector %u has no valid CRTC\n", conn_id);
        goto end;
    }

    unsigned int i, j;
    for (i = 0; i < BUF_COUNT; i ++) {
        ret = modeset_create_fb(vo, fd, &dev->bufs[i]);
        if (ret) {
            for (j = 0; j < i; j ++) {
                modeset_destroy_fb(fd, &dev->bufs[j]);
            }
            MP_ERR(vo, "Cannot create framebuffer for DRM connector %u\n", conn_id);
            return ret;
        }
    }

end:
    if (conn) drmModeFreeConnector(conn);
    if (res) drmModeFreeResources(res);
    if (ret == 0) {
        *out = dev;
    } else {
        talloc_free(dev);
    }
    return ret;
}



static int reconfig(struct vo *vo, struct mp_image_params *params, int flags)
{
    struct priv *p = vo->priv;
    int32_t device_w = p->dev->bufs[0].width;
    int32_t device_h = p->dev->bufs[0].height;
    float device_ar = device_w / (float)device_h;

    vo_get_src_dst_rects(vo, &p->src, &p->dst, &p->osd);
    int32_t video_w = (p->dst.x1 - p->dst.x0);
    int32_t video_h = (p->dst.y1 - p->dst.y0);
    float video_ar = video_w / (float)video_h;
    int32_t w, h;
    if (device_ar > video_ar) {
        w = device_h * video_ar;
        h = device_h;
    } else {
        w = device_w;
        h = device_w * video_ar;
    }
    if (w > device_w) w = device_w;
    if (h > device_h) h = device_h;

    p->x = (device_w - w) >> 1;
    p->y = (device_h - h) >> 1;

    mp_sws_set_from_cmdline(p->sws, vo->opts->sws_opts);
    p->sws->src = *params;
    p->sws->dst = (struct mp_image_params) {
        .imgfmt = IMGFMT_BGR32,
        .w = w,
        .h = h,
        .d_w = w,
        .d_h = h,
    };

    MP_INFO(vo, "resizing %dx%d -> %dx%d\n",
            p->sws->src.w,
            p->sws->src.h,
            p->sws->dst.w,
            p->sws->dst.h);

    mp_image_params_guess_csp(&p->sws->dst);

    if (mp_sws_reinit(p->sws) < 0)
        return 1;

    vo->want_redraw = true;
    return 0;
}

static void draw_image(struct vo *vo, mp_image_t *mpi)
{
    struct priv *p = vo->priv;

    struct modeset_buf *front_buf = &p->dev->bufs[p->dev->front_buf];
    struct mp_image img = {0};
    mp_image_set_params(&img, &p->sws->dst);
    img.planes[0] = front_buf->map + p->x * 4;
    img.stride[0] = front_buf->stride;

    struct mp_rect src_rc = p->src;
    src_rc.x0 = MP_ALIGN_DOWN(src_rc.x0, mpi->fmt.align_x);
    src_rc.y0 = MP_ALIGN_DOWN(src_rc.y0, mpi->fmt.align_y);
    mp_image_crop_rc(mpi, src_rc);

    mp_sws_scale(p->sws, &img, mpi);

    osd_draw_on_image(vo->osd, p->osd, mpi ? mpi->pts : 0, 0, &img);
}

static void flip_page(struct vo *vo)
{
    struct priv *p = vo->priv;

    int ret = drmModeSetCrtc(p->fd, p->dev->crtc,
                             p->dev->bufs[p->dev->front_buf].fb,
                             0, 0, &p->dev->conn, 1, &p->dev->mode);
    if (ret) {
        MP_WARN(vo, "Cannot flip page for DRM connector\n");
    } else {
        p->dev->front_buf ++;
        p->dev->front_buf %= BUF_COUNT;
    }
}

static int preinit(struct vo *vo)
{
    struct priv *p = vo->priv;
    p->dev = NULL;
    p->fd = 0;
    p->old_crtc = NULL;
    p->sws = mp_sws_alloc(vo);

    int ret;

    ret = modeset_open(vo, &p->fd, path_to_device);
    if (ret)
        return ret;

    ret = modeset_prepare_dev(vo, p->fd, connector_id, &p->dev);
    if (ret)
        return ret;

    assert(p->dev != NULL);

    p->old_crtc = drmModeGetCrtc(p->fd, p->dev->crtc);
    ret = drmModeSetCrtc(p->fd, p->dev->crtc,
                         p->dev->bufs[p->dev->front_buf + BUF_COUNT - 1].fb,
                         0, 0, &p->dev->conn, 1, &p->dev->mode);
    if (ret) {
        char *errstr = mp_strerror(errno);
        MP_ERR(vo, "Cannot set CRTC for connector %u: %s\n", connector_id,
               errstr);
    }

    return 0;
}

static void uninit(struct vo *vo)
{
    struct priv *p = vo->priv;

    if (p->dev != NULL) {
        if (p->old_crtc != NULL) {
            drmModeSetCrtc(p->fd,
                           p->old_crtc->crtc_id,
                           p->old_crtc->buffer_id,
                           p->old_crtc->x,
                           p->old_crtc->y,
                           &p->dev->conn,
                           1,
                           &p->dev->mode);
            drmModeFreeCrtc(p->old_crtc);
        }

        modeset_destroy_fb(p->fd, &p->dev->bufs[1]);
        modeset_destroy_fb(p->fd, &p->dev->bufs[0]);
    }

    talloc_free(p->dev);
}

static int query_format(struct vo *vo, int format)
{
    return format == IMGFMT_BGR32;
}

static int control(struct vo *vo, uint32_t request, void *data)
{
    return VO_NOTIMPL;
}

const struct vo_driver video_out_drm = {
    .name = "drm",
    .description = "Direct Rendering Manager",
    .preinit = preinit,
    .query_format = query_format,
    .reconfig = reconfig,
    .control = control,
    .draw_image = draw_image,
    .flip_page = flip_page,
    .uninit = uninit,
    .priv_size = sizeof(struct priv),
};
