#pragma once
/**
 * MetaReader.h — Читання метаданих треків для Nebula OS
 *
 * Стратегія (в порядку пріоритету):
 *  1. FLAC Vorbis Comments (block type 4)
 *  2. ID3v2 (MP3/FLAC з ID3 заголовком)
 *  3. Folder-name heuristic fallback
 *
 * Supported folder pattern:
 *   /music/Artist/Albums/YYYY - Album/NN. Title.flac
 *   /music/Artist/Album/NN - Title.mp3
 */
#include <Arduino.h>
#include <FS.h>
#include "MediaDB.h"

class MetaReader {
public:
    /**
     * Читає метадату файлу. path = повний шлях від кореня SD.
     * Повертає true якщо хоча б title або artist заповнені.
     */
    static bool read(fs::FS& sd, const char* path, TrackRecord& out) {
        memset(&out, 0, sizeof(out));
        strncpy(out.path, path, sizeof(out.path)-1);

        fs::File f = sd.open(path, "r");
        if (!f) {
            // Файл недоступний — тільки folder fallback
            return parseFolderPath(path, out);
        }

        bool ok = false;

        // Перевірити magic bytes
        uint8_t magic[4] = {};
        f.read(magic, 4);
        f.seek(0);

        if (magic[0]=='f' && magic[1]=='L' && magic[2]=='a' && magic[3]=='C') {
            ok = readFlacVorbis(f, out);
        } else if (magic[0]=='I' && magic[1]=='D' && magic[2]=='3') {
            ok = readID3v2(f, out);
        }
        f.close();

        // Folder fallback для незаповнених полів
        if (!ok || out.artist[0]=='\0' || out.title[0]=='\0') {
            TrackRecord tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (parseFolderPath(path, tmp)) {
                if (out.title[0]  == '\0') strncpy(out.title,  tmp.title,  63);
                if (out.artist[0] == '\0') strncpy(out.artist, tmp.artist, 63);
                if (out.album[0]  == '\0') strncpy(out.album,  tmp.album,  63);
                if (out.year[0]   == '\0') strncpy(out.year,   tmp.year,   7);
                if (out.trackNum  == 0)    out.trackNum = tmp.trackNum;
            }
        }

        return out.title[0] != '\0' || out.artist[0] != '\0';
    }

private:
    // ── Helpers ─────────────────────────────────────────────────────

    static uint32_t readU32LE(fs::File& f) {
        uint8_t b[4] = {}; f.read(b, 4);
        return (uint32_t)b[0] | ((uint32_t)b[1]<<8) |
               ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    }
    static uint32_t readU32BE(fs::File& f) {
        uint8_t b[4] = {}; f.read(b, 4);
        return ((uint32_t)b[0]<<24) | ((uint32_t)b[1]<<16) |
               ((uint32_t)b[2]<<8) | b[3];
    }
    static uint32_t syncsafeToU32(const uint8_t* b) {
        return ((uint32_t)b[0]<<21) | ((uint32_t)b[1]<<14) |
               ((uint32_t)b[2]<<7)  |  (uint32_t)b[3];
    }

    // Читає рядок з файлу (UTF-8/Latin-1), обрізає до maxLen
    static void readStr(fs::File& f, uint32_t len, char* out, uint8_t maxLen) {
        uint32_t toRead = min(len, (uint32_t)(maxLen-1));
        f.read((uint8_t*)out, toRead);
        out[toRead] = '\0';
        if (len > toRead) f.seek(f.position() + (len - toRead));
        // Trim trailing whitespace
        int l = strlen(out);
        while (l > 0 && (out[l-1] == ' ' || out[l-1] == '\r' || out[l-1] == '\n')) out[--l] = 0;
    }

    // Парсить Vorbis tag "KEY=VALUE", заповнює поля TrackRecord
    static void applyVorbisTag(const char* comment, TrackRecord& out) {
        const char* eq = strchr(comment, '=');
        if (!eq) return;
        char key[32] = {};
        size_t klen = eq - comment;
        if (klen >= sizeof(key)) return;
        strncpy(key, comment, klen);
        const char* val = eq + 1;
        if (*val == '\0') return;

        if      (strcasecmp(key, "TITLE")       == 0) strncpy(out.title,  val, 63);
        else if (strcasecmp(key, "ARTIST")      == 0) strncpy(out.artist, val, 63);
        else if (strcasecmp(key, "ALBUM")       == 0) strncpy(out.album,  val, 63);
        else if (strcasecmp(key, "GENRE")       == 0) strncpy(out.genre,  val, 31);
        else if (strcasecmp(key, "DATE")        == 0) strncpy(out.year,   val,  7);
        else if (strcasecmp(key, "YEAR")        == 0 && out.year[0]=='\0')
            strncpy(out.year, val, 7);
        else if (strcasecmp(key, "TRACKNUMBER") == 0) out.trackNum = (uint8_t)atoi(val);
        else if (strcasecmp(key, "DISCNUMBER")  == 0) out.discNum  = (uint8_t)atoi(val);
    }

