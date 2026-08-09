// Conversion between the game's proprietary character encoding (charmap.txt)
// and plain ASCII, used by the AI dialog mod. Only the Latin subset the
// model is asked to produce is covered; unknown characters are skipped.
#include "global.h"
#include "ai/ai_text.h"

#define GC_SPACE   0x00
#define GC_DIGIT0  0xA1 // '0'..'9' -> 0xA1..0xAA
#define GC_EXCL    0xAB
#define GC_QUES    0xAC
#define GC_PERIOD  0xAD
#define GC_HYPHEN  0xAE
#define GC_ELLIPSIS 0xB0
#define GC_DQUOTE_L 0xB1
#define GC_DQUOTE_R 0xB2
#define GC_SQUOTE_L 0xB3
#define GC_SQUOTE_R 0xB4
#define GC_MALE     0xB5
#define GC_FEMALE   0xB6
#define GC_MONEY    0xB7
#define GC_COMMA    0xB8
#define GC_SLASH    0xBA
#define GC_UPPER_A  0xBB // 'A'..'Z' -> 0xBB..0xD4
#define GC_LOWER_A  0xD5 // 'a'..'z' -> 0xD5..0xEE
#define GC_COLON    0xF0
#define GC_LPAREN   0x5C
#define GC_RPAREN   0x5D
#define GC_CTRL_FC  0xFC // extended control code (1+ args)
#define GC_CTRL_FD  0xFD // placeholder (1 arg)
#define GC_LINE_N   0xFE // \n
#define GC_LINE_L   0xFA // \l (scroll)
#define GC_PARA     0xFB // \p (new paragraph)
#define GC_EOS      0xFF

// Characters per message-box line. FONT_NORMAL is variable-width; this is a
// conservative fixed-width estimate that fits the standard field text box.
#define AI_WRAP_COLUMNS 34

static char DecodeChar(u8 c)
{
    if (c == GC_SPACE) return ' ';
    if (c >= GC_DIGIT0 && c <= GC_DIGIT0 + 9) return '0' + (c - GC_DIGIT0);
    if (c >= GC_UPPER_A && c <= GC_UPPER_A + 25) return 'A' + (c - GC_UPPER_A);
    if (c >= GC_LOWER_A && c <= GC_LOWER_A + 25) return 'a' + (c - GC_LOWER_A);
    switch (c)
    {
    case GC_EXCL: return '!';
    case GC_QUES: return '?';
    case GC_PERIOD: return '.';
    case GC_HYPHEN: return '-';
    case GC_DQUOTE_L:
    case GC_DQUOTE_R: return '"';
    case GC_SQUOTE_L:
    case GC_SQUOTE_R: return '\'';
    case GC_MONEY: return '$';
    case GC_COMMA: return ',';
    case GC_SLASH: return '/';
    case GC_COLON: return ':';
    case GC_LPAREN: return '(';
    case GC_RPAREN: return ')';
    }
    return 0; // unknown -> skip
}

void AiText_DecodeGameToAscii(char *dest, int destSize, const u8 *src)
{
    int di = 0;

    if (destSize <= 0)
        return;
    while (src != NULL && *src != GC_EOS && di < destSize - 1)
    {
        u8 c = *src++;
        if (c == GC_LINE_N || c == GC_LINE_L)
        {
            if (di < destSize - 1) dest[di++] = ' ';
        }
        else if (c == GC_PARA)
        {
            if (di < destSize - 2) { dest[di++] = ' '; }
        }
        else if (c == GC_CTRL_FD)
        {
            u8 arg = *src++;
            const char *tok = (arg == 0x01) ? "{PLAYER}" : "{VAR}";
            while (*tok && di < destSize - 1)
                dest[di++] = *tok++;
            if (arg == GC_EOS)
                break;
        }
        else if (c == GC_CTRL_FC)
        {
            // Extended control code: skip the code byte and one argument.
            if (*src != GC_EOS) src++;
        }
        else if (c == GC_ELLIPSIS)
        {
            if (di < destSize - 3) { dest[di++] = '.'; dest[di++] = '.'; dest[di++] = '.'; }
        }
        else
        {
            char a = DecodeChar(c);
            if (a != 0)
                dest[di++] = a;
        }
    }
    dest[di] = '\0';
}

