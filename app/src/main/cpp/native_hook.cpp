#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "plugins/dump/dump.h"
#include "plugins/cocos/cocos_runtime.h"
#include "plugins/textExtractor/textExtractor.h"
#include "utils/db.h"
#include "utils/log.h"

namespace {
std::atomic_bool g_initialized{false};
std::string g_process_name = "unknown";

enum class GameEngine : std::int32_t {
    kUnityIl2Cpp = 0,
    kCocos2dxLua = 1,
};

GameEngine GameEngineFromOrdinal(std::int32_t value) {
    switch (value) {
        case static_cast<std::int32_t>(GameEngine::kCocos2dxLua):
            return GameEngine::kCocos2dxLua;
        case static_cast<std::int32_t>(GameEngine::kUnityIl2Cpp):
        default:
            return GameEngine::kUnityIl2Cpp;
    }
}

std::string ReadStringArrayItem(JNIEnv* env, jobjectArray values, jsize index) {
    if (env == nullptr || values == nullptr || index < 0 || index >= env->GetArrayLength(values)) {
        return {};
    }
    auto item = static_cast<jstring>(env->GetObjectArrayElement(values, index));
    if (item == nullptr) {
        return {};
    }
    const char* raw = env->GetStringUTFChars(item, nullptr);
    std::string result;
    if (raw != nullptr) {
        result.assign(raw);
        env->ReleaseStringUTFChars(item, raw);
    }
    env->DeleteLocalRef(item);
    return result;
}
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_init(JNIEnv* env, jclass /*clazz*/, jstring processName) {
    if (processName != nullptr) {
        const char* utf = env->GetStringUTFChars(processName, nullptr);
        if (utf != nullptr) {
            g_process_name.assign(utf);
            env->ReleaseStringUTFChars(processName, utf);
        }
    }

    dump_plugin::SetProcess(g_process_name);
    cocos_runtime::Init(g_process_name);

    if (g_initialized.exchange(true)) {
        LOGI("[%s] Native init already completed", g_process_name.c_str());
        return;
    }

    LOGI("[%s] Native init start", g_process_name.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setHookFramework(JNIEnv* /*env*/, jclass /*clazz*/, jint framework) {
    cocos_runtime::SetHookBackend(static_cast<std::int32_t>(framework));
    text_extractor::SetHookBackend(static_cast<std::int32_t>(framework));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setGameEngine(JNIEnv* /*env*/, jclass /*clazz*/, jint engine) {
    const GameEngine game_engine = GameEngineFromOrdinal(static_cast<std::int32_t>(engine));
    cocos_runtime::SetEnabled(game_engine == GameEngine::kCocos2dxLua);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setTextReplacement(JNIEnv* env, jclass /*clazz*/, jboolean enabled, jstring text) {
    const char* raw = text != nullptr ? env->GetStringUTFChars(text, nullptr) : nullptr;
    std::string value;
    if (raw != nullptr) {
        value.assign(raw);
        env->ReleaseStringUTFChars(text, raw);
    }
    text_extractor::SetTextReplacement(enabled == JNI_TRUE, value);
    cocos_runtime::SetTextReplacement(enabled == JNI_TRUE, value);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setTextDbResetOnStart(JNIEnv* /*env*/, jclass /*clazz*/, jboolean enabled) {
    textdb::SetResetOnMainProcess(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosTextCaptureEnabled(JNIEnv* /*env*/, jclass /*clazz*/, jboolean enabled) {
    cocos_runtime::SetRuntimeTextCaptureEnabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosTextPersistence(JNIEnv* /*env*/,
                                                          jclass /*clazz*/,
                                                          jboolean enabled,
                                                          jboolean chineseOnly) {
    cocos_runtime::SetTextPersistence(enabled == JNI_TRUE, chineseOnly == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosTextReplacementDelay(JNIEnv* /*env*/,
                                                                jclass /*clazz*/,
                                                                jboolean enabled,
                                                                jint delayMs) {
    cocos_runtime::SetTextReplacementDelay(enabled == JNI_TRUE, static_cast<std::int32_t>(delayMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosBurstDelayGuard(JNIEnv* /*env*/,
                                                           jclass /*clazz*/,
                                                           jboolean enabled,
                                                           jint holdMs) {
    cocos_runtime::SetTextBurstDelayGuard(
        enabled == JNI_TRUE,
        static_cast<std::int32_t>(holdMs));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosFontReplacement(JNIEnv* env,
                                                           jclass /*clazz*/,
                                                           jboolean enabled,
                                                           jstring ttfPath,
                                                           jstring bmfontFntPath) {
    cocos_runtime::FontReplacementConfig config;
    config.enabled = enabled == JNI_TRUE;
    const char* ttf_raw = ttfPath != nullptr ? env->GetStringUTFChars(ttfPath, nullptr) : nullptr;
    if (ttf_raw != nullptr) {
        config.ttf_path.assign(ttf_raw);
        env->ReleaseStringUTFChars(ttfPath, ttf_raw);
    }
    const char* bmfont_raw = bmfontFntPath != nullptr ? env->GetStringUTFChars(bmfontFntPath, nullptr) : nullptr;
    if (bmfont_raw != nullptr) {
        config.bmfont_fnt_path.assign(bmfont_raw);
        env->ReleaseStringUTFChars(bmfontFntPath, bmfont_raw);
    }
    cocos_runtime::SetFontReplacement(std::move(config));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setCocosLuaReplacementRules(JNIEnv* env,
                                                               jclass /*clazz*/,
                                                               jbooleanArray enabledValues,
                                                               jobjectArray scriptNames,
                                                               jobjectArray replacedCodes,
                                                               jobjectArray replaceCodes,
                                                               jobjectArray prependCodes) {
    if (enabledValues == nullptr || scriptNames == nullptr || replacedCodes == nullptr ||
        replaceCodes == nullptr || prependCodes == nullptr) {
        cocos_runtime::UpdateLuaReplacementRules({});
        return;
    }

    const jsize count = std::min({
        env->GetArrayLength(enabledValues),
        env->GetArrayLength(scriptNames),
        env->GetArrayLength(replacedCodes),
        env->GetArrayLength(replaceCodes),
        env->GetArrayLength(prependCodes),
    });

    std::vector<cocos_runtime::LuaReplacementRule> rules;
    rules.reserve(static_cast<size_t>(count));
    for (jsize index = 0; index < count; ++index) {
        jboolean enabled = JNI_TRUE;
        env->GetBooleanArrayRegion(enabledValues, index, 1, &enabled);
        cocos_runtime::LuaReplacementRule rule;
        rule.enabled = enabled == JNI_TRUE;
        rule.script_name = ReadStringArrayItem(env, scriptNames, index);
        rule.replaced_code = ReadStringArrayItem(env, replacedCodes, index);
        rule.replace_code = ReadStringArrayItem(env, replaceCodes, index);
        rule.prepend_code = ReadStringArrayItem(env, prependCodes, index);
        if (!rule.script_name.empty() || !rule.replaced_code.empty() ||
            !rule.replace_code.empty() || !rule.prepend_code.empty()) {
            rules.push_back(std::move(rule));
        }
    }
    cocos_runtime::UpdateLuaReplacementRules(std::move(rules));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_startCocosRuntimeIfNeeded(JNIEnv* /*env*/, jclass /*clazz*/) {
    cocos_runtime::StartRuntimeIfNeeded();
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setTargetsJson(JNIEnv* env, jclass /*clazz*/, jstring json) {
    const char* raw = json != nullptr ? env->GetStringUTFChars(json, nullptr) : nullptr;
    std::string text;
    if (raw != nullptr) {
        text.assign(raw);
        env->ReleaseStringUTFChars(json, raw);
    }
    text_extractor::Init(g_process_name);
    text_extractor::UpdateTargetsJson(text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_setTargets(JNIEnv* env, jclass /*clazz*/, jobjectArray targets) {
    if (targets == nullptr) {
        return;
    }

    const jsize len = env->GetArrayLength(targets);
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(len));

    for (jsize i = 0; i < len; ++i) {
        auto str = static_cast<jstring>(env->GetObjectArrayElement(targets, i));
        if (str == nullptr) {
            continue;
        }
        const char* utf = env->GetStringUTFChars(str, nullptr);
        if (utf != nullptr) {
            names.emplace_back(utf);
            env->ReleaseStringUTFChars(str, utf);
        }
        env->DeleteLocalRef(str);
    }

    text_extractor::Init(g_process_name);
    text_extractor::UpdateTargets(names);
}

extern "C" JNIEXPORT void JNICALL
Java_com_tools_module_NativeBridge_startDump(JNIEnv* env, jclass /*clazz*/, jstring dataDir) {
    const char* path = dataDir != nullptr ? env->GetStringUTFChars(dataDir, nullptr) : nullptr;
    std::string dir;
    if (path != nullptr) {
        dir.assign(path);
        env->ReleaseStringUTFChars(dataDir, path);
    }
    dump_plugin::StartDump(dir);
}

jint JNI_OnLoad(JavaVM* vm, void*) {
    LOGI("JNI_OnLoad");
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_VERSION_1_6;
    }
    jclass cls = env->FindClass("com/tools/module/NativeBridge");
    if (cls != nullptr) {
        jclass global_cls = static_cast<jclass>(env->NewGlobalRef(cls));
        env->DeleteLocalRef(cls);
        jmethodID on_dump_finished = env->GetStaticMethodID(
                global_cls,
                "onDumpFinished",
                "(ZLjava/lang/String;)V");
        dump_plugin::SetJavaCallbacks(vm, global_cls, on_dump_finished);
    }
    return JNI_VERSION_1_6;
}
