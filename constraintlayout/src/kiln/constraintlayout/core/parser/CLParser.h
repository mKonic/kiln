#pragma once

// constraintlayout-core's JSON5 parser (androidx.constraintlayout.core.parser): CLElement and
// its containers, CLParser and CLParsingException. ConstraintSetParser reads ConstraintSets
// from these, and the Compose DSL writes its constraints into them.
//
// Java's char[] content becomes a shared byte string; offsets count bytes. Containers own their
// children; an element's container is a plain back pointer. instanceof checks are kind tests.

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core {

    class CLContainer;
    class CLElement;
    using CLElementPtr = std::shared_ptr<CLElement>;
    using CLContent = std::shared_ptr<const std::string>;

    class CLParsingException : public std::runtime_error {
    public:
        CLParsingException(const std::string& reason, const CLElement* element);
        std::string reason() const { return mReason + " (" + mElementClass + " at line " + std::to_string(mLineNumber) + ")"; }

    private:
        std::string mReason;
        int mLineNumber = 0;
        std::string mElementClass;
    };

    class CLElement : public std::enable_shared_from_this<CLElement> {
    public:
        enum Kind : unsigned {
            KIND_CONTAINER = 1u << 0,
            KIND_OBJECT = 1u << 1,
            KIND_ARRAY = 1u << 2,
            KIND_KEY = 1u << 3,
            KIND_NUMBER = 1u << 4,
            KIND_STRING = 1u << 5,
            KIND_TOKEN = 1u << 6,
        };
        static constexpr int64_t kNotEnded = std::numeric_limits<int64_t>::max();

        explicit CLElement(CLContent content) : mContent(std::move(content)) {}
        virtual ~CLElement() = default;
        bool is(unsigned kind) const { return (mKind & kind) != 0; }

        bool notStarted() const { return mStart == -1; }
        void setLine(int line) { mLine = line; }
        int getLine() const { return mLine; }
        void setStart(int64_t start) { mStart = start; }
        int64_t getStart() const { return mStart; }
        int64_t getEnd() const { return mEnd; }
        // Ends the element and adds it to its container.
        void setEnd(int64_t end);
        virtual std::string toString() const;
        std::string content() const;
        bool hasContent() const { return mContent != nullptr && !mContent->empty(); }
        bool isDone() const { return mEnd != kNotEnded; }
        void setContainer(CLContainer* element);
        CLContainer* getContainer() const { return mContainer; }
        bool isStarted() const { return mStart > -1; }
        virtual std::string toJSON() const { return ""; }
        virtual std::string toFormattedJSON(int /*indent*/, int /*forceIndent*/) const { return ""; }
        virtual int getInt();
        virtual float getFloat();
        virtual bool equals(const CLElement& o) const;
        virtual CLElementPtr clone() const = 0;
        std::string getStrClass() const;

        int64_t mStart = -1;
        int64_t mEnd = kNotEnded;
        int mDepth = 0;

    protected:
        static void addIndent(std::string& builder, int indent) { builder.append(std::size_t(std::max(indent, 0)), ' '); }
        std::string getDebugName() const { return ""; }
        static constexpr int sMaxLine = 80;
        static constexpr int sBaseIndent = 2;

        unsigned mKind = 0;
        CLContent mContent;
        CLContainer* mContainer = nullptr;
        int mLine = 0;
    };

    class CLArray;
    class CLObject;
    class CLKey;

    class CLContainer : public CLElement {
    public:
        explicit CLContainer(CLContent content) : CLElement(std::move(content)) { mKind |= KIND_CONTAINER; }
        using CLElement::getFloat;
        using CLElement::getInt;

        void add(CLElementPtr element) { mElements.push_back(std::move(element)); }
        std::string toString() const override;
        int size() const { return int(mElements.size()); }
        std::vector<std::string> names() const;
        bool has(const std::string& name) const;
        void put(const std::string& name, CLElementPtr value);
        void putNumber(const std::string& name, float value);
        void putString(const std::string& name, const std::string& value);
        void remove(const std::string& name);
        void clear() { mElements.clear(); }
        CLElement* get(const std::string& name) const;
        int getInt(const std::string& name) const;
        float getFloat(const std::string& name) const;
        CLArray* getArray(const std::string& name) const;
        CLObject* getObject(const std::string& name) const;
        std::string getString(const std::string& name) const;
        bool getBoolean(const std::string& name) const;
        CLElement* getOrNull(const std::string& name) const;
        CLObject* getObjectOrNull(const std::string& name) const;
        CLArray* getArrayOrNull(const std::string& name) const;
        CLArray* getArrayOrCreate(const std::string& name);
        std::optional<std::string> getStringOrNull(const std::string& name) const;
        float getFloatOrNaN(const std::string& name) const;
        CLElement* get(int index) const;
        int getInt(int index) const;
        float getFloat(int index) const;
        CLArray* getArray(int index) const;
        CLObject* getObject(int index) const;
        std::string getString(int index) const;
        bool getBoolean(int index) const;
        CLElement* getOrNull(int index) const;
        std::optional<std::string> getStringOrNull(int index) const;
        bool equals(const CLElement& o) const override;

        std::vector<CLElementPtr> mElements;

    protected:
        void cloneElementsInto(CLContainer& clone) const;
    };

    class CLObject : public CLContainer {
    public:
        explicit CLObject(CLContent content) : CLContainer(std::move(content)) { mKind |= KIND_OBJECT; }
        static std::shared_ptr<CLObject> allocate(CLContent content) { return std::make_shared<CLObject>(std::move(content)); }
        static std::shared_ptr<CLObject> empty() { return allocate(std::make_shared<const std::string>()); }
        std::string toJSON() const override;
        std::string toFormattedJSON() const { return toFormattedJSON(0, 0); }
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        CLElementPtr clone() const override;
        std::shared_ptr<CLObject> cloneObject() const { return std::static_pointer_cast<CLObject>(clone()); }
        // Iterating a CLObject yields its keys.
        CLKey* keyAt(int index) const;
    };

    class CLArray : public CLContainer {
    public:
        explicit CLArray(CLContent content) : CLContainer(std::move(content)) { mKind |= KIND_ARRAY; }
        static std::shared_ptr<CLArray> allocate(CLContent content) { return std::make_shared<CLArray>(std::move(content)); }
        static std::shared_ptr<CLArray> empty() { return allocate(std::make_shared<const std::string>()); }
        std::string toJSON() const override;
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        CLElementPtr clone() const override;
    };

    class CLKey : public CLContainer {
    public:
        explicit CLKey(CLContent content) : CLContainer(std::move(content)) { mKind |= KIND_KEY; }
        static std::shared_ptr<CLKey> allocate(CLContent content) { return std::make_shared<CLKey>(std::move(content)); }
        static std::shared_ptr<CLKey> allocate(const std::string& name, CLElementPtr value);
        std::string getName() const { return content(); }
        std::string toJSON() const override;
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        void set(CLElementPtr value);
        CLElement* getValue() const { return mElements.empty() ? nullptr : mElements[0].get(); }
        bool equals(const CLElement& o) const override;
        CLElementPtr clone() const override;
    };

    class CLNumber : public CLElement {
    public:
        explicit CLNumber(CLContent content) : CLElement(std::move(content)) { mKind |= KIND_NUMBER; }
        explicit CLNumber(float value) : CLElement(nullptr), mValue(value) { mKind |= KIND_NUMBER; }
        static std::shared_ptr<CLNumber> allocate(CLContent content) { return std::make_shared<CLNumber>(std::move(content)); }
        std::string toJSON() const override;
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        bool isInt();
        int getInt() override;
        float getFloat() override;
        void putValue(float value) { mValue = value; }
        bool equals(const CLElement& o) const override;
        CLElementPtr clone() const override { return std::make_shared<CLNumber>(*this); }

    private:
        float mValue = std::numeric_limits<float>::quiet_NaN();
    };

    class CLString : public CLElement {
    public:
        explicit CLString(CLContent content) : CLElement(std::move(content)) { mKind |= KIND_STRING; }
        static std::shared_ptr<CLString> allocate(CLContent content) { return std::make_shared<CLString>(std::move(content)); }
        static std::shared_ptr<CLString> from(const std::string& content);
        std::string toJSON() const override { return "'" + content() + "'"; }
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        bool equals(const CLElement& o) const override;
        CLElementPtr clone() const override { return std::make_shared<CLString>(*this); }
    };

    class CLToken : public CLElement {
    public:
        enum class Type { UNKNOWN, TRUE, FALSE, NULL_ };
        explicit CLToken(CLContent content) : CLElement(std::move(content)) { mKind |= KIND_TOKEN; }
        static std::shared_ptr<CLToken> allocate(CLContent content) { return std::make_shared<CLToken>(std::move(content)); }
        bool getBoolean() const;
        bool isNull() const;
        std::string toJSON() const override { return content(); }
        std::string toFormattedJSON(int indent, int forceIndent) const override;
        Type getType() const { return mType; }
        bool validate(char c, int64_t position);
        CLElementPtr clone() const override { return std::make_shared<CLToken>(*this); }

    private:
        int mIndex = 0;
        Type mType = Type::UNKNOWN;
    };

    class CLParser {
    public:
        static std::shared_ptr<CLObject> parse(const std::string& string) { return CLParser(string).parse(); }
        explicit CLParser(std::string content) : mContent(std::move(content)) {}
        std::shared_ptr<CLObject> parse();

    private:
        enum class TYPE { UNKNOWN, OBJECT, ARRAY, NUMBER, STRING, KEY, TOKEN };
        CLElement* getNextJsonElement(int position, char c, CLElement* currentElement, const CLContent& content);
        CLElement* createElement(CLElement* currentElement, int position, TYPE type, bool applyStart, const CLContent& content);

        std::string mContent;
        bool mHasComment = false;
        int mLineNumber = 0;
        // Elements still open are held only by the parse in Java (their container adds them
        // when they end); this keeps them alive until it returns.
        std::vector<CLElementPtr> mAlive;
    };

}
