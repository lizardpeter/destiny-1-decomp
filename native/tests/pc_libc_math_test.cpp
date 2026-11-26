#include "pc_libc_math.h"
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
using namespace d1::pc_libc;
static void require(bool v) { if (!v) { std::fputs("native math regression failed\n", stderr); std::abort(); } }
int main() {
    require(exp_f(0) == 1 && pow_f(2, 10) == 1024 && exp2_f(-4) == 0.0625f);
    require(fmod_f(-7, 4) == -3 && fmod_d(-7, 4) == -3);
    require(pow_d(2, 40) == 1099511627776.0 && exp2_d(40) == 1099511627776.0);
    require(diff_time(INT64_C(4102444800), INT64_C(4102444790)) == 10);
    require(std::isnan(asin_f(2)) && std::isnan(acos_f(-2)));
    require(std::signbit(atan_f(-0.0f)) && std::signbit(tan_f(-0.0f)) && std::signbit(tan_d(-0.0)));
    require(std::signbit(atan2_f(-0.0f, 1.0f)));
    const int prior = std::fegetround();
    require(std::fesetround(FE_TONEAREST) == 0);
    volatile float half = 2.5f;
    require(lrint_f(half) == 2);
    require(std::fesetround(FE_UPWARD) == 0 && lrint_f(half) == 3);
    require(std::fesetround(FE_DOWNWARD) == 0 && lrint_f(half) == 2);
    require(std::fesetround(FE_TOWARDZERO) == 0 && lrint_f(-half) == -2);
    require(std::fesetround(prior) == 0);
    require(lrint_f(4294967296.0f) == INT64_C(4294967296));
    std::mt19937 rng(0xD10133);
    for (unsigned i = 0; i < 100000; ++i) {
        const float x = float(int(rng() % 2000000) - 1000000) / 8;
        int e = 0;
        const float fraction = frexp_f(x, &e);
        require(ldexp_f(fraction, e) == x);
        float whole = 0;
        const float part = modf_f(x, &whole);
        require(whole + part == x && std::abs(part) < 1);
        require(lrint_f(whole) == int64_t(whole));
    }
    std::puts("100000 native scalar math cases, rounding modes, 64-bit widths and boundary checks passed");
}
