#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include "../../third_party/quickjs/quickjs.h"

namespace gm::engine {

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    bool Init(size_t memoryLimitBytes = 64 * 1024 * 1024);
    void Shutdown();

    // Evaluate JavaScript string. Returns JSValue (caller is responsible for JS_FreeValue).
    JSValue Eval(const std::string& code, const std::string& filename = "<eval>", bool isModule = false);

    // Call a function or method
    JSValue Call(JSValueConst func, JSValueConst this_obj, int argc, JSValueConst* argv);
    JSValue CallMethod(JSValueConst obj, const std::string& method, const std::vector<JSValue>& args);

    // Drain all pending microtasks / Promise jobs
    int RunJobs();

    // Set execution timeout (0 = disabled)
    void SetTimeoutSeconds(int seconds);
    int RemainingMilliseconds() const;
    void SetCancelCheck(std::function<bool()> check = {}) { m_cancelCheck = std::move(check); }

    // Helper conversions
    std::string ToStr(JSValueConst val);
    std::string ToJson(JSValueConst val);
    JSValue     FromJson(const std::string& jsonStr);

    JSContext* Ctx() const { return m_ctx; }
    JSRuntime* Rt() const { return m_rt; }

private:
    static int InterruptCallback(JSRuntime* rt, void* opaque);

    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
    std::chrono::steady_clock::time_point m_deadline{};
    bool m_timeoutEnabled = false;
    std::function<bool()> m_cancelCheck;
};

} // namespace gm::engine
