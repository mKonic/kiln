#include "AndroidResources.h"

#include "kiln/ui/AppResources.h"

#include <cmath>
#include <string>

namespace kiln::android {

    namespace {
        // UTF-8 <-> jstring through UTF-16 (JNI's "UTF" functions use modified UTF-8, which
        // breaks characters outside the BMP).
        jstring ToJava(JNIEnv* env, std::string_view s) {
            std::u16string u;
            u.reserve(s.size());
            for (std::size_t i = 0; i < s.size();) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                char32_t cp;
                int n;
                if (c < 0x80) { cp = c; n = 1; }
                else if ((c >> 5) == 0x6) { cp = c & 0x1f; n = 2; }
                else if ((c >> 4) == 0xe) { cp = c & 0x0f; n = 3; }
                else { cp = c & 0x07; n = 4; }
                for (int k = 1; k < n && i + std::size_t(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3f);
                i += std::size_t(n);
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    u += char16_t(0xD800 + (cp >> 10));
                    u += char16_t(0xDC00 + (cp & 0x3ff));
                } else {
                    u += char16_t(cp);
                }
            }
            return env->NewString(reinterpret_cast<const jchar*>(u.data()), jsize(u.size()));
        }
        std::string FromJava(JNIEnv* env, jstring s) {
            if (!s) return {};
            const jsize len = env->GetStringLength(s);
            std::u16string u(std::size_t(len), u'\0');
            env->GetStringRegion(s, 0, len, reinterpret_cast<jchar*>(u.data()));
            std::string out;
            for (std::size_t i = 0; i < u.size(); ++i) {
                char32_t cp = u[i];
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < u.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (u[++i] - 0xDC00);
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            }
            return out;
        }

        class AndroidAppResources final : public ui::AppResources {
        public:
            explicit AndroidAppResources(JNIEnv* env) {
                env->GetJavaVM(&m_Vm);
                jclass c = env->FindClass("dev/kiln/KilnResources");
                m_Class = static_cast<jclass>(env->NewGlobalRef(c));
                env->DeleteLocalRef(c);
                m_String = env->GetStaticMethodID(m_Class, "string", "(Ljava/lang/String;)Ljava/lang/String;");
                m_Quantity = env->GetStaticMethodID(m_Class, "quantity", "(Ljava/lang/String;I)Ljava/lang/String;");
                m_StringArray = env->GetStaticMethodID(m_Class, "stringArray", "(Ljava/lang/String;)[Ljava/lang/String;");
                m_IntArray = env->GetStaticMethodID(m_Class, "intArray", "(Ljava/lang/String;)[I");
                m_Color = env->GetStaticMethodID(m_Class, "color", "(Ljava/lang/String;)J");
                m_Dimension = env->GetStaticMethodID(m_Class, "dimension", "(Ljava/lang/String;)F");
                m_Integer = env->GetStaticMethodID(m_Class, "integer", "(Ljava/lang/String;)[I");
                m_Bool = env->GetStaticMethodID(m_Class, "bool", "(Ljava/lang/String;)[I");
                m_Format = env->GetStaticMethodID(m_Class, "format", "(Ljava/lang/String;[Ljava/lang/Object;Ljava/lang/String;)Ljava/lang/String;");
                m_Bitmap = env->GetStaticMethodID(m_Class, "bitmap", "(Ljava/lang/String;)[B");
                m_Vector = env->GetStaticMethodID(m_Class, "vector", "(Ljava/lang/String;)[Ljava/lang/String;");
                jclass lc = env->FindClass("java/lang/Long");
                m_LongClass = static_cast<jclass>(env->NewGlobalRef(lc));
                m_LongValueOf = env->GetStaticMethodID(lc, "valueOf", "(J)Ljava/lang/Long;");
                env->DeleteLocalRef(lc);
                jclass dc = env->FindClass("java/lang/Double");
                m_DoubleClass = static_cast<jclass>(env->NewGlobalRef(dc));
                m_DoubleValueOf = env->GetStaticMethodID(dc, "valueOf", "(D)Ljava/lang/Double;");
                env->DeleteLocalRef(dc);
                jclass oc = env->FindClass("java/lang/Object");
                m_ObjectClass = static_cast<jclass>(env->NewGlobalRef(oc));
                env->DeleteLocalRef(oc);
            }

