#pragma once
/**
 * ArtExtractor.h — Витягує JPEG обкладинку з FLAC PICTURE block (type 6)
 * і зберігає на SD у /art/<sanitized>.jpg
 *
 * FLAC PICTURE block (metadata block type 6):
 *   uint32 picture_type  (BE) — 3 = Cover (front)
 *   uint32 mime_len      (BE)
 *   char   mime[mime_len]
 *   uint32 desc_len      (BE)
 *   char   desc[desc_len]
 *   uint32 width         (BE)
 *   uint32 height        (BE)
 *   uint32 color_depth   (BE)
 *   uint32 colors_used   (BE)
 *   uint32 data_len      (BE)
 *   uint8  data[data_len]   ← raw JPEG bytes
 */
#include <Arduino.h>
#include <FS.h>

class ArtExtractor {
public:
    /**
     * Обчислює стандартний шлях для art-файлу.
     * /art/Three_Days_Grace_One-X.jpg
     */
    static void getArtPath(const char* artist, const char* album,
                            char* out, size_t outLen) {
        char sa[64] = {}, sb[64] = {};
        _sanitize(artist, sa, sizeof(sa));
        _sanitize(album,  sb, sizeof(sb));
        snprintf(out, outLen, "/art/%s_%s.jpg", sa, sb);
    }

    static bool artExists(fs::FS& sd, const char* artPath) {
        return sd.exists(artPath);
    }

    /**
     * Читає flacPath, знаходить PICTURE block (type 6, picture_type=3),
     * зберігає raw JPEG до artPath.
     * Повертає true якщо art знайдено і збережено.
     */
    static bool extractFromFlac(fs::FS& sd, const char* flacPath,
                                  const char* artPath) {
        fs::File f = sd.open(flacPath, "r");
        if (!f) return false;

        // Перевірити FLAC magic
        uint8_t magic[4] = {};
        if (f.read(magic, 4) != 4 ||
            magic[0]!='f' || magic[1]!='L' || magic[2]!='a' || magic[3]!='C') {
            f.close(); return false;
        }

        bool saved = false;

        // Ітерувати metadata blocks
        for (int i = 0; i < 32 && !saved; i++) {
            uint8_t hdr[4] = {};
            if (f.read(hdr, 4) != 4) break;
            bool    isLast = (hdr[0] >> 7) & 1;
            uint8_t btype  = hdr[0] & 0x7F;
            uint32_t blen  = ((uint32_t)hdr[1]<<16) | ((uint32_t)hdr[2]<<8) | hdr[3];

            if (btype == 6) { // PICTURE block
                saved = _readPictureBlock(sd, f, artPath, blen);
            } else {
                f.seek(f.position() + blen);
            }
            if (isLast) break;
        }

        f.close();
        return saved;
    }

    /**
     * Ensure /art/ directory exists on SD.
     */
    static void ensureArtDir(fs::FS& sd) {
        if (!sd.exists("/art")) {
            sd.mkdir("/art");
        }
    }

private:
    static uint32_t _readU32BE(fs::File& f) {
        uint8_t b[4] = {}; f.read(b, 4);
        return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];
    }

    static bool _readPictureBlock(fs::FS& sd, fs::File& f,
                                   const char* artPath, uint32_t blockLen) {
        uint32_t consumed = 0;

        uint32_t picType = _readU32BE(f); consumed += 4;
        // Нас цікавить тільки Cover (front) = 3, або 0 (інший)
        // Беремо будь-який тип якщо 3 немає

        uint32_t mimeLen = _readU32BE(f); consumed += 4;
        char mime[32] = {};
        uint32_t mimeRead = min(mimeLen, (uint32_t)31);
        f.read((uint8_t*)mime, mimeRead);
        if (mimeLen > mimeRead) f.seek(f.position() + (mimeLen - mimeRead));
        consumed += mimeLen;

        // Перевірити що це JPEG (PNG поки не підтримуємо)
        bool isJpeg = (strstr(mime, "jpeg") || strstr(mime, "jpg"));
        if (!isJpeg) {
            // Пропустити решту блоку
            f.seek(f.position() + (blockLen - consumed));
            return false;
        }

        uint32_t descLen = _readU32BE(f); consumed += 4;
        f.seek(f.position() + descLen); consumed += descLen;
        f.seek(f.position() + 16);     consumed += 16; // width+height+depth+colors (4×4)

        uint32_t dataLen = _readU32BE(f); consumed += 4;
        if (dataLen == 0 || dataLen > 1024*512) return false; // Санітарна перевірка

        // Якщо art вже існує — не перезаписуємо
        if (sd.exists(artPath)) {
            f.seek(f.position() + dataLen);
            return true; // Вже є
        }

        // Записати JPEG на SD
        fs::File out = sd.open(artPath, "w");
        if (!out) {
            f.seek(f.position() + dataLen);
            return false;
        }

        // Копіюємо JPEG чанками по 512 байт
        static uint8_t buf[512];
        uint32_t remaining = dataLen;
        while (remaining > 0) {
            uint32_t chunk = min(remaining, (uint32_t)sizeof(buf));
            int32_t rd = f.read(buf, chunk);
            if (rd <= 0) break;
            out.write(buf, rd);
            remaining -= rd;
        }
        out.close();

        Serial.printf("[ART] Saved: %s (%lu bytes)\n", artPath, dataLen);
        return true;
    }

    // Конвертувати рядок у безпечне ім'я файлу
    static void _sanitize(const char* in, char* out, size_t outLen) {
        size_t i = 0, o = 0;
        while (in[i] && o < outLen-1) {
            char c = in[i++];
            if (isalnum(c) || c=='-' || c=='.') out[o++] = c;
            else if (c==' ' || c=='_')            out[o++] = '_';
            // Інші символи — пропускаємо
        }
        out[o] = '\0';
        if (o == 0) { out[0]='X'; out[1]='\0'; }
    }
};
