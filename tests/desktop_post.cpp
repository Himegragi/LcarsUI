// Desktop::post through the public desktop interface: work from another thread runs once, in order, on the
// desktop thread inside run(), wakes an idle host, and is refused once the host has closed. On Linux run it
// in a native session (it opens a window); on Windows it is a CTest.
#include "isoduo/desktop.hpp"
#include "check.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace isoduo;
using test::check;

namespace {
void postedWorkRunsOnTheDesktopThread() {
    Ui ui;
    ScrollState scroll;
    auto label = ui.addLabel({.id = "status", .value = u"Waiting for posted work"}).value.value();
    DesktopConfig config{.title = u"Isoduo post verification", .width = 320, .height = 200};
    auto created = Desktop::create(ui, webAppearance(), [&] { return layout::column({layout::component(label)}); }, scroll, config);
    check(created.desktop != nullptr, "Open the native window");
    auto& desktop = *created.desktop;
    const auto owner = std::this_thread::get_id();
    std::vector<int> order;
    bool ranOnOwner = true;
    // Queued before run() starts: it waits for the loop.
    check(bool(desktop.post([&] { order.push_back(0); ranOnOwner = ranOnOwner && std::this_thread::get_id() == owner; })),
          "Work posted before run is accepted");
    check(desktop.post({}).status == Status::invalidValue, "Empty work is rejected");
    std::atomic<bool> stop{false}, accepted{true};
    // The worker waits until the host is idle in its native wait, so only the post can wake it.
    std::thread worker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        for (int i = 1; i <= 3; ++i) {
            const auto posted = desktop.post([&, i] {
                order.push_back(i);
                ranOnOwner = ranOnOwner && std::this_thread::get_id() == owner;
                if (i == 3) desktop.close();
            });
            if (!posted) accepted = false;
        }
    });
    // A watchdog fails the test instead of hanging when a post does not wake the host.
    std::thread watchdog([&] {
        for (int i = 0; i < 200 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!stop) {
            std::cerr << "FAIL posted work did not wake the idle desktop within 10 s\n";
            std::_Exit(1);
        }
    });
    const auto result = desktop.run();
    stop = true;
    worker.join();
    watchdog.join();
    check(accepted, "Work posted from another thread is accepted");
    check(result.status == Status::ok, "Posted close ends run");
    check(order == std::vector<int>{0, 1, 2, 3}, "Posted work runs once each, in the order posted");
    check(ranOnOwner, "Posted work runs on the desktop thread");
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> watch = token;
    bool ranLate = false;
    check(desktop.post([&ranLate, token = std::move(token)] { ranLate = true; }).status == Status::notFound,
          "A closed host refuses work");
    check(watch.expired() && !ranLate, "Refused work is destroyed without running");
}

void unrunWorkIsDiscardedWithTheHost() {
    Ui ui;
    ScrollState scroll;
    DesktopConfig config{.title = u"Isoduo post discard", .width = 320, .height = 200};
    auto created = Desktop::create(ui, webAppearance(), [] { return layout::column({}); }, scroll, config);
    check(created.desktop != nullptr, "Open the native window");
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> watch = token;
    bool ran = false;
    check(bool(created.desktop->post([&ran, token = std::move(token)] { ran = true; })), "Queue work that never runs");
    created.desktop.reset();
    check(watch.expired() && !ran, "Destroying the host destroys queued work without running it");
}
}

int main() {
    const test::Case cases[] = {
        {"posted work runs on the desktop thread", postedWorkRunsOnTheDesktopThread},
        {"unrun work is discarded with the host", unrunWorkIsDiscardedWithTheHost},
    };
    return test::run("desktop post", cases);
}
