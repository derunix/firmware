#include "utils/translit_icao.h"

#include <cstdint>
#include <cstring>

namespace
{
// ICAO-style mapping for Cyrillic (Russian plus common non-Russian letters) and basic Greek.
// Cyrillic:
// U+0410/U+0430 -> A/a
// U+0411/U+0431 -> B/b
// U+0412/U+0432 -> V/v
// U+0413/U+0433 -> G/g
// U+0414/U+0434 -> D/d
// U+0415/U+0435 -> E/e
// U+0401/U+0451 -> E/e (Yo treated as E)
// U+0416/U+0436 -> Zh/zh
// U+0417/U+0437 -> Z/z
// U+0418/U+0438 -> I/i
// U+0419/U+0439 -> I/i (short I)
// U+041A/U+043A -> K/k
// U+041B/U+043B -> L/l
// U+041C/U+043C -> M/m
// U+041D/U+043D -> N/n
// U+041E/U+043E -> O/o
// U+041F/U+043F -> P/p
// U+0420/U+0440 -> R/r
// U+0421/U+0441 -> S/s
// U+0422/U+0442 -> T/t
// U+0423/U+0443 -> U/u
// U+0424/U+0444 -> F/f
// U+0425/U+0445 -> Kh/kh
// U+0426/U+0446 -> Ts/ts
// U+0427/U+0447 -> Ch/ch
// U+0428/U+0448 -> Sh/sh
// U+0429/U+0449 -> Shch/shch
// U+042B/U+044B -> Y/y
// U+042D/U+044D -> E/e
// U+042E/U+044E -> Yu/yu
// U+042F/U+044F -> Ya/ya
// U+042A/U+044A -> "" (hard sign omitted)
// U+042C/U+044C -> "" (soft sign omitted)
// U+0404/U+0454 -> Ye/ye (Ukrainian/Belarusian Ye)
// U+0406/U+0456 -> I/i (Ukrainian/Belarusian Byelorussian I)
// U+0407/U+0457 -> Yi/yi (Ukrainian Yi)
// U+040E/U+045E -> U/u (Byelorussian short U)
// U+0490/U+0491 -> G/g (Ukrainian/Belarusian Ghe with upturn)
// U+0402/U+0452 -> Dj/dj (Serbian/Macedonian)
// U+0403/U+0453 -> Gj/gj (Macedonian)
// U+0405/U+0455 -> Dz/dz (Macedonian)
// U+0408/U+0458 -> J/j (Serbian/Macedonian)
// U+0409/U+0459 -> Lj/lj (Serbian)
// U+040A/U+045A -> Nj/nj (Serbian)
// U+040B/U+045B -> C/c (Serbian)
// U+040C/U+045C -> Kj/kj (Macedonian)
// U+040F/U+045F -> Dz/dz (Serbian/Macedonian)
// Greek: basic monotonic letters map to ASCII; tones/diaeresis are ignored.
// Beta -> V/v, Eta -> I/i, Theta -> Th/th, Chi -> Ch/ch, Psi -> Ps/ps.
static const char *map_icao_non_latin(uint32_t cp)
{
    switch (cp) {
    case 0x0410:
        return "A";
    case 0x0430:
        return "a";
    case 0x0411:
        return "B";
    case 0x0431:
        return "b";
    case 0x0412:
        return "V";
    case 0x0432:
        return "v";
    case 0x0413:
        return "G";
    case 0x0433:
        return "g";
    case 0x0414:
        return "D";
    case 0x0434:
        return "d";
    case 0x0415:
        return "E";
    case 0x0435:
        return "e";
    case 0x0401:
        return "E";
    case 0x0451:
        return "e";
    case 0x0416:
        return "Zh";
    case 0x0436:
        return "zh";
    case 0x0417:
        return "Z";
    case 0x0437:
        return "z";
    case 0x0418:
        return "I";
    case 0x0438:
        return "i";
    case 0x0419:
        return "I";
    case 0x0439:
        return "i";
    case 0x041A:
        return "K";
    case 0x043A:
        return "k";
    case 0x041B:
        return "L";
    case 0x043B:
        return "l";
    case 0x041C:
        return "M";
    case 0x043C:
        return "m";
    case 0x041D:
        return "N";
    case 0x043D:
        return "n";
    case 0x041E:
        return "O";
    case 0x043E:
        return "o";
    case 0x041F:
        return "P";
    case 0x043F:
        return "p";
    case 0x0420:
        return "R";
    case 0x0440:
        return "r";
    case 0x0421:
        return "S";
    case 0x0441:
        return "s";
    case 0x0422:
        return "T";
    case 0x0442:
        return "t";
    case 0x0423:
        return "U";
    case 0x0443:
        return "u";
    case 0x0424:
        return "F";
    case 0x0444:
        return "f";
    case 0x0425:
        return "Kh";
    case 0x0445:
        return "kh";
    case 0x0426:
        return "Ts";
    case 0x0446:
        return "ts";
    case 0x0427:
        return "Ch";
    case 0x0447:
        return "ch";
    case 0x0428:
        return "Sh";
    case 0x0448:
        return "sh";
    case 0x0429:
        return "Shch";
    case 0x0449:
        return "shch";
    case 0x042B:
        return "Y";
    case 0x044B:
        return "y";
    case 0x042D:
        return "E";
    case 0x044D:
        return "e";
    case 0x042E:
        return "Yu";
    case 0x044E:
        return "yu";
    case 0x042F:
        return "Ya";
    case 0x044F:
        return "ya";
    // Extended Cyrillic letters (non-Russian)
    case 0x0402:
        return "Dj";
    case 0x0452:
        return "dj";
    case 0x0403:
        return "Gj";
    case 0x0453:
        return "gj";
    case 0x0404:
        return "Ye";
    case 0x0454:
        return "ye";
    case 0x0405:
        return "Dz";
    case 0x0455:
        return "dz";
    case 0x0406:
        return "I";
    case 0x0456:
        return "i";
    case 0x0407:
        return "Yi";
    case 0x0457:
        return "yi";
    case 0x0408:
        return "J";
    case 0x0458:
        return "j";
    case 0x0409:
        return "Lj";
    case 0x0459:
        return "lj";
    case 0x040A:
        return "Nj";
    case 0x045A:
        return "nj";
    case 0x040B:
        return "C";
    case 0x045B:
        return "c";
    case 0x040C:
        return "Kj";
    case 0x045C:
        return "kj";
    case 0x040E:
        return "U";
    case 0x045E:
        return "u";
    case 0x040F:
        return "Dz";
    case 0x045F:
        return "dz";
    case 0x0490:
        return "G";
    case 0x0491:
        return "g";
    // Greek (monotonic, accents ignored)
    case 0x0386:
        return "A";
    case 0x03AC:
        return "a";
    case 0x0388:
        return "E";
    case 0x03AD:
        return "e";
    case 0x0389:
        return "I";
    case 0x03AE:
        return "i";
    case 0x038A:
        return "I";
    case 0x03AF:
        return "i";
    case 0x038C:
        return "O";
    case 0x03CC:
        return "o";
    case 0x038E:
        return "Y";
    case 0x03CD:
        return "y";
    case 0x038F:
        return "O";
    case 0x03CE:
        return "o";
    case 0x0390:
        return "i";
    case 0x03B0:
        return "y";
    case 0x03AA:
        return "I";
    case 0x03CA:
        return "i";
    case 0x03AB:
        return "Y";
    case 0x03CB:
        return "y";
    case 0x0391:
        return "A";
    case 0x03B1:
        return "a";
    case 0x0392:
        return "V";
    case 0x03B2:
        return "v";
    case 0x0393:
        return "G";
    case 0x03B3:
        return "g";
    case 0x0394:
        return "D";
    case 0x03B4:
        return "d";
    case 0x0395:
        return "E";
    case 0x03B5:
        return "e";
    case 0x0396:
        return "Z";
    case 0x03B6:
        return "z";
    case 0x0397:
        return "I";
    case 0x03B7:
        return "i";
    case 0x0398:
        return "Th";
    case 0x03B8:
        return "th";
    case 0x0399:
        return "I";
    case 0x03B9:
        return "i";
    case 0x039A:
        return "K";
    case 0x03BA:
        return "k";
    case 0x039B:
        return "L";
    case 0x03BB:
        return "l";
    case 0x039C:
        return "M";
    case 0x03BC:
        return "m";
    case 0x039D:
        return "N";
    case 0x03BD:
        return "n";
    case 0x039E:
        return "X";
    case 0x03BE:
        return "x";
    case 0x039F:
        return "O";
    case 0x03BF:
        return "o";
    case 0x03A0:
        return "P";
    case 0x03C0:
        return "p";
    case 0x03A1:
        return "R";
    case 0x03C1:
        return "r";
    case 0x03A3:
        return "S";
    case 0x03C3:
        return "s";
    case 0x03C2:
        return "s";
    case 0x03A4:
        return "T";
    case 0x03C4:
        return "t";
    case 0x03A5:
        return "Y";
    case 0x03C5:
        return "y";
    case 0x03A6:
        return "F";
    case 0x03C6:
        return "f";
    case 0x03A7:
        return "Ch";
    case 0x03C7:
        return "ch";
    case 0x03A8:
        return "Ps";
    case 0x03C8:
        return "ps";
    case 0x03A9:
        return "O";
    case 0x03C9:
        return "o";
    case 0x042A:
    case 0x044A:
    case 0x042C:
    case 0x044C:
        return "";
    default:
        return nullptr;
    }
}

static bool decode_utf8(const char *s, uint32_t *cp, size_t *consumed)
{
    const uint8_t b0 = static_cast<uint8_t>(s[0]);
    if (b0 < 0x80) {
        *cp = b0;
        *consumed = 1;
        return true;
    }

    if ((b0 & 0xE0) == 0xC0) {
        if (s[1] == '\0')
            return false;
        const uint8_t b1 = static_cast<uint8_t>(s[1]);
        if ((b1 & 0xC0) != 0x80)
            return false;
        const uint32_t code = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        if (code < 0x80)
            return false;
        *cp = code;
        *consumed = 2;
        return true;
    }

    if ((b0 & 0xF0) == 0xE0) {
        if (s[1] == '\0' || s[2] == '\0')
            return false;
        const uint8_t b1 = static_cast<uint8_t>(s[1]);
        const uint8_t b2 = static_cast<uint8_t>(s[2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
            return false;
        const uint32_t code = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        if (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))
            return false;
        *cp = code;
        *consumed = 3;
        return true;
    }

    if ((b0 & 0xF8) == 0xF0) {
        if (s[1] == '\0' || s[2] == '\0' || s[3] == '\0')
            return false;
        const uint8_t b1 = static_cast<uint8_t>(s[1]);
        const uint8_t b2 = static_cast<uint8_t>(s[2]);
        const uint8_t b3 = static_cast<uint8_t>(s[3]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
            return false;
        const uint32_t code = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        if (code < 0x10000 || code > 0x10FFFF)
            return false;
        *cp = code;
        *consumed = 4;
        return true;
    }

    return false;
}

static bool append_char(char *out, size_t out_cap, size_t *out_len, char c)
{
    if (*out_len + 1 >= out_cap)
        return false;
    out[(*out_len)++] = c;
    return true;
}

static bool append_str(char *out, size_t out_cap, size_t *out_len, const char *s)
{
    const size_t len = std::strlen(s);
    if (*out_len + len >= out_cap)
        return false;
    std::memcpy(out + *out_len, s, len);
    *out_len += len;
    return true;
}
} // namespace

size_t translit_icao_ru_to_ascii(const char *utf8_in, char *out, size_t out_cap)
{
    if (!out || out_cap == 0) {
        return 0;
    }

    size_t out_len = 0;
    out[0] = '\0';

    if (!utf8_in)
        return 0;

    const char *p = utf8_in;
    while (*p) {
        uint32_t cp = 0;
        size_t consumed = 0;

        if (!decode_utf8(p, &cp, &consumed)) {
            if (!append_char(out, out_cap, &out_len, '?'))
                break;
            p += 1;
            continue;
        }

        p += consumed;

        if (cp < 0x80) {
            if (!append_char(out, out_cap, &out_len, static_cast<char>(cp)))
                break;
            continue;
        }

        const char *mapped = map_icao_non_latin(cp);
        if (mapped) {
            if (!append_str(out, out_cap, &out_len, mapped))
                break;
        } else {
            if (!append_char(out, out_cap, &out_len, '?'))
                break;
        }
    }

    if (out_cap > 0)
        out[out_len] = '\0';
    return out_len;
}
