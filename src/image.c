#include "app.h"

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

/* GDI+ flat API wrappers for C */
#include <gdiplus/gdiplusflat.h>

static CLSID g_png_clsid;

static int get_png_encoder_clsid(CLSID *clsid) {
    UINT num = 0, size = 0;
    GdipGetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo *info = (ImageCodecInfo *)malloc(size);
    if (!info) return -1;
    GdipGetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            *clsid = info[i].Clsid;
            free(info);
            return 0;
        }
    }
    free(info);
    return -1;
}

void image_init(void) {
    GdiplusStartupInput input = { 0 };
    input.GdiplusVersion = 1;
    GdiplusStartup(&g_app.gdiplus_token, &input, NULL);
    get_png_encoder_clsid(&g_png_clsid);
}

void image_shutdown(void) {
    if (g_app.gdiplus_token) {
        GdiplusShutdown(g_app.gdiplus_token);
        g_app.gdiplus_token = 0;
    }
}

int image_save_from_clipboard(const wchar_t *out_path) {
    int result = -1;
    HANDLE hData = NULL;
    GpBitmap *gpBitmap = NULL;

    /* Try CF_DIB first */
    hData = GetClipboardData(CF_DIB);
    if (hData) {
        BITMAPINFO *bmi = (BITMAPINFO *)GlobalLock(hData);
        if (bmi) {
            int headerSize = bmi->bmiHeader.biSize;
            int palCount = 0;
            if (bmi->bmiHeader.biBitCount <= 8)
                palCount = (bmi->bmiHeader.biClrUsed > 0) ?
                    (int)bmi->bmiHeader.biClrUsed : (1 << bmi->bmiHeader.biBitCount);

            BYTE *bits = (BYTE *)bmi + headerSize + palCount * sizeof(RGBQUAD);

            GpStatus st = GdipCreateBitmapFromGdiDib(bmi, bits, &gpBitmap);
            GlobalUnlock(hData);

            if (st == Ok && gpBitmap) {
                /* Resize if too large (max 2560px on longest edge) */
                UINT w = 0, h = 0;
                GdipGetImageWidth(gpBitmap, &w);
                GdipGetImageHeight(gpBitmap, &h);

                GpBitmap *scaled = NULL;
                if (w > 2560 || h > 2560) {
                    UINT newW, newH;
                    if (w >= h) {
                        newW = 2560;
                        newH = (UINT)((double)h / w * 2560.0);
                    } else {
                        newH = 2560;
                        newW = (UINT)((double)w / h * 2560.0);
                    }
                    GdipCreateBitmapFromScan0(newW, newH, 0, PixelFormat32bppARGB, NULL, &scaled);
                    if (scaled) {
                        GpGraphics *graphics = NULL;
                        GdipGetImageGraphicsContext(scaled, &graphics);
                        if (graphics) {
                            GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
                            GdipDrawImageRectRectI(graphics, gpBitmap,
                                0, 0, newW, newH, 0, 0, w, h, UnitPixel, NULL, NULL, NULL);
                            GdipDeleteGraphics(graphics);
                        }
                        GdipDisposeImage(gpBitmap);
                        gpBitmap = scaled;
                    }
                }

                GpStatus save_st = GdipSaveImageToFile(gpBitmap, out_path, &g_png_clsid, NULL);
                result = (save_st == Ok) ? 0 : -1;
                GdipDisposeImage(gpBitmap);
                return result;
            }
        }
    }

    /* Fallback: CF_BITMAP */
    hData = GetClipboardData(CF_BITMAP);
    if (hData) {
        HBITMAP hbmp = (HBITMAP)hData;
        GdipCreateBitmapFromHBITMAP(hbmp, NULL, &gpBitmap);
        if (gpBitmap) {
            GpStatus save_st = GdipSaveImageToFile(gpBitmap, out_path, &g_png_clsid, NULL);
            result = (save_st == Ok) ? 0 : -1;
            GdipDisposeImage(gpBitmap);
        }
    }

    return result;
}

