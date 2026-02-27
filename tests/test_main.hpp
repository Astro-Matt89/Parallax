#pragma once
// tests/test_main.hpp - shared test utilities

#include <string>
#include <sstream>
#include <cmath>

namespace test {

void pass(const std::string& name);
void fail(const std::string& name, const std::string& msg);
int  summary();

inline void check(bool condition, const std::string& name,
                   const std::string& msg = "") {
    if (condition) pass(name);
    else           fail(name, msg.empty() ? "condition was false" : msg);
}

inline void checkNear(double a, double b, double tol,
                       const std::string& name) {
    if (std::abs(a - b) <= tol) {
        pass(name);
    } else {
        std::ostringstream ss;
        ss << a << " != " << b << " (tol=" << tol << ")";
        fail(name, ss.str());
    }
}

} // namespace test
