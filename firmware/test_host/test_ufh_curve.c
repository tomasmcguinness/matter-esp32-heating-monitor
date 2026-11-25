// Tests ufh_curve.h: a UFH manifold's flow from its calibration points (TASK-42.2).
#include <stdio.h>

#include "ufh_curve.h"

static int checks, failures;

#define CHECK_EQ(actual, expected)                                                                     \
    do {                                                                                               \
        unsigned long a_ = (unsigned long)(actual), e_ = (unsigned long)(expected);                    \
        checks++;                                                                                      \
        if (a_ != e_) {                                                                                \
            failures++;                                                                                \
            printf("%s:%d: %s = %lu, expected %lu\n", __FILE__, __LINE__, #actual, a_, e_);            \
        }                                                                                              \
    } while (0)

int main(void)
{
    // No points: no flow, and everything counts as extrapolated.
    CHECK_EQ(ufh_curve_flow(NULL, NULL, 0, 1000), 0);
    CHECK_EQ(ufh_curve_is_extrapolated(NULL, 0, 1000), 1);

    // One point is TASK-42.1's proportional scaling everywhere: 324 l/h at 1113.
    {
        uint16_t h[] = {1113};
        uint32_t m[] = {324};
        CHECK_EQ(ufh_curve_flow(h, m, 1, 1113), 324);
        CHECK_EQ(ufh_curve_flow(h, m, 1, 1268), 369); // 324 x 1268 / 1113 = 369.1
        CHECK_EQ(ufh_curve_flow(h, m, 1, 0), 0);
        CHECK_EQ(ufh_curve_is_extrapolated(h, 1, 1113), 0);
        CHECK_EQ(ufh_curve_is_extrapolated(h, 1, 1268), 1);
    }

    // Two points: the straight line between them, proportional outside.
    {
        uint16_t h[] = {1000, 1400};
        uint32_t m[] = {300, 340};
        CHECK_EQ(ufh_curve_flow(h, m, 2, 1000), 300);  // on a point
        CHECK_EQ(ufh_curve_flow(h, m, 2, 1400), 340);  // on the other
        CHECK_EQ(ufh_curve_flow(h, m, 2, 1200), 320);  // halfway
        CHECK_EQ(ufh_curve_flow(h, m, 2, 1100), 310);
        CHECK_EQ(ufh_curve_flow(h, m, 2, 500), 150);   // 300 x 500 / 1000
        CHECK_EQ(ufh_curve_flow(h, m, 2, 1750), 425);  // 340 x 1750 / 1400
        CHECK_EQ(ufh_curve_is_extrapolated(h, 2, 1200), 0);
        CHECK_EQ(ufh_curve_is_extrapolated(h, 2, 999), 1);
        CHECK_EQ(ufh_curve_is_extrapolated(h, 2, 1401), 1);
    }

    // Three points with a falling middle stretch: the manifold's share can drop as the total rises.
    {
        uint16_t h[] = {900, 1100, 1300};
        uint32_t m[] = {330, 320, 360};
        CHECK_EQ(ufh_curve_flow(h, m, 3, 1000), 325);  // between the first two, falling
        CHECK_EQ(ufh_curve_flow(h, m, 3, 1100), 320);  // on the middle point
        CHECK_EQ(ufh_curve_flow(h, m, 3, 1200), 340);  // between the last two, rising
        CHECK_EQ(ufh_curve_flow(h, m, 3, 450), 165);   // 330 x 450 / 900
    }

    printf("test_ufh_curve: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
