#pragma once

// nq_fmt.h - English (US/international) number formatting + tiny
// interpolation helpers shared by the 3 visually-ported pilot screens
// (s19_zen.cpp, s01_painel.cpp, s14_hashrate.cpp). Ports the small slice of
// the approved design reference this firmware actually needs (NQ.fmt.n0/
// n1/n2/ths and NQ.map/NQ.clamp), not the approved design's full NQ.fmt/NQ.* surface
// (diff/brl/dur/hm/oneIn and the odds/BTC-price helpers are used
// by OTHER screens, not these 3, and are out of this task's scope).
//
// Why header-only (no nq_fmt.cpp): main/CMakeLists.txt is explicitly
// off-limits in this pass (other agents are editing it in parallel to wire
// up the font.c files and this screen set), and every.cpp added to the
// build needs an explicit SRCS entry there. A header full of `inline`
// functions has no separate translation unit to register: it just gets
// included by the 3 screens'.cpp files, which are ALREADY in
// SCREEN_ROTATION_SRCS. Zero build-system changes required to use it.
//
// Why hand-rolled instead of libc: ESP-IDF's newlib does not ship a usable
// locale for this on-device (setlocale(LC_NUMERIC,.) is not available here).
// These helpers format with plain snprintf("%f") and do the comma-
// thousands/dot-decimal punctuation themselves.
//
// Hard rule enforced here for every one of these helpers: NAN in, "--" out.
// A UiState field that is not populated yet is NAN (see ui_state.h's
// top-of-file comment) and must never be displayed as an invented 0,00.

#include <cmath>
#include <cstdio>
#include <cstring>

namespace nqfmt
{

namespace detail
{

// Inserts ',' thousands separators into an ASCII decimal integer string
// (optional leading '-'), e.g. "32768" -> "32,768", "800" -> "800".
// `out` must be large enough (digits + 1 separator per 3 digits + sign +
// NUL); on the rare case it isn't, falls back to the ungrouped digits
// rather than truncating oddly -- every value these 3 screens show (MHz,
// mV, W, degC, GH/s/1000, sample counts) is at most a handful of digits, so
// this fallback path is not expected to trigger in practice.
inline void insertThousands(char *out, size_t outN, const char *digits)
{
    bool neg = (digits[0] == '-');
    const char *d = neg ? digits + 1 : digits;
    size_t len = strlen(d);
    if (len == 0 || outN == 0) {
        snprintf(out, outN, "%s", digits);
        return;
    }
    size_t groups = (len - 1) / 3;
    size_t outLen = len + groups + (neg ? 1 : 0);
    if (outLen + 1 > outN) {
        snprintf(out, outN, "%s", digits);
        return;
    }
    out[outLen] = '\0';
    size_t pos = outLen;
    size_t di = len;
    int sinceGroup = 0;
    while (di > 0) {
        out[--pos] = d[--di];
        if (++sinceGroup == 3 && di > 0) {
            out[--pos] = ',';
            sinceGroup = 0;
        }
    }
    if (neg) {
        out[--pos] = '-';
    }
}

} // namespace detail

// Longest decimal expansion these helpers ever have to hold. Source value
// is a `float`; its largest finite magnitude is FLT_MAX (~3.4028235e38),
// which "%.*f" expands to 39 integer digits (10^38 <= FLT_MAX < 10^39).
// Budget: 1 optional '-' sign + 39 integer digits + 1 '.' + up to 2
// fractional digits (the largest `decimals` any of num/n0/n1/n2/
// ths is called with here) + 1 NUL = 44, rounded up to 48. `raw` and
// `intPart` are both sized to this: `intPart` must be at least as large
// as `raw` because the no-decimal-point branch below copies the whole of
// `raw` into `intPart` verbatim, and a destination smaller than the
// source's own declared bound is exactly what -Werror=format-truncation
// (correctly) flags as a possible truncation.
constexpr size_t kNumBufMax = 48;

// v formatted with `decimals` digits, dot as decimal separator, comma as
// thousands separator on the integer part (English/international
// convention, e.g. "967,563" / "6.53" -- the approved design reference's NQ.fmt.n0/n1/n2/n3,
// merged into one function with a `decimals` argument). NAN -> "--".
inline void num(char *buf, size_t n, float v, int decimals)
{
    if (n == 0) {
        return;
    }
    if (std::isnan(v)) {
        snprintf(buf, n, "--");
        return;
    }
    char raw[kNumBufMax];
    snprintf(raw, sizeof(raw), "%.*f", decimals, (double) v);

    char *dot = strchr(raw, '.');
    char intPart[kNumBufMax];
    char fracPart[8] = "";
    if (dot) {
        size_t intLen = (size_t) (dot - raw);
        if (intLen >= sizeof(intPart)) {
            intLen = sizeof(intPart) - 1;
        }
        memcpy(intPart, raw, intLen);
        intPart[intLen] = '\0';
        snprintf(fracPart, sizeof(fracPart), "%s", dot + 1);
    } else {
        snprintf(intPart, sizeof(intPart), "%s", raw);
    }

    char grouped[32];
    detail::insertThousands(grouped, sizeof(grouped), intPart);

    if (decimals > 0) {
        snprintf(buf, n, "%s.%s", grouped, fracPart);
    } else {
        snprintf(buf, n, "%s", grouped);
    }
}

inline void n0(char *buf, size_t n, float v)
{
    num(buf, n, v, 0);
}
inline void n1(char *buf, size_t n, float v)
{
    num(buf, n, v, 1);
}
inline void n2(char *buf, size_t n, float v)
{
    num(buf, n, v, 2);
}

// GH/s -> TH/s with dot decimal (the approved design reference's NQ.fmt.ths). No thousands
// separator on the integer part: every hashrate this miner reports is a
// single-digit number of TH/s, so it would never actually trigger.
inline void ths(char *buf, size_t n, float ghs, int decimals)
{
    if (std::isnan(ghs)) {
        snprintf(buf, n, "--");
        return;
    }
    num(buf, n, ghs / 1000.0f, decimals);
}

// the approved design reference's NQ.clamp/NQ.map, used to size/position the zen bar and the
// 01-painel instrument tapes. NAN is treated as "unknown -> use the low end
// of the output range" (`c`) rather than propagating NaN into a widget
// width/position, since NQ.clamp's `v < a ? a : v > b ? b : v` comparisons
// against NaN are all false in JS too (leaving v itself, i.e. NaN) -- the
// the approved design never has to deal with that because its simulator never feeds NaN
// in; on real hardware a not-yet-valid field can, so this port adds the
// explicit fallback the approved design didn't need.
inline float clampf(float v, float a, float b)
{
    if (std::isnan(v)) {
        return a;
    }
    return v < a ? a : (v > b ? b : v);
}

inline float mapf(float v, float a, float b, float c, float d)
{
    if (std::isnan(v) || b == a) {
        return c;
    }
    float t = (v - a) / (b - a);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return c + (d - c) * t;
}

} // namespace nqfmt
