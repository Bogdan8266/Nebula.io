#pragma once
/**
 * ArtRenderer.h - Album art decoding and dithering for E-Ink
 *
 * Pipeline:
 *   JPEG on SD -> JPEGDEC (Streaming via Callbacks) -> RGB565 MCU blocks
 *   -> per-pixel grayscale -> Floyd-Steinberg -> 1-bit bitmap
 *
 * Memory: No large RAM buffer needed (~5KB for bitmap + ~1KB for context)
 */
#include <Arduino.h>
#include <FS.h>
#include <JPEGDEC.h>

enum class ArtRenderMode { NORMAL, SIDE_LEFT, SIDE_RIGHT, BACKGROUND };

// Context for JPEGDEC callback
struct ArtRenderCtx {
    uint8_t*  bits;       // output 1-bit bitmap
    uint16_t  targetW;    // target size in pixels (square)
    uint16_t  targetH;
    uint16_t  decodedW;   // decoded width after JPEGDEC scale
    uint16_t  decodedH;
    int16_t*  errCur;     // F-S error for current row
    int16_t*  errNxt;     // F-S error for next row
    uint16_t  lastRow;    // track row transitions for buffer swap
    fs::File* sdFile;     // Source SD file
    ArtRenderMode mode;   // Rendering mode (NORMAL, SIDE_LEFT, SIDE_RIGHT, BACKGROUND)
    uint8_t*  grayBuf;    // Buffer for full-image processing (blur/gradient) before dithering
};

