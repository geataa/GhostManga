#include <windows.h>
#include <iostream>
#include <cassert>
#include "../src/engine/Scheduler.h"
#include "../src/core/Paths.h"
#include "../src/core/Log.h"

using namespace gm;

int main() {
    std::cout << "=============================================\n";
    std::cout << "  GhostManga M7 Automated Verification Tests \n";
    std::cout << "=============================================\n";

    std::wstring err;
    paths::Init(&err);
    log::Init();

    // TEST 1: Scheduler Start and Stop lifecycle
    std::cout << "[TEST 1] Testing Scheduler lifecycle (Start/Stop)...\n";
    {
        bool callbackCalled = false;
        engine::Scheduler::Instance().Start([&](const engine::UpdateNotification& notif) {
            callbackCalled = true;
        }, 120);

        assert(engine::Scheduler::Instance().IsRunning() == true);

        // Stop scheduler
        engine::Scheduler::Instance().Stop();
        assert(engine::Scheduler::Instance().IsRunning() == false);

        std::cout << "  -> Scheduler lifecycle: PASSED\n";
    }

    // TEST 2: CheckSeries with empty inputs
    std::cout << "[TEST 2] Testing CheckSeries validation...\n";
    {
        engine::UpdateNotification notif;
        bool res1 = engine::Scheduler::Instance().CheckSeries("", "https://manga.test", "", notif);
        assert(res1 == false);

        bool res2 = engine::Scheduler::Instance().CheckSeries("NonExistentMirror", "https://manga.test", "", notif);
        assert(res2 == false);

        std::cout << "  -> CheckSeries validation: PASSED\n";
    }

    std::cout << "\nALL M7 VERIFICATION TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
