/**
 * @file test_framework.hpp
 * @brief Minimal, dependency-free C++17 unit test framework for embedded Linux.
 */

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cmath>

namespace test_runner {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline bool register_test(const std::string& name, std::function<void()> func) {
    get_tests().push_back({name, func});
    return true;
}

#define TEST_CASE(name) \
    void test_func_##name(); \
    static bool registered_##name = test_runner::register_test(#name, test_func_##name); \
    void test_func_##name()

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "\n    [FAIL] " #expr " is false at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #expr); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " == " #b); \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " != " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " != " #b); \
        } \
    } while(0)

#define ASSERT_GE(a, b) \
    do { \
        if ((a) < (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " >= " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " >= " #b); \
        } \
    } while(0)

#define ASSERT_LE(a, b) \
    do { \
        if ((a) > (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " <= " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " <= " #b); \
        } \
    } while(0)

#define ASSERT_GT(a, b) \
    do { \
        if ((a) <= (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " > " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " > " #b); \
        } \
    } while(0)

#define ASSERT_LT(a, b) \
    do { \
        if ((a) >= (b)) { \
            std::cerr << "\n    [FAIL] Expected " #a " < " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: " #a " < " #b); \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::fabs((a) - (b)) > (eps)) { \
            std::cerr << "\n    [FAIL] Expected |" #a " - " #b "| <= " #eps \
                      << " (diff=" << std::fabs((a) - (b)) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw std::runtime_error("Assertion failed: near check"); \
        } \
    } while(0)

inline int run_all_tests(const std::string& suite_name) {
    std::cout << "\n=======================================================\n";
    std::cout << "  RUNNING TEST SUITE: " << suite_name << "\n";
    std::cout << "=======================================================\n";
    int passed = 0;
    int failed = 0;
    for (const auto& t : get_tests()) {
        std::cout << " [RUN ] " << t.name << "..." << std::flush;
        try {
            t.func();
            std::cout << " \033[32m[PASS]\033[0m\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << " \033[31m[FAIL]\033[0m (" << e.what() << ")\n";
            failed++;
        } catch (...) {
            std::cout << " \033[31m[FAIL]\033[0m (Unknown exception)\n";
            failed++;
        }
    }
    std::cout << "-------------------------------------------------------\n";
    std::cout << " Total: " << (passed + failed) << " | Passed: " << passed << " | Failed: " << failed << "\n";
    std::cout << "=======================================================\n\n";
    return (failed == 0) ? 0 : 1;
}

} // namespace test_runner