class ArtRenderer {
public:
    /**
     * Decodes JPEG from SD and dithers it into the provided 1-bit outBitmap.
     * Returns true if successful.
     */
    static bool renderTo(fs::FS& sd, const char* artPath, uint8_t* outBitmap, uint16_t w, uint16_t h, ArtRenderMode mode = ArtRenderMode::NORMAL, bool clearBuffer = true) {
        fs::File f = sd.open(artPath, "r");
        if (!f) return false;
        
        uint32_t fileSize = f.size();
        if (fileSize == 0) { f.close(); return false; }

        // --- Prepare 1-bit bitmap -----------------------------------
        // For SIDE modes, we use the MAX height (usually 120) to ensure the buffer is clear
        uint16_t rowBytes = (w + 7) / 8;
        if (clearBuffer) {
            memset(outBitmap, 0xFF, rowBytes * h); // Default to white
        }

        // --- Floyd-Steinberg row error buffers -----------------------
        static int16_t errCur[200]; memset(errCur, 0, sizeof(int16_t)*200);
        static int16_t errNxt[200]; memset(errNxt, 0, sizeof(int16_t)*200);

        // --- Setup Context -------------------------------------------
        _ctx.bits    = outBitmap;
        _ctx.targetW = w;
        _ctx.targetH = h;
        _ctx.errCur  = errCur;
        _ctx.errNxt  = errNxt;
        _ctx.lastRow = 0xFFFF;
        _ctx.sdFile  = &f;
        _ctx.mode    = mode;
        _ctx.grayBuf = nullptr;

        if (mode == ArtRenderMode::BACKGROUND) {
            _ctx.grayBuf = (uint8_t*)malloc(w * h);
        }

        // --- JPEGDEC decode (Streaming) -----------------------------
        // Розміщуємо JPEGDEC у heap, оскільки його розмір (~20КБ) перевищує ліміт стеку ESP32 (8КБ).
        JPEGDEC* jpeg = new JPEGDEC();
        if (!jpeg) {
            if (_ctx.grayBuf) free(_ctx.grayBuf);
            f.close();
            return false;
        }

        // Use open with callbacks to read directly from SD file
        if (!jpeg->open(artPath, _fOpen, _fClose, _fRead, _fSeek, _jpegCallback)) {
            if (_ctx.grayBuf) free(_ctx.grayBuf);
            delete jpeg;
            f.close(); 
            return false;
        }

        _ctx.decodedW = jpeg->getWidth();
        _ctx.decodedH = jpeg->getHeight();

        // scale: 0=1/1, JPEG_SCALE_HALF=2, JPEG_SCALE_QUARTER=4, JPEG_SCALE_EIGHTH=8
        int scale = 0; 
        if      (_ctx.decodedW >= (int)w * 8) { scale = JPEG_SCALE_EIGHTH;  _ctx.decodedW /= 8; _ctx.decodedH /= 8; }
        else if (_ctx.decodedW >= (int)w * 4) { scale = JPEG_SCALE_QUARTER; _ctx.decodedW /= 4; _ctx.decodedH /= 4; }
        else if (_ctx.decodedW >= (int)w * 2) { scale = JPEG_SCALE_HALF;    _ctx.decodedW /= 2; _ctx.decodedH /= 2; }

        jpeg->decode(0, 0, scale);
        jpeg->close();
        delete jpeg;

        // --- Post-Processing (Blur, Gradient & Dithering) for Background ---
        if (_ctx.grayBuf) {
            // 1. Box Blur (Fast 2-pass)
            const int radius = 4;
            uint8_t* rowTemp = (uint8_t*)malloc(w);
            if (rowTemp) {
                for (int y = 0; y < h; y++) {
                    memcpy(rowTemp, &_ctx.grayBuf[y * w], w);
                    for (int x = 0; x < w; x++) {
                        int sum = 0, count = 0;
                        for (int r = -radius; r <= radius; r++) {
                            int nx = x + r;
                            if (nx >= 0 && nx < w) { sum += rowTemp[nx]; count++; }
                        }
                        _ctx.grayBuf[y * w + x] = sum / count;
                    }
                }
                free(rowTemp);
            }

            uint8_t* colTemp = (uint8_t*)malloc(h);
            if (colTemp) {
                for (int x = 0; x < w; x++) {
                    for (int y = 0; y < h; y++) colTemp[y] = _ctx.grayBuf[y * w + x];
                    for (int y = 0; y < h; y++) {
                        int sum = 0, count = 0;
                        for (int r = -radius; r <= radius; r++) {
                            int ny = y + r;
                            if (ny >= 0 && ny < h) { sum += colTemp[ny]; count++; }
                        }
                        _ctx.grayBuf[y * w + x] = sum / count;
                    }
                }
                free(colTemp);
            }

            // 2. Apply Brightness & Gradient (Top & Bottom to Black)
            for (int y = 0; y < h; y++) {
                float factor = 0.7f; // Increased base brightness for blurred bg
                if (y < 55) { // Fade to black at top (status bar + artist)
                    factor = 0.7f * (y / 55.0f);
                } else if (y > h - 45) { // Fade to black at bottom (buttons)
                    factor = 0.7f * ((h - 1 - y) / 45.0f);
                }
                for (int x = 0; x < w; x++) {
                    _ctx.grayBuf[y * w + x] = (uint8_t)(_ctx.grayBuf[y * w + x] * factor);
                }
            }

            // 3. Floyd-Steinberg Dithering Pass
            memset(errCur, 0, sizeof(int16_t) * 200);
            memset(errNxt, 0, sizeof(int16_t) * 200);
            
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int16_t val = _ctx.grayBuf[y * w + x] + errCur[x];
                    if (val < 0) val = 0; if (val > 255) val = 255;
                    uint8_t q = (val >= 128) ? 255 : 0;
                    
                    if (q == 0) outBitmap[y * rowBytes + (x / 8)] &= ~(0x80 >> (x & 7));
                    
                    int16_t err = val - q;
                    if (x + 1 <  w)              errCur[x+1] += err * 7 / 16;
                    if (x - 1 >= 0 && y + 1 < h) errNxt[x-1] += err * 3 / 16;
                    if (              y + 1 < h) errNxt[x]   += err * 5 / 16;
                    if (x + 1 < w &&  y + 1 < h) errNxt[x+1] += err * 1 / 16;
                }
                memcpy(errCur, errNxt, sizeof(int16_t) * 200);
                memset(errNxt, 0, sizeof(int16_t) * 200);
                if (y % 16 == 0) yield();
            }

            free(_ctx.grayBuf);
            _ctx.grayBuf = nullptr;
        }

        f.close();

        return true;
    }

