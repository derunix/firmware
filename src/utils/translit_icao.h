#pragma once

#include <cstddef>

// Transliterate UTF-8 text to ASCII using an ICAO-style Cyrillic mapping plus basic Greek.
// - Non-ASCII outside the mapping becomes '?'.
// - Hard/soft signs are omitted; Greek accents are ignored.
// - Always NUL-terminates if out_cap > 0.
// Returns number of bytes written (excluding the NUL).
size_t translit_icao_ru_to_ascii(const char *utf8_in, char *out, size_t out_cap);
