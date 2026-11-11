#include "AndroidDates.h"

#include "kiln/text/DateFormat.h"

#include <string>

namespace kiln::android {

    namespace {
        class AndroidDates final : public text::DateFormatBackend {
        public:
            explicit AndroidDates(JNIEnv* env) {
                env->GetJavaVM(&m_Vm);
                jclass c = env->FindClass("dev/kiln/KilnDates");
                m_Class = static_cast<jclass>(env->NewGlobalRef(c));
                env->DeleteLocalRef(c);
                m_FormatSkeleton = env->GetStaticMethodID(m_Class, "formatSkeleton", "(JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
                m_FormatPattern = env->GetStaticMethodID(m_Class, "formatPattern", "(JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
                m_Parse = env->GetStaticMethodID(m_Class, "parse", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)[I");
                m_ShortPattern = env->GetStaticMethodID(m_Class, "shortDatePattern", "(Ljava/lang/String;)Ljava/lang/String;");
                m_FirstDay = env->GetStaticMethodID(m_Class, "firstDayOfWeek", "(Ljava/lang/String;)I");
                m_Weekdays = env->GetStaticMethodID(m_Class, "weekdayNames", "(Ljava/lang/String;)[Ljava/lang/String;");
                m_Today = env->GetStaticMethodID(m_Class, "today", "()[I");
            }

            std::string FormatSkeleton(std::int64_t ms, std::string_view skeleton, std::string_view locale) override {
                return CallString(m_FormatSkeleton, ms, skeleton, locale);
            }
            std::string FormatPattern(std::int64_t ms, std::string_view pattern, std::string_view locale) override {
                return CallString(m_FormatPattern, ms, pattern, locale);
            }
            std::optional<text::CivilDate> Parse(std::string_view date, std::string_view pattern, std::string_view locale) override {
                JNIEnv* env = Env();
                if (!env) return text::BuiltInDateFormat().Parse(date, pattern, locale);
                jstring a = Str(env, date), b = Str(env, pattern), c = Str(env, locale);
                auto* r = static_cast<jintArray>(env->CallStaticObjectMethod(m_Class, m_Parse, a, b, c));
                Clear(env);
                env->DeleteLocalRef(a); env->DeleteLocalRef(b); env->DeleteLocalRef(c);
                if (!r) return std::nullopt;
                jint v[3]{};
                env->GetIntArrayRegion(r, 0, 3, v);
                env->DeleteLocalRef(r);
                return text::CivilDate{ v[0], v[1], v[2] };
            }
            std::string ShortDatePattern(std::string_view locale) override {
                JNIEnv* env = Env();
                if (!env) return text::BuiltInDateFormat().ShortDatePattern(locale);
                jstring l = Str(env, locale);
                auto* r = static_cast<jstring>(env->CallStaticObjectMethod(m_Class, m_ShortPattern, l));
                Clear(env);
                env->DeleteLocalRef(l);
                return Take(env, r);
            }
            int FirstDayOfWeek(std::string_view locale) override {
                JNIEnv* env = Env();
                if (!env) return text::BuiltInDateFormat().FirstDayOfWeek(locale);
                jstring l = Str(env, locale);
                const int r = env->CallStaticIntMethod(m_Class, m_FirstDay, l);
                Clear(env);
                env->DeleteLocalRef(l);
                return r;
            }
            std::vector<std::pair<std::string, std::string>> WeekdayNames(std::string_view locale) override {
                JNIEnv* env = Env();
                if (!env) return text::BuiltInDateFormat().WeekdayNames(locale);
                jstring l = Str(env, locale);
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Weekdays, l));
                Clear(env);
                env->DeleteLocalRef(l);
                std::vector<std::pair<std::string, std::string>> out;
                if (!arr) return text::BuiltInDateFormat().WeekdayNames(locale);
                for (int i = 0; i + 1 < env->GetArrayLength(arr); i += 2) {
                    std::string full = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, i)));
                    std::string narrow = Take(env, static_cast<jstring>(env->GetObjectArrayElement(arr, i + 1)));
                    out.emplace_back(std::move(full), std::move(narrow));
                }
                env->DeleteLocalRef(arr);
                return out;
            }
            text::CivilDate Today() override {
                JNIEnv* env = Env();
                if (!env) return text::BuiltInDateFormat().Today();
                auto* r = static_cast<jintArray>(env->CallStaticObjectMethod(m_Class, m_Today));
                Clear(env);
                if (!r) return text::BuiltInDateFormat().Today();
                jint v[3]{};
                env->GetIntArrayRegion(r, 0, 3, v);
                env->DeleteLocalRef(r);
                return { v[0], v[1], v[2] };
            }

        private:
            JNIEnv* Env() const {
                JNIEnv* env = nullptr;
                m_Vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
                return env;
            }
            static void Clear(JNIEnv* env) { if (env->ExceptionCheck()) env->ExceptionClear(); }
            static jstring Str(JNIEnv* env, std::string_view s) { return env->NewStringUTF(std::string(s).c_str()); }
            static std::string Take(JNIEnv* env, jstring s) {
                if (!s) return {};
                const char* c = env->GetStringUTFChars(s, nullptr);
                std::string out = c ? c : "";
                if (c) env->ReleaseStringUTFChars(s, c);
                env->DeleteLocalRef(s);
                return out;
            }
            std::string CallString(jmethodID m, std::int64_t ms, std::string_view a, std::string_view locale) {
                JNIEnv* env = Env();
                if (!env) return {};
                jstring s = Str(env, a), l = Str(env, locale);
                auto* r = static_cast<jstring>(env->CallStaticObjectMethod(m_Class, m, jlong(ms), s, l));
                Clear(env);
                env->DeleteLocalRef(s); env->DeleteLocalRef(l);
                return Take(env, r);
            }

            JavaVM* m_Vm = nullptr;
            jclass m_Class = nullptr;
            jmethodID m_FormatSkeleton{}, m_FormatPattern{}, m_Parse{}, m_ShortPattern{}, m_FirstDay{}, m_Weekdays{}, m_Today{};
        };
    }

    void InstallDateFormat(JNIEnv* env) {
        static AndroidDates* dates = new AndroidDates(env);   // lives for the process
        text::SetDateFormat(dates);
    }

}