HBITMAP image_load_thumbnail(const wchar_t *path, int tw, int th) {
    GpImage *gpImage = NULL;
    if (GdipLoadImageFromFile(path, &gpImage) != Ok || !gpImage)
        return NULL;

    UINT w = 0, h = 0;
    GdipGetImageWidth(gpImage, &w);
    GdipGetImageHeight(gpImage, &h);

    GpBitmap *gpThumb = NULL;
    GdipCreateBitmapFromScan0(tw, th, 0, PixelFormat32bppARGB, NULL, &gpThumb);
    if (!gpThumb) {
        GdipDisposeImage(gpImage);
        return NULL;
    }

    GpGraphics *graphics = NULL;
    GdipGetImageGraphicsContext(gpThumb, &graphics);
    if (graphics) {
        GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
        GdipSetSmoothingMode(graphics, SmoothingModeHighQuality);

        /* Maintain aspect ratio, center */
        float scaleW = (float)tw / w;
        float scaleH = (float)th / h;
        float scale = (scaleW < scaleH) ? scaleW : scaleH;
        int dw = (int)(w * scale);
        int dh = (int)(h * scale);
        int dx = (tw - dw) / 2;
        int dy = (th - dh) / 2;

        GdipDrawImageRectRectI(graphics, gpImage,
            dx, dy, dw, dh, 0, 0, w, h, UnitPixel, NULL, NULL, NULL);
        GdipDeleteGraphics(graphics);
    }

    HBITMAP result = NULL;
    GdipCreateHBITMAPFromBitmap(gpThumb, &result, 0);

    GdipDisposeImage(gpThumb);
    GdipDisposeImage(gpImage);
    return result;
}

HBITMAP image_load_full(const wchar_t *path, int *out_w, int *out_h) {
    GpImage *gpImage = NULL;
    if (GdipLoadImageFromFile(path, &gpImage) != Ok || !gpImage)
        return NULL;

    UINT w = 0, h = 0;
    GdipGetImageWidth(gpImage, &w);
    GdipGetImageHeight(gpImage, &h);

    /* Cap to 80% of screen */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int maxW = screenW * 4 / 5;
    int maxH = screenH * 4 / 5;

    int dw = (int)w, dh = (int)h;
    if (dw > maxW || dh > maxH) {
        float scaleW = (float)maxW / w;
        float scaleH = (float)maxH / h;
        float scale = (scaleW < scaleH) ? scaleW : scaleH;
        dw = (int)(w * scale);
        dh = (int)(h * scale);
    }

    GpBitmap *gpBmp = NULL;
    GdipCreateBitmapFromScan0(dw, dh, 0, PixelFormat32bppARGB, NULL, &gpBmp);
    if (!gpBmp) {
        GdipDisposeImage(gpImage);
        return NULL;
    }

    GpGraphics *graphics = NULL;
    GdipGetImageGraphicsContext(gpBmp, &graphics);
    if (graphics) {
        GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
        GdipDrawImageRectRectI(graphics, gpImage,
            0, 0, dw, dh, 0, 0, w, h, UnitPixel, NULL, NULL, NULL);
        GdipDeleteGraphics(graphics);
    }

    HBITMAP result = NULL;
    GdipCreateHBITMAPFromBitmap(gpBmp, &result, 0);

    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;

    GdipDisposeImage(gpBmp);
    GdipDisposeImage(gpImage);
    return result;
}

int image_get_dimensions(const wchar_t *path, int *w, int *h) {
    GpImage *gpImage = NULL;
    if (GdipLoadImageFromFile(path, &gpImage) != Ok || !gpImage)
        return -1;
    UINT uw = 0, uh = 0;
    GdipGetImageWidth(gpImage, &uw);
    GdipGetImageHeight(gpImage, &uh);
    if (w) *w = (int)uw;
    if (h) *h = (int)uh;
    GdipDisposeImage(gpImage);
    return 0;
}
