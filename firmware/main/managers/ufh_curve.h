#pragma once

#include <stdint.h>

// A UFH manifold's flow at a given heat meter flow, from its calibration points (TASK-42.2).
//
// Each point i pairs the heat meter's flow when the loop gauges were read, h[i], with the manifold's
// flow then, m[i] (the loops summed), both l/h, and the points are sorted by h ascending with no two
// equal. Between two points the manifold's flow is the straight line through them; below the lowest
// or above the highest it is scaled in proportion from that end point, m x hm / h, so a single point
// gives the proportional scaling of TASK-42.1 everywhere.
//
// Plain C with no ESP-IDF in it, so test_host/ can test it on a desktop.
//
// Returns 0 if there are no points or the end point's h is 0.
static inline uint32_t ufh_curve_flow(const uint16_t *h, const uint32_t *m, uint8_t n, uint32_t hm)
{
    if (n == 0)
    {
        return 0;
    }

    uint8_t end = (hm <= h[0]) ? 0 : (hm >= h[n - 1] ? (uint8_t)(n - 1) : n);

    if (end < n)
    {
        return h[end] == 0 ? 0 : (uint32_t)((uint64_t)m[end] * hm / h[end]);
    }

    for (uint8_t i = 0; i + 1 < n; i++)
    {
        if (hm >= h[i] && hm <= h[i + 1])
        {
            int64_t span = (int64_t)h[i + 1] - h[i];
            int64_t flow = (int64_t)m[i] + ((int64_t)hm - h[i]) * ((int64_t)m[i + 1] - (int64_t)m[i]) / span;

            return flow < 0 ? 0 : (uint32_t)flow;
        }
    }

    return 0;
}

// Whether hm is outside the points, where the flow is only scaled from an end point.
static inline int ufh_curve_is_extrapolated(const uint16_t *h, uint8_t n, uint32_t hm)
{
    return n == 0 || hm < h[0] || hm > h[n - 1];
}
