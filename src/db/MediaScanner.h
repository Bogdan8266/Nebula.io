#pragma once
/**
 * MediaScanner.h — Сканер SD карти для побудови /nebula.db
 *
 * Використання:
 *   MediaScanner scanner;
 *   scanner.scan(sd, [](const MediaScanner::Progress& p) {
 *       Serial.printf("Scanned: %lu | %s\n", p.scanned, p.currentFile);
 *   });
 */
#include <Arduino.h>
#include <FS.h>
#include "MediaDB.h"
#include "MetaReader.h"
#include "ArtExtractor.h"

class MediaScanner {
public:
    struct Progress {
        uint32_t scanned;       // знайдено треків
        uint32_t skipped;       // пропущено (не музика / помилка)
        char     currentFile[64]; // ім'я поточного файлу
    };

    using ProgressCb = void (*)(const Progress&);

    /**
     * Сканує rootDir (типово "/music"), будує /nebula.db.
     * Повертає кількість записаних треків (0 = помилка).
     */
    uint32_t scan(fs::FS& sd, const char* rootDir = "/music",
                  ProgressCb progressCb = nullptr) {
        _sd = &sd;
        _cb = progressCb;
        _progress = {0, 0, {}};

        // Відкрити DB файл для запису
        fs::File dbFile = sd.open(NEBDB_PATH, "w");
        if (!dbFile) {
            Serial.println("[SCAN] ERROR: cannot create /nebula.db");
            return 0;
        }

        // Записати порожній заголовок (trackCount = 0 поки що)
        DbHeader hdr = {};
        strncpy(hdr.magic, NEBDB_MAGIC, 7);
        hdr.version    = NEBDB_VERSION;
        hdr.trackCount = 0;
        hdr.buildTime  = 0;
        dbFile.write((uint8_t*)&hdr, sizeof(hdr));

        // Переконатись що /art/ існує
        ArtExtractor::ensureArtDir(sd);

        // Скан
        fs::File root = sd.open(rootDir);
        if (root && root.isDirectory()) {
            _scanDir(root, rootDir, dbFile);
            root.close();
        } else {
            // Спробуємо корінь SD якщо /music не знайдено
            Serial.printf("[SCAN] '%s' not found, scanning SD root\n", rootDir);
            root = sd.open("/");
            if (root && root.isDirectory()) {
                _scanDir(root, "", dbFile);
                root.close();
            }
        }

        // Оновити заголовок з фінальним trackCount
        dbFile.seek(0);
        hdr.trackCount = _progress.scanned;
        dbFile.write((uint8_t*)&hdr, sizeof(hdr));
        dbFile.close();

        Serial.printf("[SCAN] Done: %lu tracks, %lu skipped\n",
                      _progress.scanned, _progress.skipped);
        return _progress.scanned;
    }

private:
    fs::FS*    _sd  = nullptr;
    ProgressCb _cb  = nullptr;
    Progress   _progress = {};
    uint32_t   _nextId   = 0;

    static bool _isMusicFile(const char* name) {
        const char* ext = strrchr(name, '.');
        if (!ext) return false;
        return strcasecmp(ext, ".flac") == 0 ||
               strcasecmp(ext, ".mp3")  == 0 ||
               strcasecmp(ext, ".wav")  == 0 ||
               strcasecmp(ext, ".aac")  == 0 ||
               strcasecmp(ext, ".ogg")  == 0;
    }

    static bool _isSkippableDir(const char* name) {
        // Пропускаємо системні й приховані папки
        return name[0] == '.' ||
               strcasecmp(name, "System Volume Information") == 0 ||
               strcasecmp(name, "RECYCLER") == 0 ||
               strcasecmp(name, "$RECYCLE.BIN") == 0;
    }

    void _scanDir(fs::File& dir, const char* basePath, fs::File& dbFile, int depth = 0) {
        if (depth > 8) return; // Захист від надмірної рекурсії

        fs::File entry = dir.openNextFile();
        while (entry) {
            const char* name = entry.name();

            if (entry.isDirectory()) {
                if (!_isSkippableDir(name)) {
                    // Будуємо підшлях
                    char subPath[256] = {};
                    snprintf(subPath, sizeof(subPath), "%s/%s", basePath, name);
                    _scanDir(entry, subPath, dbFile, depth + 1);
                }
                entry.close();
                entry = dir.openNextFile();
                continue;
            }

            if (!_isMusicFile(name)) {
                _progress.skipped++;
                entry.close();
                continue;
            }

            // Побудувати повний шлях
            char fullPath[256] = {};
            snprintf(fullPath, sizeof(fullPath), "%s/%s", basePath, name);

            // Зчитати метадату
            TrackRecord rec = {};
            rec.id = _nextId;
            MetaReader::read(*_sd, fullPath, rec);

            // Якщо title досі пустий — використати ім'я файлу
            if (rec.title[0] == '\0') {
                strncpy(rec.title, name, 63);
                // Прибрати розширення
                char* dot = strrchr(rec.title, '.');
                if (dot) *dot = '\0';
            }

            // ── Art extraction (тільки FLAC) ─────────────────────
            const char* ext2 = strrchr(fullPath, '.');
            if (ext2 && strcasecmp(ext2, ".flac") == 0 &&
                rec.artist[0] != '\0' && rec.album[0] != '\0') {
                char artPath[256] = {};
                ArtExtractor::getArtPath(rec.artist, rec.album, artPath, sizeof(artPath));
                if (ArtExtractor::artExists(*_sd, artPath)) {
                    rec.flags |= 0x01; // art вже є
                } else if (ArtExtractor::extractFromFlac(*_sd, fullPath, artPath)) {
                    rec.flags |= 0x01; // art успішно витягнуто
                }
            }

            // Записати запис у DB (Тут важливо, щоб всі прапорці вже були встановлені)
            dbFile.write((uint8_t*)&rec, sizeof(rec));
            _nextId++;
            _progress.scanned++;

            // Callback кожні 10 файлів
            if (((_progress.scanned) % 10) == 0 || _progress.scanned == 1) {
                strncpy(_progress.currentFile, name, 63);
                if (_cb) _cb(_progress);
            }

            entry.close();
            entry = dir.openNextFile();
        }
    }
};
