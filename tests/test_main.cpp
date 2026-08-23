// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <iostream>

int RunViewInjectionTests();
int RunConfigTests();
int RunBuildProfileTests();
int RunGameStateTests();

int main()
{
    std::cout << "Pinball FX Head Tracking Tests\n";
    std::cout << "==============================\n";

    int failures = 0;
    failures += RunViewInjectionTests();
    failures += RunConfigTests();
    failures += RunBuildProfileTests();
    failures += RunGameStateTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
