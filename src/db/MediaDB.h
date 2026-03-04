#pragma once
/**
 * MediaDB.h — Nebula OS flat-file media library
 *
 * Формат файлу /nebula.db:
 *   [DbHeader 32B][TrackRecord 512B][TrackRecord 512B]...
 *
 * Fixed-size records = O(1) random access: offset = 32 + id * 512
 */
#include <Arduino.h>
#include <FS.h>

// ── Data structures ───────────────────────────────────────────────

struct __attribute__((packed)) TrackRecord {
    uint32_t id;            // 4   = 4
    char     title[64];     // 64  = 68
    char     artist[64];    // 64  = 132
    char     album[64];     // 64  = 196
    char     genre[32];     // 32  = 228
    char     year[8];       // 8   = 236
    uint8_t  trackNum;      // 1   = 237
    uint8_t  discNum;       // 1   = 238
    uint8_t  flags;         // 1   = 239  bit0=hasEmbeddedArt
    uint8_t  _pad0;         // 1   = 240
    uint32_t durationSec;   // 4   = 244
    char     path[256];     // 256 = 500
    uint8_t  _pad1[12];     // 12  = 512 ✓
};
static_assert(sizeof(TrackRecord) == 512, "TrackRecord must be 512 bytes");

struct AlbumRecord {
    char artist[64];
    char album[64];
    char firstTrackPath[256]; 
};

struct __attribute__((packed)) DbHeader {
    char     magic[8];      // "NEBDB01\0"  8
    uint32_t version;       // 4   = 12
    uint32_t trackCount;    // 4   = 16
    uint32_t buildTime;     // 4   = 20
    uint8_t  reserved[12];  // 12  = 32 ✓
};
static_assert(sizeof(DbHeader) == 32, "DbHeader must be 32 bytes");

constexpr uint32_t   NEBDB_VERSION = 1;
constexpr const char NEBDB_MAGIC[] = "NEBDB01";
constexpr const char NEBDB_PATH[]  = "/nebula.db";

// ── MediaDB class ─────────────────────────────────────────────────

class MediaDB {
public:
    // Відкрити існуючу БД. Повертає false якщо файл відсутній або пошкоджений.
    bool open(fs::FS& sd) {
        close();
        _file = sd.open(NEBDB_PATH, "r");
        if (!_file) return false;
        DbHeader hdr;
        if (_file.read((uint8_t*)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) { close(); return false; }
        if (strncmp(hdr.magic, NEBDB_MAGIC, 7) != 0 || hdr.version != NEBDB_VERSION) {
            close(); return false;
        }
        _trackCount = hdr.trackCount;
        _valid = true;
        Serial.printf("[DB] Opened: %lu tracks\n", _trackCount);
        return true;
    }

    void close() {
        if (_file) _file.close();
        _valid = false; _trackCount = 0;
    }

    bool     isValid()       const { return _valid; }
    uint32_t trackCount()    const { return _trackCount; }

    // Читати один трек по ID (O(1))
    bool getTrack(uint32_t id, TrackRecord& out) {
        if (!_valid || id >= _trackCount) return false;
        uint32_t off = sizeof(DbHeader) + id * sizeof(TrackRecord);
        if (!_file.seek(off)) return false;
        return _file.read((uint8_t*)&out, sizeof(out)) == (int)sizeof(out);
    }

    // ── Ітератор (зчитує стримінгом, не завантажує в RAM) ────────
    // Callback: bool cb(const TrackRecord&) → false = зупинитись
    template<typename Cb>
    void forEach(Cb cb) {
        if (!_valid) return;
        _file.seek(sizeof(DbHeader));
        TrackRecord r;
        for (uint32_t i = 0; i < _trackCount; i++) {
            if (_file.read((uint8_t*)&r, sizeof(r)) != (int)sizeof(r)) break;
            if (!cb(r)) break;
        }
    }

    // ── Навігаційні запити ────────────────────────────────────────

    // Список унікальних артистів (відсортований). Повертає кількість.
    uint16_t getArtists(char out[][64], uint16_t maxOut) {
        uint16_t n = 0;
        forEach([&](const TrackRecord& r) {
            if (r.artist[0] == '\0') return true;
            for (uint16_t i = 0; i < n; i++)
                if (strncasecmp(out[i], r.artist, 63) == 0) return true;
            if (n < maxOut) { strncpy(out[n++], r.artist, 63); out[n-1][63]=0; }
            return true;
        });
        _sortStrings(out, n);
        return n;
    }

    // Список альбомів для артиста (відсортований). Повертає кількість.
    uint16_t getAlbums(const char* artist, char out[][64], uint16_t maxOut) {
        uint16_t n = 0;
        forEach([&](const TrackRecord& r) {
            if (strncasecmp(r.artist, artist, 63) != 0) return true;
            if (r.album[0] == '\0') return true;
            for (uint16_t i = 0; i < n; i++)
                if (strncasecmp(out[i], r.album, 63) == 0) return true;
            if (n < maxOut) { strncpy(out[n++], r.album, 63); out[n-1][63]=0; }
            return true;
        });
        _sortStrings(out, n);
        return n;
    }

    // Треки альбому (відсортовані по trackNum). Повертає кількість.
    uint16_t getAlbumTracks(const char* artist, const char* album,
                             TrackRecord* out, uint16_t maxOut) {
        uint16_t n = 0;
        forEach([&](const TrackRecord& r) {
            if (strncasecmp(r.artist, artist, 63) != 0) return true;
            if (strncasecmp(r.album,  album,  63) != 0) return true;
            if (n < maxOut) out[n++] = r;
            return true;
        });
        // Insertion sort by trackNum
        for (uint16_t i = 1; i < n; i++) {
            TrackRecord t = out[i]; uint16_t j = i;
            while (j > 0 && out[j-1].trackNum > t.trackNum) { out[j]=out[j-1]; j--; }
            out[j] = t;
        }
        return n;
    }

    // Список унікальних альбомів для Carousel (Cover Flow)
    uint16_t getAlbumsRecords(AlbumRecord* out, uint16_t maxOut) {
        uint16_t n = 0;
        forEach([&](const TrackRecord& r) {
            if (r.album[0] == '\0') return true;
            bool found = false;
            for (uint16_t i = 0; i < n; i++) {
                if (strcasecmp(out[i].album, r.album) == 0 && 
                    strcasecmp(out[i].artist, r.artist) == 0) {
                    found = true; break;
                }
            }
            if (!found && n < maxOut) {
                strncpy(out[n].artist, r.artist, 63);
                strncpy(out[n].album, r.album, 63);
                strncpy(out[n].firstTrackPath, r.path, 255);
                n++;
            }
            return true;
        });
        return n;
    }

private:
    fs::File _file;
    uint32_t _trackCount = 0;
    bool     _valid = false;

    static void _sortStrings(char arr[][64], uint16_t n) {
        for (uint16_t i = 1; i < n; i++) {
            char tmp[64]; strncpy(tmp, arr[i], 63); tmp[63]=0;
            uint16_t j = i;
            while (j > 0 && strcasecmp(arr[j-1], tmp) > 0) {
                strncpy(arr[j], arr[j-1], 63); j--;
            }
            strncpy(arr[j], tmp, 63);
        }
    }
};
