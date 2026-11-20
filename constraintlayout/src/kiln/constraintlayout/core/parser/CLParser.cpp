#include "kiln/constraintlayout/core/parser/CLParser.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace kiln::constraintlayout::core {

    namespace {

        // Java's Float.toString: the shortest digits that round-trip, as "12.5" between 1e-3
        // and 1e7, else as "1.25E10".
        std::string javaFloatToString(float value) {
            if (std::isnan(value)) return "NaN";
            if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
            if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";
            char buf[64];
            auto [end, ec] = std::to_chars(buf, buf + sizeof buf, value, std::chars_format::scientific);
            (void)ec;
            std::string sci(buf, end);   // d[.ddd]e±XX
            const std::size_t e = sci.find('e');
            std::string mantissa = sci.substr(0, e);
            const int exponent = std::atoi(sci.c_str() + e + 1);
            bool negative = false;
            if (!mantissa.empty() && mantissa[0] == '-') {
                negative = true;
                mantissa.erase(0, 1);
            }
            std::string digits;
            for (char c : mantissa)
                if (c != '.') digits += c;
            std::string out;
            const float magnitude = std::fabs(value);
            if (magnitude >= 1e-3f && magnitude < 1e7f) {
                if (exponent >= 0) {
                    std::string intPart = digits.substr(0, std::min<std::size_t>(digits.size(), std::size_t(exponent) + 1));
                    while (int(intPart.size()) < exponent + 1) intPart += '0';
                    std::string frac = digits.size() > std::size_t(exponent) + 1 ? digits.substr(std::size_t(exponent) + 1) : "0";
                    out = intPart + "." + frac;
                } else {
                    out = "0." + std::string(std::size_t(-exponent - 1), '0') + digits;
                }
            } else {
                out = digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exponent);
            }
            return negative ? "-" + out : out;
        }

        // Java would throw a NullPointerException past the root on malformed input.
        CLElement* containerOf(CLElement* element) {
            CLElement* container = element->getContainer();
            if (container == nullptr) throw CLParsingException("unbalanced json content", element);
            return container;
        }

        std::string javaIntToString(int v) { return std::to_string(v); }

        int parseJavaInt(const std::string& s, const CLElement* element) {
            int value = 0;
            const char* begin = s.data();
            const char* end = s.data() + s.size();
            if (begin != end && *begin == '+') ++begin;
            auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc() || ptr != end || begin == end) throw CLParsingException("For input string: \"" + s + "\"", element);
            return value;
        }

        float parseJavaFloat(const std::string& s, const CLElement* element) {
            std::string t = s;
            while (!t.empty() && (unsigned char)t.back() <= ' ') t.pop_back();
            std::size_t start = 0;
            while (start < t.size() && (unsigned char)t[start] <= ' ') start++;
            t = t.substr(start);
            // Java accepts a trailing type suffix.
            if (!t.empty() && (t.back() == 'f' || t.back() == 'F' || t.back() == 'd' || t.back() == 'D')) t.pop_back();
            if (t.empty()) throw CLParsingException("empty String", element);
            char* endPtr = nullptr;
            const float value = std::strtof(t.c_str(), &endPtr);
            if (endPtr != t.c_str() + t.size()) throw CLParsingException("For input string: \"" + s + "\"", element);
            return value;
        }

    }

    CLParsingException::CLParsingException(const std::string& reason, const CLElement* element)
        : std::runtime_error(reason), mReason(reason) {
        if (element != nullptr) {
            mElementClass = element->getStrClass();
            mLineNumber = element->getLine();
        } else {
            mElementClass = "unknown";
            mLineNumber = 0;
        }
    }

    // CLElement

    void CLElement::setEnd(int64_t end) {
        if (mEnd != kNotEnded) return;
        mEnd = end;
        if (mContainer != nullptr) mContainer->add(shared_from_this());
    }

    std::string CLElement::getStrClass() const {
        if (is(KIND_OBJECT)) return "CLObject";
        if (is(KIND_ARRAY)) return "CLArray";
        if (is(KIND_KEY)) return "CLKey";
        if (is(KIND_CONTAINER)) return "CLContainer";
        if (is(KIND_NUMBER)) return "CLNumber";
        if (is(KIND_STRING)) return "CLString";
        if (is(KIND_TOKEN)) return "CLToken";
        return "CLElement";
    }

    std::string CLElement::toString() const {
        if (mStart > mEnd || mEnd == kNotEnded) return getStrClass() + " (INVALID, " + std::to_string(mStart) + "-" + std::to_string(mEnd) + ")";
        return getStrClass() + " (" + std::to_string(mStart) + " : " + std::to_string(mEnd) + ") <<" + content() + ">>";
    }

    std::string CLElement::content() const {
        if (mContent == nullptr || mContent->empty() || mStart == -1) return "";
        if (mEnd == kNotEnded || mEnd < mStart) return mContent->substr(std::size_t(mStart), 1);
        int64_t length = mEnd - mStart + 1;
        if (length > 1000) length = 1000;
        return mContent->substr(std::size_t(mStart), std::size_t(length));
    }

    void CLElement::setContainer(CLContainer* element) {
        mContainer = element;
        if (element != nullptr) mDepth = element->mDepth + 1;
    }

    int CLElement::getInt() {
        if (is(KIND_NUMBER)) return static_cast<CLNumber*>(this)->getInt();
        return 0;
    }

    float CLElement::getFloat() {
        if (is(KIND_NUMBER)) return static_cast<CLNumber*>(this)->getFloat();
        return std::numeric_limits<float>::quiet_NaN();
    }

    bool CLElement::equals(const CLElement& o) const {
        if (this == &o) return true;
        if (mStart != o.mStart || mEnd != o.mEnd || mLine != o.mLine) return false;
        const std::string empty;
        if ((mContent ? *mContent : empty) != (o.mContent ? *o.mContent : empty)) return false;
        if (mContainer == o.mContainer) return true;
        if (mContainer == nullptr || o.mContainer == nullptr) return false;
        return mContainer->equals(*o.mContainer);
    }

    // CLContainer

    std::string CLContainer::toString() const {
        std::string list;
        for (const auto& element : mElements) {
            if (!list.empty()) list += "; ";
            list += element->toString();
        }
        return CLElement::toString() + " = <" + list + " >";
    }

    std::vector<std::string> CLContainer::names() const {
        std::vector<std::string> names;
        for (const auto& element : mElements)
            if (element->is(KIND_KEY)) names.push_back(element->content());
        return names;
    }

    bool CLContainer::has(const std::string& name) const {
        for (const auto& element : mElements)
            if (element->is(KIND_KEY) && element->content() == name) return true;
        return false;
    }

    void CLContainer::put(const std::string& name, CLElementPtr value) {
        for (const auto& element : mElements) {
            auto* key = static_cast<CLKey*>(element.get());
            if (key->content() == name) {
                key->set(std::move(value));
                return;
            }
        }
        mElements.push_back(CLKey::allocate(name, std::move(value)));
    }

    void CLContainer::putNumber(const std::string& name, float value) { put(name, std::make_shared<CLNumber>(value)); }

    void CLContainer::putString(const std::string& name, const std::string& value) {
        auto stringElement = std::make_shared<CLString>(std::make_shared<const std::string>(value));
        stringElement->setStart(0);
        stringElement->setEnd(int64_t(value.size()) - 1);
        put(name, stringElement);
    }

    void CLContainer::remove(const std::string& name) {
        std::erase_if(mElements, [&](const CLElementPtr& element) { return element->content() == name; });
    }

    CLElement* CLContainer::get(const std::string& name) const {
        for (const auto& element : mElements) {
            auto* key = static_cast<CLKey*>(element.get());
            if (key->content() == name) return key->getValue();
        }
        throw CLParsingException("no element for key <" + name + ">", this);
    }

    int CLContainer::getInt(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr) return element->getInt();
        throw CLParsingException("no int found for key <" + name + ">", this);
    }

    float CLContainer::getFloat(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr) return element->getFloat();
        throw CLParsingException("no float found for key <" + name + ">", this);
    }

    CLArray* CLContainer::getArray(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr && element->is(KIND_ARRAY)) return static_cast<CLArray*>(element);
        throw CLParsingException("no array found for key <" + name + ">, found [" + (element ? element->getStrClass() : "null") + "] : " +
                                     (element ? element->toString() : "null"),
                                 this);
    }

    CLObject* CLContainer::getObject(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr && element->is(KIND_OBJECT)) return static_cast<CLObject*>(element);
        throw CLParsingException("no object found for key <" + name + ">, found [" + (element ? element->getStrClass() : "null") + "] : " +
                                     (element ? element->toString() : "null"),
                                 this);
    }

    std::string CLContainer::getString(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr && element->is(KIND_STRING)) return element->content();
        throw CLParsingException("no string found for key <" + name + ">, found [" + (element ? element->getStrClass() : "null") + "] : " +
                                     (element ? element->toString() : "null"),
                                 this);
    }

    bool CLContainer::getBoolean(const std::string& name) const {
        CLElement* element = get(name);
        if (element != nullptr && element->is(KIND_TOKEN)) return static_cast<CLToken*>(element)->getBoolean();
        throw CLParsingException("no boolean found for key <" + name + ">, found [" + (element ? element->getStrClass() : "null") + "] : " +
                                     (element ? element->toString() : "null"),
                                 this);
    }

    CLElement* CLContainer::getOrNull(const std::string& name) const {
        for (const auto& element : mElements) {
            auto* key = static_cast<CLKey*>(element.get());
            if (key->content() == name) return key->getValue();
        }
        return nullptr;
    }

    CLObject* CLContainer::getObjectOrNull(const std::string& name) const {
        CLElement* element = getOrNull(name);
        return element != nullptr && element->is(KIND_OBJECT) ? static_cast<CLObject*>(element) : nullptr;
    }

    CLArray* CLContainer::getArrayOrNull(const std::string& name) const {
        CLElement* element = getOrNull(name);
        return element != nullptr && element->is(KIND_ARRAY) ? static_cast<CLArray*>(element) : nullptr;
    }

    CLArray* CLContainer::getArrayOrCreate(const std::string& name) {
        if (CLArray* array = getArrayOrNull(name)) return array;
        auto array = CLArray::empty();
        CLArray* raw = array.get();
        put(name, std::move(array));
        return raw;
    }

    std::optional<std::string> CLContainer::getStringOrNull(const std::string& name) const {
        CLElement* element = getOrNull(name);
        if (element != nullptr && element->is(KIND_STRING)) return element->content();
        return std::nullopt;
    }

    float CLContainer::getFloatOrNaN(const std::string& name) const {
        CLElement* element = getOrNull(name);
        if (element != nullptr && element->is(KIND_NUMBER)) return element->getFloat();
        return std::numeric_limits<float>::quiet_NaN();
    }

    CLElement* CLContainer::get(int index) const {
        if (index >= 0 && index < int(mElements.size())) return mElements[std::size_t(index)].get();
        throw CLParsingException("no element at index " + std::to_string(index), this);
    }

    int CLContainer::getInt(int index) const { return get(index)->getInt(); }
    float CLContainer::getFloat(int index) const { return get(index)->getFloat(); }

    CLArray* CLContainer::getArray(int index) const {
        CLElement* element = get(index);
        if (element->is(KIND_ARRAY)) return static_cast<CLArray*>(element);
        throw CLParsingException("no array at index " + std::to_string(index), this);
    }

    CLObject* CLContainer::getObject(int index) const {
        CLElement* element = get(index);
        if (element->is(KIND_OBJECT)) return static_cast<CLObject*>(element);
        throw CLParsingException("no object at index " + std::to_string(index), this);
    }

    std::string CLContainer::getString(int index) const {
        CLElement* element = get(index);
        if (element->is(KIND_STRING)) return element->content();
        throw CLParsingException("no string at index " + std::to_string(index), this);
    }

    bool CLContainer::getBoolean(int index) const {
        CLElement* element = get(index);
        if (element->is(KIND_TOKEN)) return static_cast<CLToken*>(element)->getBoolean();
        throw CLParsingException("no boolean at index " + std::to_string(index), this);
    }

    CLElement* CLContainer::getOrNull(int index) const {
        if (index >= 0 && index < int(mElements.size())) return mElements[std::size_t(index)].get();
        return nullptr;
    }

    std::optional<std::string> CLContainer::getStringOrNull(int index) const {
        CLElement* element = getOrNull(index);
        if (element != nullptr && element->is(KIND_STRING)) return element->content();
        return std::nullopt;
    }

    bool CLContainer::equals(const CLElement& o) const {
        if (this == &o) return true;
        if (!o.is(KIND_CONTAINER)) return false;
        const auto& other = static_cast<const CLContainer&>(o);
        if (mElements.size() != other.mElements.size()) return false;
        for (std::size_t i = 0; i < mElements.size(); i++)
            if (!mElements[i]->equals(*other.mElements[i])) return false;
        return true;
    }

    void CLContainer::cloneElementsInto(CLContainer& clone) const {
        clone.mElements.clear();
        clone.mElements.reserve(mElements.size());
        for (const auto& element : mElements) {
            CLElementPtr elementClone = element->clone();
            elementClone->setContainer(&clone);
            clone.mElements.push_back(std::move(elementClone));
        }
    }

    // CLObject

    std::string CLObject::toJSON() const {
        std::string json = getDebugName() + "{ ";
        bool first = true;
        for (const auto& element : mElements) {
            if (!first) json += ", ";
            else first = false;
            json += element->toJSON();
        }
        json += " }";
        return json;
    }

    std::string CLObject::toFormattedJSON(int indent, int forceIndent) const {
        std::string json = getDebugName();
        json += "{\n";
        bool first = true;
        for (const auto& element : mElements) {
            if (!first) json += ",\n";
            else first = false;
            json += element->toFormattedJSON(indent + sBaseIndent, forceIndent - 1);
        }
        json += "\n";
        addIndent(json, indent);
        json += "}";
        return json;
    }

    CLElementPtr CLObject::clone() const {
        auto clone = std::make_shared<CLObject>(*this);
        cloneElementsInto(*clone);
        return clone;
    }

    CLKey* CLObject::keyAt(int index) const { return static_cast<CLKey*>(mElements[std::size_t(index)].get()); }

    // CLArray

    std::string CLArray::toJSON() const {
        std::string content = getDebugName() + "[";
        bool first = true;
        for (const auto& element : mElements) {
            if (!first) content += ", ";
            else first = false;
            content += element->toJSON();
        }
        return content + "]";
    }

    std::string CLArray::toFormattedJSON(int indent, int forceIndent) const {
        std::string json;
        std::string val = toJSON();
        if (forceIndent <= 0 && int(val.size()) + indent < sMaxLine) {
            json += val;
        } else {
            json += "[\n";
            bool first = true;
            for (const auto& element : mElements) {
                if (!first) json += ",\n";
                else first = false;
                addIndent(json, indent + sBaseIndent);
                json += element->toFormattedJSON(indent + sBaseIndent, forceIndent - 1);
            }
            json += "\n";
            addIndent(json, indent);
            json += "]";
        }
        return json;
    }

    CLElementPtr CLArray::clone() const {
        auto clone = std::make_shared<CLArray>(*this);
        cloneElementsInto(*clone);
        return clone;
    }

    // CLKey

    namespace {
        bool isSection(const std::string& name) {
            static const char* const kSections[] = { "ConstraintSets", "Variables", "Generate", "Transitions",
                                                      "KeyFrames", "KeyAttributes", "KeyPositions", "KeyCycles" };
            for (const char* s : kSections)
                if (name == s) return true;
            return false;
        }
    }

    std::shared_ptr<CLKey> CLKey::allocate(const std::string& name, CLElementPtr value) {
        auto key = std::make_shared<CLKey>(std::make_shared<const std::string>(name));
        key->setStart(0);
        key->setEnd(int64_t(name.size()) - 1);
        key->set(std::move(value));
        return key;
    }

    std::string CLKey::toJSON() const {
        if (!mElements.empty()) return getDebugName() + content() + ": " + mElements[0]->toJSON();
        return getDebugName() + content() + ": <> ";
    }

    std::string CLKey::toFormattedJSON(int indent, int forceIndent) const {
        std::string json = getDebugName();
        addIndent(json, indent);
        std::string content = this->content();
        if (!mElements.empty()) {
            json += content;
            json += ": ";
            if (isSection(content)) forceIndent = 3;
            if (forceIndent > 0) {
                json += mElements[0]->toFormattedJSON(indent, forceIndent - 1);
            } else {
                std::string val = mElements[0]->toJSON();
                if (int(val.size()) + indent < sMaxLine) json += val;
                else json += mElements[0]->toFormattedJSON(indent, forceIndent - 1);
            }
            return json;
        }
        return content + ": <> ";
    }

    void CLKey::set(CLElementPtr value) {
        if (!mElements.empty()) mElements[0] = std::move(value);
        else mElements.push_back(std::move(value));
    }

    bool CLKey::equals(const CLElement& o) const {
        if (this == &o) return true;
        if (o.is(KIND_KEY) && getName() != static_cast<const CLKey&>(o).getName()) return false;
        return CLContainer::equals(o);
    }

    CLElementPtr CLKey::clone() const {
        auto clone = std::make_shared<CLKey>(*this);
        cloneElementsInto(*clone);
        return clone;
    }

    // CLNumber

    std::string CLNumber::toJSON() const {
        float value = const_cast<CLNumber*>(this)->getFloat();
        int intValue = int(value);
        if (!std::isnan(value) && float(intValue) == value) return javaIntToString(intValue);
        return javaFloatToString(value);
    }

    std::string CLNumber::toFormattedJSON(int indent, int /*forceIndent*/) const {
        std::string json;
        addIndent(json, indent);
        json += toJSON();
        return json;
    }

    bool CLNumber::isInt() {
        float value = getFloat();
        int intValue = int(value);
        return float(intValue) == value;
    }

    int CLNumber::getInt() {
        if (std::isnan(mValue) && hasContent()) mValue = float(parseJavaInt(content(), this));
        if (std::isnan(mValue)) return 0;
        return int(mValue);
    }

    float CLNumber::getFloat() {
        if (std::isnan(mValue) && hasContent()) mValue = parseJavaFloat(content(), this);
        return mValue;
    }

    bool CLNumber::equals(const CLElement& o) const {
        if (this == &o) return true;
        if (!o.is(KIND_NUMBER)) return false;
        float thisFloat = const_cast<CLNumber*>(this)->getFloat();
        float otherFloat = const_cast<CLNumber&>(static_cast<const CLNumber&>(o)).getFloat();
        if (std::isnan(thisFloat) && std::isnan(otherFloat)) return true;
        return thisFloat == otherFloat;
    }

    // CLString

    std::shared_ptr<CLString> CLString::from(const std::string& content) {
        auto stringElement = std::make_shared<CLString>(std::make_shared<const std::string>(content));
        stringElement->setStart(0);
        stringElement->setEnd(int64_t(content.size()) - 1);
        return stringElement;
    }

    std::string CLString::toFormattedJSON(int indent, int /*forceIndent*/) const {
        std::string json;
        addIndent(json, indent);
        json += "'" + content() + "'";
        return json;
    }

    bool CLString::equals(const CLElement& o) const {
        if (this == &o) return true;
        if (o.is(KIND_STRING) && content() == o.content()) return true;
        return CLElement::equals(o);
    }

    // CLToken

    bool CLToken::getBoolean() const {
        if (mType == Type::TRUE) return true;
        if (mType == Type::FALSE) return false;
        throw CLParsingException("this token is not a boolean: <" + content() + ">", this);
    }

    bool CLToken::isNull() const {
        if (mType == Type::NULL_) return true;
        throw CLParsingException("this token is not a null: <" + content() + ">", this);
    }

    std::string CLToken::toFormattedJSON(int indent, int /*forceIndent*/) const {
        std::string json;
        addIndent(json, indent);
        json += content();
        return json;
    }

    bool CLToken::validate(char c, int64_t position) {
        static constexpr std::string_view kTrue = "true", kFalse = "false", kNull = "null";
        auto at = [&](std::string_view token) { return std::size_t(mIndex) < token.size() ? token[std::size_t(mIndex)] : '\0'; };
        bool isValid = false;
        switch (mType) {
            case Type::TRUE:
                isValid = at(kTrue) == c;
                if (isValid && std::size_t(mIndex) + 1 == kTrue.size()) setEnd(position);
                break;
            case Type::FALSE:
                isValid = at(kFalse) == c;
                if (isValid && std::size_t(mIndex) + 1 == kFalse.size()) setEnd(position);
                break;
            case Type::NULL_:
                isValid = at(kNull) == c;
                if (isValid && std::size_t(mIndex) + 1 == kNull.size()) setEnd(position);
                break;
            case Type::UNKNOWN:
                if (at(kTrue) == c) {
                    mType = Type::TRUE;
                    isValid = true;
                } else if (at(kFalse) == c) {
                    mType = Type::FALSE;
                    isValid = true;
                } else if (at(kNull) == c) {
                    mType = Type::NULL_;
                    isValid = true;
                }
                break;
        }
        mIndex++;
        return isValid;
    }

    // CLParser

    std::shared_ptr<CLObject> CLParser::parse() {
        auto content = std::make_shared<const std::string>(mContent);
        CLElement* currentElement = nullptr;
        const int length = int(content->size());

        mLineNumber = 1;

        int startIndex = -1;
        for (int i = 0; i < length; i++) {
            char c = (*content)[std::size_t(i)];
            if (c == '{') {
                startIndex = i;
                break;
            }
            if (c == '\n') mLineNumber++;
        }
        if (startIndex == -1) throw CLParsingException("invalid json content", nullptr);

        std::shared_ptr<CLObject> root = CLObject::allocate(content);
        root->setLine(mLineNumber);
        root->setStart(startIndex);
        currentElement = root.get();

        for (int i = startIndex + 1; i < length; i++) {
            char c = (*content)[std::size_t(i)];
            if (c == '\n') mLineNumber++;
            if (mHasComment) {
                if (c == '\n') mHasComment = false;
                else continue;
            }

            if (currentElement == nullptr) break;

            if (currentElement->isDone()) {
                currentElement = getNextJsonElement(i, c, currentElement, content);
            } else if (currentElement->is(CLElement::KIND_OBJECT)) {
                if (c == '}') currentElement->setEnd(i - 1);
                else currentElement = getNextJsonElement(i, c, currentElement, content);
            } else if (currentElement->is(CLElement::KIND_ARRAY)) {
                if (c == ']') currentElement->setEnd(i - 1);
                else currentElement = getNextJsonElement(i, c, currentElement, content);
            } else if (currentElement->is(CLElement::KIND_STRING)) {
                char ck = (*content)[std::size_t(currentElement->mStart)];
                if (ck == c) {
                    currentElement->setStart(currentElement->mStart + 1);
                    currentElement->setEnd(i - 1);
                }
            } else {
                if (currentElement->is(CLElement::KIND_TOKEN)) {
                    auto* token = static_cast<CLToken*>(currentElement);
                    if (!token->validate(c, i))
                        throw CLParsingException("parsing incorrect token " + token->content() + " at line " + std::to_string(mLineNumber), token);
                }
                if (currentElement->is(CLElement::KIND_KEY) || currentElement->is(CLElement::KIND_STRING)) {
                    char ck = (*content)[std::size_t(currentElement->mStart)];
                    if ((ck == '\'' || ck == '"') && ck == c) {
                        currentElement->setStart(currentElement->mStart + 1);
                        currentElement->setEnd(i - 1);
                    }
                }
                if (!currentElement->isDone()) {
                    if (c == '}' || c == ']' || c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ':') {
                        currentElement->setEnd(i - 1);
                        if (c == '}' || c == ']') {
                            currentElement = containerOf(currentElement);
                            currentElement->setEnd(i - 1);
                            if (currentElement->is(CLElement::KIND_KEY)) {
                                currentElement = containerOf(currentElement);
                                currentElement->setEnd(i - 1);
                            }
                        }
                    }
                }
            }

            if (currentElement->isDone() &&
                (!currentElement->is(CLElement::KIND_KEY) || !static_cast<CLKey*>(currentElement)->mElements.empty())) {
                currentElement = currentElement->getContainer();
            }
        }

        while (currentElement != nullptr && !currentElement->isDone()) {
            if (currentElement->is(CLElement::KIND_STRING)) currentElement->setStart(currentElement->mStart + 1);
            currentElement->setEnd(length - 1);
            currentElement = currentElement->getContainer();
        }

        mAlive.clear();
        return root;
    }

    CLElement* CLParser::getNextJsonElement(int position, char c, CLElement* currentElement, const CLContent& content) {
        switch (c) {
            case ' ':
            case ':':
            case ',':
            case '\t':
            case '\r':
            case '\n':
                break;
            case '{':
                currentElement = createElement(currentElement, position, TYPE::OBJECT, true, content);
                break;
            case '[':
                currentElement = createElement(currentElement, position, TYPE::ARRAY, true, content);
                break;
            case ']':
            case '}':
                currentElement->setEnd(position - 1);
                currentElement = containerOf(currentElement);
                currentElement->setEnd(position);
                break;
            case '"':
            case '\'':
                if (currentElement->is(CLElement::KIND_OBJECT)) currentElement = createElement(currentElement, position, TYPE::KEY, true, content);
                else currentElement = createElement(currentElement, position, TYPE::STRING, true, content);
                break;
            case '/':
                if (position + 1 < int(content->size()) && (*content)[std::size_t(position) + 1] == '/') mHasComment = true;
                break;
            case '-':
            case '+':
            case '.':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                currentElement = createElement(currentElement, position, TYPE::NUMBER, true, content);
                break;
            default:
                if (currentElement->is(CLElement::KIND_CONTAINER) && !currentElement->is(CLElement::KIND_OBJECT)) {
                    currentElement = createElement(currentElement, position, TYPE::TOKEN, true, content);
                    auto* token = static_cast<CLToken*>(currentElement);
                    if (!token->validate(c, position))
                        throw CLParsingException("incorrect token <" + std::string(1, c) + "> at line " + std::to_string(mLineNumber), token);
                } else {
                    currentElement = createElement(currentElement, position, TYPE::KEY, true, content);
                }
        }
        if (currentElement != nullptr && currentElement->mDepth >= 256) throw CLParsingException("Max depth exceeded", currentElement);
        return currentElement;
    }

    CLElement* CLParser::createElement(CLElement* currentElement, int position, TYPE type, bool applyStart, const CLContent& content) {
        CLElementPtr newElement;
        switch (type) {
            case TYPE::OBJECT:
                newElement = CLObject::allocate(content);
                position++;
                break;
            case TYPE::ARRAY:
                newElement = CLArray::allocate(content);
                position++;
                break;
            case TYPE::STRING:
                newElement = CLString::allocate(content);
                break;
            case TYPE::NUMBER:
                newElement = CLNumber::allocate(content);
                break;
            case TYPE::KEY:
                newElement = CLKey::allocate(content);
                break;
            case TYPE::TOKEN:
                newElement = CLToken::allocate(content);
                break;
            default:
                break;
        }
        if (newElement == nullptr) return nullptr;
        newElement->setLine(mLineNumber);
        if (applyStart) newElement->setStart(position);
        if (currentElement->is(CLElement::KIND_CONTAINER)) newElement->setContainer(static_cast<CLContainer*>(currentElement));
        mAlive.push_back(newElement);
        return newElement.get();
    }

}
