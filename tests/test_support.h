// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <iostream>
#include <string>

// Same shape as cameraunlock-core/cpp/tests: no framework, each suite counts
// its own failures and main() sums them.

namespace pinballfx_tests
{
    inline void Check(int& failures, bool condition, const std::string& name)
    {
        if (condition) {
            std::cout << "  [PASS] " << name << "\n";
        } else {
            std::cout << "  [FAIL] " << name << "\n";
            ++failures;
        }
    }

    inline bool NearEqual(double a, double b, double eps = 1e-4)
    {
        return std::fabs(a - b) <= eps;
    }

    inline int Report(const char* suite, int failures)
    {
        if (failures == 0) {
            std::cout << suite << ": all passed\n";
        } else {
            std::cout << suite << ": " << failures << " failure(s)\n";
        }
        return failures;
    }
}
