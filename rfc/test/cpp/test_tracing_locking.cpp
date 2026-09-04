#include "catch.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <string>
#include <functional>

#include "erpl_tracing.hpp"

using namespace erpl;

namespace {

// Run `fn` on a DETACHED thread and report whether it finished in time.
//
// It must be detached: a std::async future joins in its destructor, so a watchdog
// built on it cannot escape the very deadlock it is meant to detect -- it hangs the
// test binary instead, with no diagnosis. That is also how this defect hid in
// production: it looks like a slow query, not a lock cycle.
bool CompletesWithin(std::chrono::milliseconds budget, std::function<void()> fn)
{
	auto done = std::make_shared<std::atomic<bool>>(false);
	std::thread([done, fn = std::move(fn)]() mutable {
		fn();
		done->store(true, std::memory_order_release);
	}).detach();

	auto deadline = std::chrono::steady_clock::now() + budget;
	while (std::chrono::steady_clock::now() < deadline) {
		if (done->load(std::memory_order_acquire)) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return done->load(std::memory_order_acquire);
}

} // namespace

TEST_CASE("Tracer setters do not deadlock while tracing is enabled", "[erpl_rfc][tracing]") {
	auto &tracer = ErplTracer::Instance();

	// The setters log their own change through Info(). SetEnabled scopes its lock and
	// logs after the scope; the others held trace_mutex across the Info() call, and
	// WriteToFile locks the same non-recursive mutex -- so any setter called while
	// tracing was writing to a file deadlocked the process.
	tracer.SetTraceDirectory("./trace");
	tracer.SetOutputMode("file");
	tracer.SetEnabled(true);

	REQUIRE(CompletesWithin(std::chrono::seconds(5), [&] {
		tracer.SetLevel(TraceLevel::DEBUG_LEVEL);
	}));
	REQUIRE(CompletesWithin(std::chrono::seconds(5), [&] {
		tracer.SetOutputMode("both");
	}));
	REQUIRE(CompletesWithin(std::chrono::seconds(5), [&] {
		tracer.SetMaxFileSize(4 * 1024 * 1024);
	}));
	REQUIRE(CompletesWithin(std::chrono::seconds(5), [&] {
		tracer.SetTraceDirectory("./trace");
	}));

	// The tracer must still work after all that.
	REQUIRE(CompletesWithin(std::chrono::seconds(5), [&] {
		tracer.Info("TEST", "still alive");
	}));

	tracer.SetEnabled(false);
}
