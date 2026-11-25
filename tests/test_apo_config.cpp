// Checks the Equalizer APO text helpers: filter lines in APO's syntax, and
// that switching BEQ on and off leaves config.txt exactly as it was.
#include "../src/plugin/ApoConfig.h"

#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}
static void same(const std::string& got, const std::string& want, const std::string& what) {
    check(got == want, what + "\n  got:  " + got + "\n  want: " + want);
}

int main() {
    using namespace bteq;
    same(apo::num(-6.1, 3), "-6.1", "num trims zeros");
    same(apo::num(-0.0001, 3), "0", "num has no negative zero");
    same(apo::num(105, 2), "105", "num whole number");

    same(apo::filterLine({ FilterType::LowShelf, 105, 6.4, 0.7, true }, 1.0), "Filter: ON LSC Fc 105 Hz Gain 6.4 dB Q 0.7", "low shelf");
    same(apo::filterLine({ FilterType::HighShelf, 10000, -2, 0.7071, true }, 0.5), "Filter: ON HSC Fc 10000 Hz Gain -1 dB Q 0.7071",
         "high shelf scaled by the correction amount");
    same(apo::filterLine({ FilterType::Peaking, 8800, 5.1, 1.42, true }, 1.0), "Filter: ON PK Fc 8800 Hz Gain 5.1 dB Q 1.42", "peaking");
    same(apo::filterLine({ FilterType::LowPass, 16000, 0, 0.71, true }, 1.0), "Filter: ON LPQ Fc 16000 Hz Q 0.71", "low pass");
    same(apo::graphicLine({ 20, 21, 22.5 }, { -0.2, 0, 1.25 }), "GraphicEQ: 20 -0.2; 21 0; 22.5 1.25", "graphic EQ");

    // A config.txt as Peace leaves it, with Unix line endings.
    const std::string peace = "# Peace config\nDevice: Headphones\nInclude: peace.txt\n\nPreamp: -3 dB\n";
    const auto taken = apo::takeOver(peace);
    same(taken, apo::kHeader + "\r\n" + apo::kInclude + "\r\n# Peace config\r\n" + apo::kMark + "Device: Headphones\r\n" + apo::kMark +
                    "Include: peace.txt\r\n\r\n" + apo::kMark + "Preamp: -3 dB\r\n",
         "take over comments out every active line");
    check(apo::isTakenOver(taken), "taken-over config is recognised");
    check(!apo::isTakenOver(peace), "Peace's config is not taken over");
    same(apo::takeOver(taken), taken, "taking over twice changes nothing");
    same(apo::restore(taken), "# Peace config\r\nDevice: Headphones\r\nInclude: peace.txt\r\n\r\nPreamp: -3 dB\r\n",
         "restore gives the original lines back");
    // A line Peace adds while BEQ is on gets switched off on the next start, and back on with the rest.
    const auto later = apo::takeOver(taken + "Include: peace.txt\r\n");
    check(apo::restore(later).find("Preamp: -3 dB\r\nInclude: peace.txt\r\n") != std::string::npos, "lines added later survive");
    check(later.find(apo::kMark + "Include: peace.txt\r\n" + apo::kMark) == std::string::npos, "new line marked once");

    std::printf(failures == 0 ? "All APO config checks passed\n" : "%d APO config checks failed\n", failures);
    return failures == 0 ? 0 : 1;
}