bool32 AiText_HasDynamicPlaceholders(const u8 *src)
{
    while (src != NULL && *src != GC_EOS)
    {
        u8 c = *src++;
        if (c == GC_CTRL_FD)
        {
            u8 arg = *src++;
            if (arg != 0x01) // anything but {PLAYER}
                return TRUE;
            if (arg == GC_EOS)
                break;
        }
        else if (c == GC_CTRL_FC)
        {
            if (*src != GC_EOS) src++;
        }
    }
    return FALSE;
}

static u8 EncodeChar(char a)
{
    if (a == ' ') return GC_SPACE;
    if (a >= '0' && a <= '9') return GC_DIGIT0 + (a - '0');
    if (a >= 'A' && a <= 'Z') return GC_UPPER_A + (a - 'A');
    if (a >= 'a' && a <= 'z') return GC_LOWER_A + (a - 'a');
    switch (a)
    {
    case '!': return GC_EXCL;
    case '?': return GC_QUES;
    case '.': return GC_PERIOD;
    case '-': return GC_HYPHEN;
    case '"': return GC_DQUOTE_R;
    case '\'': return GC_SQUOTE_R;
    case '$': return GC_MONEY;
    case ',': return GC_COMMA;
    case '/': return GC_SLASH;
    case ':': return GC_COLON;
    case '(': return GC_LPAREN;
    case ')': return GC_RPAREN;
    case ';': return GC_COMMA;
    }
    return GC_EOS; // unknown -> caller skips
}

// Map a UTF-8 sequence starting at *pp to an ASCII substitute, advancing *pp
// past it. Returns a short ASCII string ("" to drop). Lets styled/foreign
// replies (e.g. Italian or Sicilian) degrade to readable ASCII instead of
// vanishing, since the game charmap has no accented letters.
static const char *TransliterateUtf8(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned char c = p[0];

    // 2-byte: Latin-1 Supplement (accented Latin letters live in U+00C0..U+00FF).
    if (c == 0xC3 && p[1] >= 0x80)
    {
        unsigned char cp = p[1] + 0x40; // reconstruct U+00Cx..U+00FF low byte
        *pp += 2;
        switch (cp)
        {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return "O";
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return "o";
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
        case 0xD1: return "N"; case 0xF1: return "n";
        case 0xC7: return "C"; case 0xE7: return "c";
        default: return "";
        }
    }
    // 3-byte: common General Punctuation (smart quotes, dashes, ellipsis).
    if (c == 0xE2 && p[1] == 0x80)
    {
        unsigned char t = p[2];
        *pp += 3;
        switch (t)
        {
        case 0x98: case 0x99: return "'";   // ' '
        case 0x9C: case 0x9D: return "\"";  // " "
        case 0x93: case 0x94: return "-";   // en/em dash
        case 0xA6: return "...";            // ellipsis
        default: return "";
        }
    }
    // Unknown multibyte: drop just this byte.
    *pp += 1;
    return "";
}

int AiText_EncodeAsciiToGame(u8 *dest, int destSize, const char *src)
{
    char ascii[1024];
    int ai = 0;

    // First pass: fold whitespace and transliterate to plain ASCII.
    while (*src != '\0' && ai < (int)sizeof(ascii) - 4)
    {
        unsigned char c = (unsigned char)*src;
        if (c == '\n' || c == '\r' || c == '\t')
        {
            ascii[ai++] = ' ';
            src++;
        }
        else if (c >= 0x80)
        {
            const char *sub = TransliterateUtf8(&src);
            while (*sub && ai < (int)sizeof(ascii) - 4)
                ascii[ai++] = *sub++;
        }
        else
        {
            ascii[ai++] = *src++;
        }
    }
    ascii[ai] = '\0';
    src = ascii;

    {
    int di = 0;
    int col = 0;
    int line = 0;
    int lastBreak = -1; // index in dest where the last space of this line is

    while (*src != '\0' && di < destSize - 2)
    {
        char a = *src++;

        if (a == ' ')
        {
            if (col == 0)
                continue; // no leading spaces
            dest[di] = GC_SPACE;
            lastBreak = di;
            di++;
            col++;
        }
        else
        {
            u8 g = EncodeChar(a);
            if (g == GC_EOS)
                continue;
            dest[di++] = g;
            col++;
        }

        if (col >= AI_WRAP_COLUMNS && lastBreak >= 0)
        {
            // Replace the last space with a line-break control code.
            dest[lastBreak] = (line == 0) ? GC_LINE_N : GC_LINE_L;
            col = di - lastBreak - 1;
            lastBreak = -1;
            line++;
        }
    }
    dest[di++] = GC_EOS;
    return di;
    }
}
