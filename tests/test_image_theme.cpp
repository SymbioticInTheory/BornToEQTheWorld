// Checks the colours BEQ picks from a background image.
#include "../src/plugin/ImageTheme.h"

#include <cstdio>
#include <string>

using namespace bteq::imagetheme;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// An image made of `parts`: each is (colour, share of pixels).
static std::vector<uint8_t> image(std::initializer_list<std::pair<uint32_t, int>> parts) {
    std::vector<uint8_t> px;
    for (auto [c, count] : parts)
        for (int i = 0; i < count; ++i) {
            px.push_back((uint8_t)(c >> 16));
            px.push_back((uint8_t)(c >> 8));
            px.push_back((uint8_t)c);
        }
    return px;
}

static double hueOf(const Rgb& c) {
    double h, s, v;
    toHsv(c, h, s, v);
    return h;
}

int main() {
    // A dark blue night with a small orange glow: the glow is the accent (vivid beats big and dull).
    auto night = pick(image({ { 0x0B1530, 7000 }, { 0x1A2A55, 2500 }, { 0xF08A24, 500 } }));
    check(night.vivid, "night is vivid");
    check(hueDistance(hueOf(night.accent), 30) < 12, "night accent is orange");
    check(hueDistance(hueOf(night.fog), 225) < 20, "night fog is blue");
    check(luminance(night.fog) < 0.01, "fog stays nearly black");

    // A greyscale photo: light neutral accent.
    auto grey = pick(image({ { 0x202020, 5000 }, { 0x808080, 3000 }, { 0xD0D0D0, 2000 } }));
    check(!grey.vivid, "grey image is not vivid");
    double h, s, l;
    toHsl(grey.accent, h, s, l);
    check(s <= 0.1 + 1e-9 && l > 0.8, "grey accent is a light neutral");

    // Deep blue: the accent is lifted so it reads on dark panels.
    auto blue = pick(image({ { 0x0A1AA0, 8000 }, { 0x050510, 2000 } }));
    check(hueDistance(hueOf(blue.accent), 235) < 12, "blue accent keeps its hue");
    check(luminance(blue.accent) >= 0.16, "blue accent is bright enough");

    // Curves stay clear of the accent and of the gold target.
    for (const auto& c : { night, blue }) {
        check(hueDistance(hueOf(c.eq), hueOf(c.accent)) > 60, "EQ curve clear of the accent");
        check(hueDistance(hueOf(c.eq), 43) > 40, "EQ curve clear of the target");
        check(hueDistance(hueOf(c.measured), hueOf(c.eq)) > 30 || c.measured.r == c.measured.b, "measured clear of the EQ curve");
    }
    // Orange (sunset): the usual teal EQ, not lilac next to the slate measured curve.
    auto sunset = pick(image({ { 0x2A1446, 4000 }, { 0xF0963C, 3000 }, { 0x190C1E, 3000 } }));
    check(sunset.eq.g > sunset.eq.r && sunset.eq.g > sunset.eq.b, "teal EQ against orange");
    // A red image keeps the familiar teal EQ curve.
    auto red = pick(image({ { 0xB02020, 3000 }, { 0x101010, 7000 } }));
    check(hueDistance(hueOf(red.accent), 0) < 10, "red accent");
    check(red.eq.g > red.eq.r, "teal EQ against red");

    check(pick({}).vivid == false, "empty image does not crash");

    if (failures == 0) std::printf("image theme: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
