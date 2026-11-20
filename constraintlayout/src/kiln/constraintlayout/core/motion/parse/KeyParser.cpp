#include "kiln/constraintlayout/core/motion/parse/KeyParser.h"

#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/parser/CLParser.h"

#include <cstdio>
#include <functional>

namespace kiln::constraintlayout::core::motion {

    namespace {
        TypedBundle parse(const std::string& str, const std::function<int(const std::string&)>& table, const std::function<int(int)>& dtype) {
            TypedBundle bundle;
            try {
                std::shared_ptr<CLObject> parsedContent = CLParser::parse(str);
                const int n = parsedContent->size();
                for (int i = 0; i < n; i++) {
                    auto* clkey = static_cast<CLKey*>(parsedContent->get(i));
                    const std::string type = clkey->content();
                    CLElement* value = clkey->getValue();
                    const int id = table(type);
                    if (id == -1) {
                        std::fprintf(stderr, "unknown type %s\n", type.c_str());
                        continue;
                    }
                    switch (dtype(id)) {
                        case TypedValues::FLOAT_MASK: bundle.add(id, value->getFloat()); break;
                        case TypedValues::STRING_MASK: bundle.add(id, value->content()); break;
                        case TypedValues::INT_MASK: bundle.add(id, value->getInt()); break;
                        case TypedValues::BOOLEAN_MASK: bundle.add(id, parsedContent->getBoolean(i)); break;
                    }
                }
            } catch (const CLParsingException& e) {
                std::fprintf(stderr, "%s\n", e.what());
            }
            return bundle;
        }
    }

    TypedBundle KeyParser::parseAttributes(const std::string& str) {
        return parse(str, TypedValues::AttributesType::getId, TypedValues::AttributesType::getType);
    }

}