            std::optional<std::string> GetString(std::string_view name, const ui::Configuration&) const override {
                return CallString(m_String, name);
            }
            std::optional<std::string> GetQuantityString(std::string_view name, int quantity, const ui::Configuration&) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                auto* r = static_cast<jstring>(env->CallStaticObjectMethod(m_Class, m_Quantity, n, jint(quantity)));
                Clear(env);
                env->DeleteLocalRef(n);
                return Take(env, r);
            }
            std::optional<std::vector<std::string>> GetStringArray(std::string_view name, const ui::Configuration&) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_StringArray, n));
                Clear(env);
                env->DeleteLocalRef(n);
                if (!arr) return std::nullopt;
                std::vector<std::string> out;
                const jsize len = env->GetArrayLength(arr);
                for (jsize i = 0; i < len; ++i) {
                    auto* s = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
                    out.push_back(FromJava(env, s));
                    if (s) env->DeleteLocalRef(s);
                }
                env->DeleteLocalRef(arr);
                return out;
            }
            std::optional<std::vector<int>> GetIntArray(std::string_view name, const ui::Configuration&) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                auto* arr = static_cast<jintArray>(env->CallStaticObjectMethod(m_Class, m_IntArray, n));
                Clear(env);
                env->DeleteLocalRef(n);
                if (!arr) return std::nullopt;
                std::vector<int> out(std::size_t(env->GetArrayLength(arr)));
                env->GetIntArrayRegion(arr, 0, jsize(out.size()), reinterpret_cast<jint*>(out.data()));
                env->DeleteLocalRef(arr);
                return out;
            }
            std::optional<ui::Color> GetColor(std::string_view name, const ui::Configuration&) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                const jlong v = env->CallStaticLongMethod(m_Class, m_Color, n);
                Clear(env);
                env->DeleteLocalRef(n);
                if (!(v & (jlong(1) << 32))) return std::nullopt;
                return ui::Color(std::uint32_t(v & 0xffffffff));
            }
            std::optional<float> GetDimension(std::string_view name, const ui::Configuration&, float, float) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                const float v = env->CallStaticFloatMethod(m_Class, m_Dimension, n);
                Clear(env);
                env->DeleteLocalRef(n);
                if (std::isnan(v)) return std::nullopt;
                return v;
            }
            std::optional<int> GetInteger(std::string_view name, const ui::Configuration&) const override { return CallFlagged(m_Integer, name); }
            std::optional<bool> GetBoolean(std::string_view name, const ui::Configuration&) const override {
                auto v = CallFlagged(m_Bool, name);
                if (!v) return std::nullopt;
                return *v != 0;
            }
            std::optional<ui::DrawableResource> GetDrawable(std::string_view name, const ui::Configuration&) const override {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                ui::DrawableResource out;
                auto* bytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(m_Class, m_Bitmap, n));
                Clear(env);
                if (bytes) {
                    out.bitmap.resize(std::size_t(env->GetArrayLength(bytes)));
                    env->GetByteArrayRegion(bytes, 0, jsize(out.bitmap.size()), reinterpret_cast<jbyte*>(out.bitmap.data()));
                    env->DeleteLocalRef(bytes);
                    env->DeleteLocalRef(n);
                    return out;
                }
                auto* arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(m_Class, m_Vector, n));
                Clear(env);
                env->DeleteLocalRef(n);
                if (!arr) return std::nullopt;
                const jsize len = env->GetArrayLength(arr);
                for (jsize i = 0; i < len; ++i) {
                    auto* s = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
                    std::string v = FromJava(env, s);
                    if (s) env->DeleteLocalRef(s);
                    if (!v.empty() && v[0] == '\x01') {
                        out.vector.push_back({ v.substr(1), false, {} });
                    } else if (!v.empty() && v[0] == '\x02') {
                        out.vector.push_back({ v.substr(1), true, {} });
                    } else if (!out.vector.empty() && i + 1 < len) {
                        auto* vs = static_cast<jstring>(env->GetObjectArrayElement(arr, ++i));
                        out.vector.back().attrs.emplace_back(std::move(v), FromJava(env, vs));
                        if (vs) env->DeleteLocalRef(vs);
                    }
                }
                env->DeleteLocalRef(arr);
                if (out.vector.empty()) return std::nullopt;
                return out;
            }
            std::string Format(std::string_view pattern, std::span<const text::FormatArg> args, const ui::Configuration& config) const override {
                JNIEnv* env = Env();
                if (!env) return AppResources::Format(pattern, args, config);
                jobjectArray arr = env->NewObjectArray(jsize(args.size()), m_ObjectClass, nullptr);
                for (std::size_t i = 0; i < args.size(); ++i) {
                    jobject o = nullptr;
                    if (const auto* s = std::get_if<std::string>(&args[i].value)) o = ToJava(env, *s);
                    else if (const auto* n = std::get_if<std::int64_t>(&args[i].value)) o = env->CallStaticObjectMethod(m_LongClass, m_LongValueOf, jlong(*n));
                    else o = env->CallStaticObjectMethod(m_DoubleClass, m_DoubleValueOf, jdouble(std::get<double>(args[i].value)));
                    env->SetObjectArrayElement(arr, jsize(i), o);
                    if (o) env->DeleteLocalRef(o);
                }
                jstring p = ToJava(env, pattern);
                jstring tag = ToJava(env, config.locales.empty() ? std::string_view() : std::string_view(config.locales.front()));
                auto* r = static_cast<jstring>(env->CallStaticObjectMethod(m_Class, m_Format, p, arr, tag));
                Clear(env);
                env->DeleteLocalRef(p); env->DeleteLocalRef(tag); env->DeleteLocalRef(arr);
                return Take(env, r).value_or(std::string(pattern));
            }

        private:
            JNIEnv* Env() const {
                JNIEnv* env = nullptr;
                m_Vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
                return env;
            }
            static void Clear(JNIEnv* env) { if (env->ExceptionCheck()) env->ExceptionClear(); }
            static std::optional<std::string> Take(JNIEnv* env, jstring s) {
                if (!s) return std::nullopt;
                std::string out = FromJava(env, s);
                env->DeleteLocalRef(s);
                return out;
            }
            std::optional<std::string> CallString(jmethodID m, std::string_view name) const {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                auto* r = static_cast<jstring>(env->CallStaticObjectMethod(m_Class, m, n));
                Clear(env);
                env->DeleteLocalRef(n);
                return Take(env, r);
            }
            std::optional<int> CallFlagged(jmethodID m, std::string_view name) const {
                JNIEnv* env = Env();
                if (!env) return std::nullopt;
                jstring n = ToJava(env, name);
                auto* r = static_cast<jintArray>(env->CallStaticObjectMethod(m_Class, m, n));
                Clear(env);
                env->DeleteLocalRef(n);
                if (!r) return std::nullopt;
                jint v[2]{};
                env->GetIntArrayRegion(r, 0, 2, v);
                env->DeleteLocalRef(r);
                if (!v[0]) return std::nullopt;
                return int(v[1]);
            }

            JavaVM* m_Vm = nullptr;
            jclass m_Class = nullptr, m_LongClass = nullptr, m_DoubleClass = nullptr, m_ObjectClass = nullptr;
            jmethodID m_String{}, m_Quantity{}, m_StringArray{}, m_IntArray{}, m_Color{}, m_Dimension{}, m_Integer{}, m_Bool{}, m_Format{},
                m_Bitmap{}, m_Vector{}, m_LongValueOf{}, m_DoubleValueOf{};
        };
    }

    void InstallAppResources(JNIEnv* env) {
        ui::SetAppResources(std::make_shared<AndroidAppResources>(env));
    }

}