private:
    static ArtRenderCtx _ctx;

    // --- File Callbacks for JPEGDEC ---
    static void* _fOpen(const char *szFilename, int32_t *pFileSize) {
        // The file is already open in _ctx.sdFile
        *pFileSize = _ctx.sdFile->size();
        return (void*)_ctx.sdFile;
    }
    static void _fClose(void *pHandle) {
        // We close the file in renderTo
    }
    static int32_t _fRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
        fs::File *f = (fs::File *)pFile->fHandle;
        return f->read(pBuf, iLen);
    }
    static int32_t _fSeek(JPEGFILE *pFile, int32_t iPos) {
        fs::File *f = (fs::File *)pFile->fHandle;
        return f->seek(iPos);
    }

    // --- Pixel Callback ---
    static int _jpegCallback(JPEGDRAW* pDraw) {
        // Essential for long-running decodes on ESP32 to prevent Watchdog Reset.
        // Yield every 8 lines to avoid slowing down rendering too much.
        if (pDraw->y % 8 == 0) yield(); 

        uint16_t tW = _ctx.targetW;
        uint16_t tH = _ctx.targetH;
        uint16_t dW = _ctx.decodedW ? _ctx.decodedW : tW;
        uint16_t dH = _ctx.decodedH ? _ctx.decodedH : tH;

        for (int row = 0; row < pDraw->iHeight; row++) {
            int srcY = pDraw->y + row;

            for (int col = 0; col < pDraw->iWidth; col++) {
                int srcX = pDraw->x + col;
                int tx   = (int)((float)srcX * tW / dW);
                if (tx < 0 || tx >= tW) continue;

                // --- Calculate Local Height and Offset for Perspective ---
                int16_t h_x   = tH;
                int16_t y_off = 0;
                
                if (_ctx.mode == ArtRenderMode::SIDE_LEFT) {
                    // Left edge (x=0) is 80px, right edge (x=39) is 120px
                    h_x = 80 + (40 * tx / (tW - 1));
                    y_off = (tH - h_x) / 2;
                } else if (_ctx.mode == ArtRenderMode::SIDE_RIGHT) {
                    // Left edge (x=0) is 120px, right edge (x=39) is 80px
                    h_x = tH - (40 * tx / (tW - 1));
                    y_off = (tH - h_x) / 2;
                }

                // Map decoded Y to local target Y
                int ty = (int)((float)srcY * h_x / dH) + y_off;
                if (ty < 0 || ty >= tH) continue;

                if (ty != _ctx.lastRow && _ctx.lastRow != 0xFFFF) {
                    memcpy(_ctx.errCur, _ctx.errNxt, sizeof(int16_t) * tW);
                    memset(_ctx.errNxt, 0,           sizeof(int16_t) * tW);
                }
                _ctx.lastRow = ty;

                uint16_t px = pDraw->pPixels[row * pDraw->iWidth + col];
                uint8_t  r  = ((px >> 11) & 0x1F) << 3;
                uint8_t  g  = ((px >>  5) & 0x3F) << 2;
                uint8_t  b  = ( px        & 0x1F) << 3;
                int16_t gray = (r * 77 + g * 150 + b * 29) >> 8;

                // --- Linear Contrast Stretch ---
                const int16_t blackClip = 40;
                const int16_t whiteClip = 215;
                if (gray < blackClip) {
                    gray = 0;
                } else if (gray > whiteClip) {
                    gray = 255;
                } else {
                    gray = (gray - blackClip) * 255 / (whiteClip - blackClip);
                }

                if (_ctx.grayBuf) {
                    _ctx.grayBuf[ty * tW + tx] = (uint8_t)gray;
                    continue; 
                }

                if (_ctx.mode == ArtRenderMode::SIDE_LEFT || _ctx.mode == ArtRenderMode::SIDE_RIGHT) {
                    // Darken side covers consistently (prevents interference stripes)
                    gray = (gray * 180) >> 8; 
                }

                int16_t val  = (int16_t)(gray + _ctx.errCur[tx]);
                if (val < 0) val = 0; if (val > 255) val = 255;
                
                uint8_t q    = (val >= 128) ? 255 : 0;
                int16_t err  = val - q;

                uint16_t rb = (tW + 7) / 8;
                if (q == 0) _ctx.bits[ty * rb + tx/8] &= ~(0x80 >> (tx & 7));
                else        _ctx.bits[ty * rb + tx/8] |=  (0x80 >> (tx & 7));

                // Floyd-Steinberg distribution
                if (tx+1 <  tW)               _ctx.errCur[tx+1] += err * 7 / 16;
                if (tx-1 >= 0 && ty+1 < tH)   _ctx.errNxt[tx-1] += err * 3 / 16;
                if (           ty+1 < tH)      _ctx.errNxt[tx]   += err * 5 / 16;
                if (tx+1 < tW && ty+1 < tH)    _ctx.errNxt[tx+1] += err * 1 / 16;
            }
        }
        return 1;
    }
};

ArtRenderCtx ArtRenderer::_ctx = {};
