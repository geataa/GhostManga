#include "JsEngine.h"
#include "../core/Log.h"
#include "../core/Utf8.h"

namespace gm::engine {

int JsEngine::InterruptCallback(JSRuntime* rt, void* opaque) {
    auto* self = static_cast<JsEngine*>(opaque);
    if (self && self->m_cancelCheck && self->m_cancelCheck()) return 1;
    if (self && self->m_timeoutEnabled) {
        if (std::chrono::steady_clock::now() > self->m_deadline) {
            return 1; // Terminate execution
        }
    }
    return 0;
}

JsEngine::JsEngine() = default;

JsEngine::~JsEngine() {
    Shutdown();
}

bool JsEngine::Init(size_t memoryLimitBytes) {
    if (m_rt) return true;

    m_rt = JS_NewRuntime();
    if (!m_rt) return false;

    if (memoryLimitBytes > 0) {
        JS_SetMemoryLimit(m_rt, memoryLimitBytes);
    }

    // Set safe stack size limit (512 KB) to prevent C++ stack overflow crashes on recursive JS
    JS_SetMaxStackSize(m_rt, 512 * 1024);

    JS_SetInterruptHandler(m_rt, InterruptCallback, this);

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
        return false;
    }

    JS_SetContextOpaque(m_ctx, this);
    return true;
}

void JsEngine::Shutdown() {
    if (m_ctx) {
        JS_FreeContext(m_ctx);
        m_ctx = nullptr;
    }
    if (m_rt) {
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
    }
}

void JsEngine::SetTimeoutSeconds(int seconds) {
    if (seconds <= 0) {
        m_timeoutEnabled = false;
    } else {
        m_timeoutEnabled = true;
        m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    }
}

int JsEngine::RemainingMilliseconds() const {
    if (m_cancelCheck && m_cancelCheck()) return 0;
    if (!m_timeoutEnabled) return 30000;
    return (int)std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - std::chrono::steady_clock::now()).count());
}

JSValue JsEngine::Eval(const std::string& code, const std::string& filename, bool isModule) {
    if (!m_ctx) return JS_EXCEPTION;
    if (m_rt) JS_UpdateStackTop(m_rt);

    int flags = isModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
    JSValue val = JS_Eval(m_ctx, code.data(), code.size(), filename.c_str(), flags);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(m_ctx);
        std::string err = ToStr(exc);

        JSValue stack = JS_GetPropertyStr(m_ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            err += "\nStack:\n" + ToStr(stack);
        }
        JS_FreeValue(m_ctx, stack);
        JS_FreeValue(m_ctx, exc);

        log::Write(L"[JsEngine::Eval error] %ls", FromUtf8(err).c_str());
    }

    return val;
}

JSValue JsEngine::Call(JSValueConst func, JSValueConst this_obj, int argc, JSValueConst* argv) {
    if (!m_ctx) return JS_EXCEPTION;
    if (m_rt) JS_UpdateStackTop(m_rt);
    JSValue res = JS_Call(m_ctx, func, this_obj, argc, argv);
    if (JS_IsException(res)) {
        JSValue exc = JS_GetException(m_ctx);
        std::string err = ToStr(exc);
        JS_FreeValue(m_ctx, exc);
        log::Write(L"[JsEngine::Call error] %ls", FromUtf8(err).c_str());
    }
    return res;
}

JSValue JsEngine::CallMethod(JSValueConst obj, const std::string& method, const std::vector<JSValue>& args) {
    if (!m_ctx) return JS_EXCEPTION;
    if (m_rt) JS_UpdateStackTop(m_rt);
    JSValue func = JS_GetPropertyStr(m_ctx, obj, method.c_str());
    if (!JS_IsFunction(m_ctx, func)) {
        JS_FreeValue(m_ctx, func);
        return JS_EXCEPTION;
    }
    JSValue res = JS_Call(m_ctx, func, obj, (int)args.size(), const_cast<JSValue*>(args.data()));
    JS_FreeValue(m_ctx, func);
    if (JS_IsException(res)) {
        JSValue exc = JS_GetException(m_ctx);
        std::string err = ToStr(exc);
        JS_FreeValue(m_ctx, exc);
        log::Write(L"[JsEngine::CallMethod error %ls] %ls", FromUtf8(method).c_str(), FromUtf8(err).c_str());
    }
    return res;
}

int JsEngine::RunJobs() {
    if (!m_rt) return 0;
    JS_UpdateStackTop(m_rt);
    if (RemainingMilliseconds() > 0) {
        auto timers = Eval("if (globalThis.__pumpTimers) globalThis.__pumpTimers();", "<timers>");
        JS_FreeValue(m_ctx, timers);
    }
    int count = 0;
    JSContext* ctx = nullptr;
    while (RemainingMilliseconds() > 0) {
        int err = JS_ExecutePendingJob(m_rt, &ctx);
        if (err <= 0) {
            if (err < 0 && ctx) {
                JSValue exc = JS_GetException(ctx);
                log::Write(L"[JsEngine::RunJobs error] %ls", FromUtf8(ToStr(exc)).c_str());
                JS_FreeValue(ctx, exc);
            }
            break;
        }
        count++;
    }
    return count;
}

std::string JsEngine::ToStr(JSValueConst val) {
    if (!m_ctx) return "";
    const char* str = JS_ToCString(m_ctx, val);
    if (!str) return "";
    std::string s(str);
    JS_FreeCString(m_ctx, str);
    return s;
}

std::string JsEngine::ToJson(JSValueConst val) {
    if (!m_ctx) return "";
    JSValue j = JS_JSONStringify(m_ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(j)) {
        JS_FreeValue(m_ctx, JS_GetException(m_ctx));
        return "";
    }
    std::string s = ToStr(j);
    JS_FreeValue(m_ctx, j);
    return s;
}

JSValue JsEngine::FromJson(const std::string& jsonStr) {
    if (!m_ctx) return JS_EXCEPTION;
    return JS_ParseJSON(m_ctx, jsonStr.data(), jsonStr.size(), "<json>");
}

} // namespace gm::engine
