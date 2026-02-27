// tests/test_main.cpp - minimal test runner (no external dependencies)
//
// A simple "test framework" using assert-like macros.  Each test function
// returns true on success, false on failure.

#include "test_main.hpp"
#include <iostream>
#include <string>

namespace test {

static int s_pass = 0;
static int s_fail = 0;

void pass(const std::string& name) {
    std::cout << "  [PASS] " << name << "\n";
    ++s_pass;
}

void fail(const std::string& name, const std::string& msg) {
    std::cout << "  [FAIL] " << name << " : " << msg << "\n";
    ++s_fail;
}

int summary() {
    int total = s_pass + s_fail;
    std::cout << "\n========================================\n"
              << " Results: " << s_pass << "/" << total << " passed";
    if (s_fail > 0) std::cout << "  (" << s_fail << " FAILED)";
    std::cout << "\n========================================\n";
    return (s_fail == 0) ? 0 : 1;
}

} // namespace test

// Forward declarations for test suites
int testCoordinates();
int testCatalog();
int testAtmosphere();
int testTelescope();
int testProcedural();
int testDiscovery();

int main() {
    std::cout << "PARALLAX Unit Tests\n"
              << "========================================\n";

    std::cout << "\n[Coordinates]\n";
    testCoordinates();

    std::cout << "\n[Star Catalog]\n";
    testCatalog();

    std::cout << "\n[Atmosphere]\n";
    testAtmosphere();

    std::cout << "\n[Telescope]\n";
    testTelescope();

    std::cout << "\n[Procedural Generator]\n";
    testProcedural();

    std::cout << "\n[Discovery Engine]\n";
    testDiscovery();

    return test::summary();
}
