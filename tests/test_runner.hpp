#pragma once
// test_runner.hpp — minimal C++ test framework, zero dependencies
// Usage:
//   #include "test_runner.hpp"
//   TEST("my test") { ASSERT_EQ(1+1, 2); }
//   int main() { return npc::test::run_all(); }

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>
#include <cmath>
#include <stdexcept>

namespace npc {
namespace test {

// ─── Assertion failure exception ─────────────────────────────────────────────

struct AssertionFailed : std::exception {
    std::string msg;
    explicit AssertionFailed(std::string m) : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

// ─── Test case registry ───────────────────────────────────────────────────────

struct TestCase {
    std::string name;
    std::string file;
    int         line;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct TestRegistrar {
    TestRegistrar(const char* name, const char* file, int line,
                  std::function<void()> fn) {
        registry().push_back({name, file, line, fn});
    }
};

// ─── Assertion macros ─────────────────────────────────────────────────────────

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_TRUE failed: " #expr " (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_FALSE failed: " #expr " (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_EQ failed: " #a " [" << _a << "] != " #b " [" << _b \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_NE(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a == _b) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_NE failed: " #a " == " #b " [" << _a \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_LT(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a < _b)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_LT failed: " #a " [" << _a << "] >= " #b " [" << _b \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_LE(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a <= _b)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_LE failed: " #a " [" << _a << "] > " #b " [" << _b \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_GT(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a > _b)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_GT failed: " #a " [" << _a << "] <= " #b " [" << _b \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_GE(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a >= _b)) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_GE failed: " #a " [" << _a << "] < " #b " [" << _b \
            << "] (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { auto _a = (a); auto _b = (b); auto _e = (eps); \
         if (std::fabs(_a - _b) > _e) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_NEAR failed: |" #a " - " #b "| = " << std::fabs(_a-_b) \
            << " > eps=" << _e << " (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

#define ASSERT_THROWS(expr) \
    do { bool _threw = false; try { expr; } catch(...) { _threw = true; } \
         if (!_threw) { \
             throw ::npc::test::AssertionFailed( \
                 "ASSERT_THROWS: no exception thrown for: " #expr \
                 " (" __FILE__ ":" + std::to_string(__LINE__) + ")"); \
         } \
    } while(0)

#define ASSERT_NO_THROW(expr) \
    do { try { expr; } catch(std::exception& _ex) { \
        std::ostringstream _ss; \
        _ss << "ASSERT_NO_THROW: unexpected exception: " << _ex.what() \
            << " (" __FILE__ ":" << __LINE__ << ")"; \
        throw ::npc::test::AssertionFailed(_ss.str()); \
    }} while(0)

// ─── TEST macro ────────────────────────────────────────────────────────────────

#define NPC_TEST_CAT_(a, b) a##b
#define NPC_TEST_CAT(a, b)  NPC_TEST_CAT_(a, b)
#define TEST(name) \
    static void NPC_TEST_CAT(_test_fn_, __LINE__)(); \
    static ::npc::test::TestRegistrar NPC_TEST_CAT(_reg_, __LINE__)( \
        name, __FILE__, __LINE__, NPC_TEST_CAT(_test_fn_, __LINE__)); \
    static void NPC_TEST_CAT(_test_fn_, __LINE__)()

// ─── Test runner ───────────────────────────────────────────────────────────────

inline int run_all(bool verbose = false) {
    auto& tests = registry();
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    // ANSI colors (detect terminal)
    bool color = (std::getenv("NO_COLOR") == nullptr);
    auto green  = [&](const std::string& s){ return color ? "\033[32m" + s + "\033[0m" : s; };
    auto red    = [&](const std::string& s){ return color ? "\033[31m" + s + "\033[0m" : s; };
    auto yellow = [&](const std::string& s){ return color ? "\033[33m" + s + "\033[0m" : s; };
    auto bold   = [&](const std::string& s){ return color ? "\033[1m"  + s + "\033[0m" : s; };

    std::cout << bold("Running " + std::to_string(tests.size()) + " tests...\n") << std::flush;

    for (auto& tc : tests) {
        if (verbose)
            std::cout << "  [ RUN  ] " << tc.name << "\n";
        try {
            tc.fn();
            ++passed;
            if (verbose)
                std::cout << "  [" << green(" PASS ") << "] " << tc.name << "\n";
            else
                std::cout << green(".");
        } catch (const AssertionFailed& e) {
            ++failed;
            failures.push_back("[" + tc.name + "] " + e.msg);
            if (verbose)
                std::cout << "  [" << red(" FAIL ") << "] " << tc.name
                          << "\n         " << e.msg << "\n";
            else
                std::cout << red("F");
        } catch (const std::exception& e) {
            ++failed;
            failures.push_back("[" + tc.name + "] EXCEPTION: " + e.what());
            if (verbose)
                std::cout << "  [" << red(" ERR  ") << "] " << tc.name
                          << "\n         " << e.what() << "\n";
            else
                std::cout << red("E");
        }
    }

    if (!verbose) std::cout << "\n";

    if (!failures.empty()) {
        std::cout << "\n" << bold(red("Failures:")) << "\n";
        for (auto& f : failures)
            std::cout << "  " << red("✗") << " " << f << "\n";
    }

    std::cout << "\n" << bold("Results: ")
              << green(std::to_string(passed) + " passed")
              << ", "
              << (failed ? red(std::to_string(failed) + " failed")
                         : yellow("0 failed"))
              << " / " << tests.size() << " total\n";

    return failed == 0 ? 0 : 1;
}

} // namespace test
} // namespace npc