    // ── FLAC Vorbis Comment parser ───────────────────────────────────
    static bool readFlacVorbis(fs::File& f, TrackRecord& out) {
        f.seek(4); // skip "fLaC"
        bool found = false;

        for (int attempt = 0; attempt < 32; attempt++) {
            uint8_t hdr[4] = {};
            if (f.read(hdr, 4) != 4) break;
            bool   isLast   = (hdr[0] >> 7) & 1;
            uint8_t btype   = hdr[0] & 0x7F;
            uint32_t blen   = ((uint32_t)hdr[1]<<16) | ((uint32_t)hdr[2]<<8) | hdr[3];

            if (btype == 4) { // VORBIS_COMMENT
                // Skip vendor string
                uint32_t vendLen = readU32LE(f);
                f.seek(f.position() + vendLen);
                // Read comments
                uint32_t count = readU32LE(f);
                for (uint32_t i = 0; i < count && i < 64; i++) {
                    uint32_t clen = readU32LE(f);
                    if (clen == 0 || clen > 512) { f.seek(f.position() + clen); continue; }
                    char buf[256] = {};
                    uint32_t toRead = min(clen, (uint32_t)255);
                    f.read((uint8_t*)buf, toRead);
                    buf[toRead] = '\0';
                    if (clen > toRead) f.seek(f.position() + (clen - toRead));
                    applyVorbisTag(buf, out);
                }
                found = true;
                break;
            } else {
                f.seek(f.position() + blen);
            }
            if (isLast) break;
        }
        return found;
    }

    // ── ID3v2 parser ─────────────────────────────────────────────────
    // Підтримка: v2.3 і v2.4 (4-byte frame IDs)
    static bool readID3v2(fs::File& f, TrackRecord& out) {
        f.seek(0);
        uint8_t hdr[10] = {};
        if (f.read(hdr, 10) != 10) return false;
        if (hdr[0]!='I' || hdr[1]!='D' || hdr[2]!='3') return false;
        uint8_t ver = hdr[3]; // 3 = v2.3, 4 = v2.4
        if (ver < 3 || ver > 4) return false; // Підтримуємо лише v2.3 / v2.4

        uint32_t tagSize = syncsafeToU32(hdr + 6);
        uint32_t pos = 10;
        bool found = false;

        while (pos + 10 <= tagSize) {
            uint8_t fhdr[10] = {};
            if (f.read(fhdr, 10) != 10) break;
            if (fhdr[0] == 0) break; // padding

            char fid[5] = {(char)fhdr[0],(char)fhdr[1],(char)fhdr[2],(char)fhdr[3],0};
            uint32_t fsize;
            if (ver == 4) {
                fsize = syncsafeToU32(fhdr + 4);
            } else {
                fsize = ((uint32_t)fhdr[4]<<24)|((uint32_t)fhdr[5]<<16)|
                        ((uint32_t)fhdr[6]<<8)|fhdr[7];
            }
            pos += 10;
            if (fsize == 0 || fsize > tagSize) break;

            // Тільки текстові фрейми нам потрібні
            bool isText = (strcmp(fid,"TIT2")==0 || strcmp(fid,"TPE1")==0 ||
                           strcmp(fid,"TALB")==0 || strcmp(fid,"TDRC")==0 ||
                           strcmp(fid,"TYER")==0 || strcmp(fid,"TRCK")==0 ||
                           strcmp(fid,"TCON")==0 || strcmp(fid,"TPOS")==0);

            if (isText && fsize >= 2) {
                uint8_t enc = 0; f.read(&enc, 1);
                uint32_t dataLen = fsize - 1;
                char buf[128] = {};

                if (enc == 1 || enc == 2) {
                    // UTF-16: читаємо побайтово, беремо тільки ASCII
                    uint8_t tmp[256] = {};
                    uint32_t rd = min(dataLen, (uint32_t)255);
                    f.read(tmp, rd);
                    if (rd < dataLen) f.seek(f.position() + (dataLen - rd));
                    // Skip BOM if present
                    uint32_t start = (tmp[0]==0xFF&&tmp[1]==0xFE)||(tmp[0]==0xFE&&tmp[1]==0xFF) ? 2 : 0;
                    uint32_t bi = 0;
                    // Little-endian UTF-16: take every 2nd byte starting from start
                    for (uint32_t k = start; k+1 < rd && bi < 127; k += 2) {
                        uint16_t ch = (enc==1) ? (tmp[k] | (uint16_t)tmp[k+1]<<8)
                                                : ((uint16_t)tmp[k]<<8 | tmp[k+1]);
                        if (ch == 0) break;
                        if (ch < 0x80) buf[bi++] = (char)ch;
                        else buf[bi++] = '?';
                    }
                    buf[bi] = '\0';
                } else {
                    // UTF-8 або Latin-1
                    uint32_t rd = min(dataLen, (uint32_t)127u);
                    f.read((uint8_t*)buf, rd);
                    buf[rd] = '\0';
                    if (rd < dataLen) f.seek(f.position() + (dataLen - rd));
                }

                // Trim null bytes and whitespace
                for (int i = strlen(buf)-1; i >= 0 && (buf[i]=='\0'||buf[i]==' '); i--) buf[i]='\0';

                if      (strcmp(fid,"TIT2")==0) { strncpy(out.title,  buf, 63); found=true; }
                else if (strcmp(fid,"TPE1")==0) { strncpy(out.artist, buf, 63); found=true; }
                else if (strcmp(fid,"TALB")==0) { strncpy(out.album,  buf, 63); }
                else if (strcmp(fid,"TDRC")==0 ||
                         strcmp(fid,"TYER")==0)  { strncpy(out.year,   buf,  7); }
                else if (strcmp(fid,"TRCK")==0)  { out.trackNum = (uint8_t)atoi(buf); }
                else if (strcmp(fid,"TPOS")==0)  { out.discNum  = (uint8_t)atoi(buf); }
                else if (strcmp(fid,"TCON")==0)  { strncpy(out.genre, buf, 31); }
            } else {
                f.seek(f.position() + fsize);
            }
            pos += fsize;
        }
        return found;
    }

