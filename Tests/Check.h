#pragma once

#include <cmath>
#include <iostream>

namespace test
{
inline int checks = 0;
inline int failures = 0;
}

#define CHECK(condition) \
    do \
    { \
        ++test::checks; \
        if (! (condition)) \
        { \
            ++test::failures; \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
        } \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance) \
    do \
    { \
        ++test::checks; \
        const auto checkActual = (actual); \
        const auto checkExpected = (expected); \
        const auto checkTolerance = (tolerance); \
        if (std::abs (checkActual - checkExpected) > checkTolerance) \
        { \
            ++test::failures; \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK_NEAR failed: " \
                      << checkActual << " vs " << checkExpected \
                      << " (tolerance " << checkTolerance << ")\n"; \
        } \
    } while (false)

inline int reportAndExit()
{
    std::cout << test::checks << " checks, " << test::failures << " failures\n";
    return test::failures == 0 ? 0 : 1;
}
