#ifndef FORMAT_HELPERS_HPP
#define FORMAT_HELPERS_HPP

// ---------------------------------------------------------------------------
// format_helpers.hpp — pure math and format helpers with no platform
// dependencies.  Safe to include on any platform (Linux, macOS, Windows).
// Used by both the CLI and the future Qt6 GUI.
// ---------------------------------------------------------------------------

#include <string>
#include <cmath>
#include <cstdio>

namespace Pathmux {

// ---------------------------------------------------------------------------
// truncate — truncate string to maxCols, appending "..." if needed.
// All content is ASCII so size() == visible width.
// ---------------------------------------------------------------------------
inline std::string truncate(const std::string& s, int maxCols) {
    if ((int)s.size() <= maxCols) return s;
    return s.substr(0, maxCols - 3) + "...";
}

// ---------------------------------------------------------------------------
// utf8DisplayWidth — count display columns, not bytes.
// Multi-byte UTF-8 sequences count as 1 column each.
// ---------------------------------------------------------------------------
inline int utf8DisplayWidth(const std::string& s) {
    int cols = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        if      (c < 0x80) { ++i;      }   // 1-byte ASCII
        else if (c < 0xE0) { i += 2;   }   // 2-byte sequence
        else if (c < 0xF0) { i += 3;   }   // 3-byte sequence (em-dash etc.)
        else               { i += 4;   }   // 4-byte sequence
        ++cols;
    }
    return cols;
}

// ---------------------------------------------------------------------------
// Unit format helpers — store metric internally, display per useImperial pref.
// ---------------------------------------------------------------------------
inline std::string formatDistance(double km, bool imperial) {
    if (imperial) {
        double miles = km * 0.621371;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f mi", miles);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f km", km);
    return buf;
}

inline std::string formatSpeed(double kmh, bool imperial) {
    if (imperial) {
        double mph = kmh * 0.621371;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f mph", mph);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f km/h", kmh);
    return buf;
}

inline std::string formatAltitude(double metres, bool imperial) {
    if (imperial) {
        double feet = metres * 3.28084;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f ft", feet);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f m", metres);
    return buf;
}

// haversineKm — great-circle distance between two lat/lon points in kilometres
inline double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    const double R    = 6371.0;
    const double toRad = 3.14159265358979323846 / 180.0;
    double dLat = (lat2 - lat1) * toRad;
    double dLon = (lon2 - lon1) * toRad;
    double a = std::sin(dLat/2) * std::sin(dLat/2)
             + std::cos(lat1 * toRad) * std::cos(lat2 * toRad)
             * std::sin(dLon/2) * std::sin(dLon/2);
    return R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

} // namespace Pathmux

#endif
// SN: 00083
