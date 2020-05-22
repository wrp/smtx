#include "sttm.h"

wchar_t CSET_US[MAXMAP]; /* "USASCII" */

#if defined(__STDC_ISO_10646__) || defined(WCHAR_IS_UNICODE)
wchar_t CSET_UK[MAXMAP] = { /* "United Kingdom" */
    [L'#'] = 0x00a3
};

wchar_t CSET_GRAPH[MAXMAP] = { /* Graphics Set One */
    [L'-'] = 0x2191,
    [L'}'] = 0x00a3,
    [L'~'] = 0x00b7,
    [L'{'] = 0x03c0,
    [L','] = 0x2190,
    [L'+'] = 0x2192,
    [L'.'] = 0x2193,
    [L'|'] = 0x2260,
    [L'>'] = 0x2265,
    [L'`'] = 0x25c6,
    [L'a'] = 0x2592,
    [L'b'] = 0x2409,
    [L'c'] = 0x240c,
    [L'd'] = 0x240d,
    [L'e'] = 0x240a,
    [L'f'] = 0x00b0,
    [L'g'] = 0x00b1,
    [L'h'] = 0x2592,
    [L'i'] = 0x2603,
    [L'j'] = 0x2518,
    [L'k'] = 0x2510,
    [L'l'] = 0x250c,
    [L'm'] = 0x2514,
    [L'n'] = 0x253c,
    [L'o'] = 0x23ba,
    [L'p'] = 0x23bb,
    [L'q'] = 0x2500,
    [L'r'] = 0x23bc,
    [L's'] = 0x23bd,
    [L't'] = 0x251c,
    [L'u'] = 0x2524,
    [L'v'] = 0x2534,
    [L'w'] = 0x252c,
    [L'x'] = 0x2502,
    [L'y'] = 0x2264,
    [L'z'] = 0x2265,
    [L'_'] = L' ',
    [L'0'] = 0x25ae
};

#else /* wchar_t doesn't map to Unicode */

wchar_t CSET_UK[] = { /* "United Kingdom" */
    [L'#'] = L'&'
};

wchar_t CSET_GRAPH[] = { /* Graphics Set One */
    [L'-'] = '^',
    [L'}'] = L'&',
    [L'~'] = L'o',
    [L'{'] = L'p',
    [L','] = L'<',
    [L'+'] = L'>',
    [L'.'] = L'v',
    [L'|'] = L'!',
    [L'>'] = L'>',
    [L'`'] = L'+',
    [L'a'] = L':',
    [L'b'] = L' ',
    [L'c'] = L' ',
    [L'd'] = L' ',
    [L'e'] = L' ',
    [L'f'] = L'\'',
    [L'g'] = L'#',
    [L'h'] = L'#',
    [L'i'] = L'i',
    [L'j'] = L'+',
    [L'k'] = L'+',
    [L'l'] = L'+',
    [L'm'] = L'+',
    [L'n'] = '+',
    [L'o'] = L'-',
    [L'p'] = L'-',
    [L'q'] = L'-',
    [L'r'] = L'-',
    [L's'] = L'_',
    [L't'] = L'+',
    [L'u'] = L'+',
    [L'v'] = L'+',
    [L'w'] = L'+',
    [L'x'] = L'|',
    [L'y'] = L'<',
    [L'z'] = L'>',
    [L'_'] = L' ',
    [L'0'] = L'#',
};

#endif