    // ── Folder-name heuristic ────────────────────────────────────────
    // Парсить шлях типу /music/Artist/Albums/YYYY - Album/NN. Title.flac
    static bool parseFolderPath(const char* path, TrackRecord& out) {
        char buf[256] = {};
        strncpy(buf, path, 255);

        // Розбити на компоненти
        const char* parts[16] = {};
        int np = 0;
        parts[np++] = buf;
        for (char* p = buf; *p; p++) {
            if (*p == '/' && np < 15) { *p = '\0'; parts[np++] = p+1; }
        }
        if (np < 2) return false;

        // ── Filename → title + trackNum ─────────────────────────────
        const char* fname = parts[np-1];
        char nameBuf[128] = {};
        const char* dot = strrchr(fname, '.');
        size_t nlen = dot ? (size_t)(dot-fname) : strlen(fname);
        strncpy(nameBuf, fname, min(nlen, (size_t)127));

        const char* titleStart = nameBuf;
        if (isdigit(nameBuf[0])) {
            out.trackNum = (uint8_t)atoi(nameBuf);
            const char* p2 = nameBuf;
            while (isdigit(*p2)) p2++;
            if (*p2 == '.') p2++;                  // "03. "
            while (*p2 == ' ') p2++;
            if (*p2 == '-') p2++;                   // "03 - "
            while (*p2 == ' ') p2++;
            titleStart = p2;
        }
        if (*titleStart) strncpy(out.title, titleStart, 63);

        // ── Parent dir → album (+ optional year) ────────────────────
        int di = np - 2;
        if (di >= 0 && parts[di][0]) {
            const char* dir = parts[di];
            // Якщо починається з 4-значного числа "YYYY - Album"
            if (isdigit(dir[0]) && isdigit(dir[1]) && isdigit(dir[2]) && isdigit(dir[3]) &&
                (dir[4]=='-' || dir[4]==' ')) {
                char yr[5] = {dir[0],dir[1],dir[2],dir[3],0};
                strncpy(out.year, yr, 7);
                const char* al = dir+4;
                while (*al=='-' || *al==' ') al++;
                if (*al) strncpy(out.album, al, 63);
            } else {
                strncpy(out.album, dir, 63);
            }
            di--;
        }

        // ── Skip intermediate dirs (Albums, Disc X, CD X) ────────────
        auto isSkippable = [](const char* s) {
            if (strcasecmp(s,"Albums")==0 || strcasecmp(s,"Album")==0) return true;
            if (strcasecmp(s,"Discs")==0  || strcasecmp(s,"Disc")==0)  return true;
            if (strncasecmp(s,"Disc ",5)==0) return true;
            if (strncasecmp(s,"CD",2)==0 && isdigit(s[2])) return true;
            return false;
        };
        while (di >= 0 && isSkippable(parts[di])) di--;

        // ── Artist ───────────────────────────────────────────────────
        if (di >= 0 && parts[di][0]) strncpy(out.artist, parts[di], 63);

        return out.title[0] != '\0';
    }
};
