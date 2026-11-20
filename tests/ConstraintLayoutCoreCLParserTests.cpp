// Ported from constraintlayout-core CLParserTest.java (androidx). Expected values are upstream's.
#include "Check.h"

#include "kiln/constraintlayout/core/parser/CLParser.h"

#include <string>

    static void testBasicFormat(const std::string& content) {
    using namespace kiln::constraintlayout::core;
        try {
            auto parsedContent = CLParser::parse(content);
            CHECK_EQ(parsedContent->toJSON(), content);
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testParsing) {
    using namespace kiln::constraintlayout::core;
        testBasicFormat("{ a: { start: ['parent', 'start', 20], " "top: ['parent', 'top', 30] } }");
        testBasicFormat("{ test: 'hello, the', key: 'world' }");
        testBasicFormat("{ test: [1, 2, 3] }");
        testBasicFormat("{ test: ['hello', 'world', { value: 42 }] }");
        testBasicFormat("{ test: [null] }");
        testBasicFormat("{ test: [null, false, true] }");
        testBasicFormat("{ test: ['hello', 'world', { value: 42 }], value: false, " "plop: 23, hello: { key: 42, text: 'bonjour' } }");
    }
    TEST(ClParser_testValue) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ test: ['hello', 'world', { value: 42 }], value: false, plop: 23, " "hello: { key: 49, text: 'bonjour' } }";
            auto parsedContent = CLParser::parse(test);
            CHECK(parsedContent->toJSON() == test);
            CHECK_EQ("hello", parsedContent->getArray("test")->getString(0));
            CHECK_EQ("world", parsedContent->getArray("test")->getString(1));
            CHECK_EQ(42, parsedContent->getArray("test")->getObject(2)->get("value")->getInt());
            CHECK_EQ(false, parsedContent->getBoolean("value"));
            CHECK_EQ(23, parsedContent->getInt("plop"));
            CHECK_EQ(49, parsedContent->getObject("hello")->getInt("key"));
            CHECK_EQ("bonjour", parsedContent->getObject("hello")->getString("text"));
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testException) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ test: ['hello', 'world', { value: 42 }], value: false, " "plop: 23, hello: { key: 49, text: 'bonjour' } }";
            auto parsedContent = CLParser::parse(test);
            parsedContent->getObject("test")->getString(0);
        } catch (const CLParsingException& e) {
            CHECK_EQ("no object found for key <test>, found [CLArray] : " "CLArray (9 : 39) <<'hello', 'world', { value: 42 }>> = <CLString (10 : 14) " "<<hello>>; CLString (19 : 23) <<world>>; CLObject (28 : 39) " "<< value: 42 }>> = <CLKey (29 : 33) <<value>> = <CLNumber (36 : 37) " "<<42>> > > > (CLObject at line 1)", e.reason());
        }
    }
    TEST(ClParser_testTrailingCommas) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ test: ['hello', 'world'],,,,,,, }";
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("hello", parsedContent->getArray("test")->getString(0));
            CHECK_EQ("world", parsedContent->getArray("test")->getString(1));
            CHECK_EQ("{ test: ['hello', 'world'] }", parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testIncompleteObject) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ test: ['hello', 'world";
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("hello", parsedContent->getArray("test")->getString(0));
            CHECK_EQ("world", parsedContent->getArray("test")->getString(1));
            CHECK_EQ("{ test: ['hello', 'world'] }", parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testDoubleQuotes) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ test: [\"hello\", \"world\"] }";
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("hello", parsedContent->getArray("test")->getString(0));
            CHECK_EQ("world", parsedContent->getArray("test")->getString(1));
            CHECK_EQ("{ test: ['hello', 'world'] }", parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testDoubleQuotesKey) {
    using namespace kiln::constraintlayout::core;
        try {
            std::string test = "{ \"test\": [\"hello\", \"world\"] }";
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{ test: ['hello', 'world'] }", parsedContent->toJSON());
            CHECK_EQ("hello", parsedContent->getArray("test")->getString(0));
            CHECK_EQ("world", parsedContent->getArray("test")->getString(1));
            CHECK_EQ("{ test: ['hello', 'world'] }", parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testMultilines) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{\n" "  firstName: 'John',\n" "  lastName: 'Smith',\n" "  isAlive: true,\n" "  age: 27,\n" "  address: {\n" "    streetAddress: '21 2nd Street',\n" "    city: 'New York',\n" "    state: 'NY',\n" "    postalCode: '10021-3100'\n" "  },\n" "  phoneNumbers: [\n" "    {\n" "      type: 'home',\n" "      number: '212 555-1234'\n" "    },\n" "    {\n" "      type: 'office',\n" "      number: '646 555-4567'\n" "    }\n" "  ],\n" "  children: [],\n" "  spouse: null\n" "}          ";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("John", parsedContent->getString("firstName"));
            CHECK_EQ("{ firstName: 'John', lastName: 'Smith', isAlive: true, " "age: 27, address: { streetAddress: '21 2nd Street', city: 'New " "York', " "state: 'NY', postalCode: '10021-3100' }, " "phoneNumbers: [{ type: 'home', number: '212 555-1234' }, " "{ type: 'office', number: '646 555-4567' }], " "children: [], spouse: null }",
                    parsedContent->toJSON());
            CHECK_EQ(2, parsedContent->getArray("phoneNumbers")->size());
            CLElement* element = parsedContent->get("spouse");
            if (element->is(CLElement::KIND_TOKEN)) {
                auto* token = static_cast<CLToken*>(element);
                CHECK(CLToken::Type::NULL_ == token->getType());
            }
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testDoubleQuotesMultilines) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{\n" "  \"firstName\": \"John\",\n" "  \"lastName\": \"Smith\",\n" "  \"isAlive\": true,\n" "  \"age\": 27,\n" "  \"address\": {\n" "    \"streetAddress\": \"21 2nd Street\",\n" "    \"city\": \"New York\",\n" "    \"state\": \"NY\",\n" "    \"postalCode\": \"10021-3100\"\n" "  },\n" "  \"phoneNumbers\": [\n" "    {\n" "      \"type\": \"home\",\n" "      \"number\": \"212 555-1234\"\n" "    },\n" "    {\n" "      \"type\": \"office\",\n" "      \"number\": \"646 555-4567\"\n" "    }\n" "  ],\n" "  \"children\": [],\n" "  \"spouse\": null\n" "}          ";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("John", parsedContent->getString("firstName"));
            CHECK_EQ("{ firstName: 'John', lastName: 'Smith', isAlive: true, " "age: 27, address: { streetAddress: '21 2nd Street', city: 'New York', " "state: 'NY', postalCode: '10021-3100' }, " "phoneNumbers: [{ type: 'home', number: '212 555-1234' }, " "{ type: 'office', number: '646 555-4567' }], " "children: [], spouse: null }", parsedContent->toJSON());
            CHECK_EQ(2, parsedContent->getArray("phoneNumbers")->size());
            CLElement* element = parsedContent->get("spouse");
            if (element->is(CLElement::KIND_TOKEN)) {
                auto* token = static_cast<CLToken*>(element);
                CHECK(CLToken::Type::NULL_ == token->getType());
            }
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testJSON5) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{\n" "  // comments\n  unquoted: 'and you can quote me on that',\n" "  singleQuotes: 'I can use \"double quotes\" here',\n" "  leadingDecimalPoint: .8675309, andTrailing: 8675309.,\n" "  positiveSign: +1,\n" "  trailingComma: 'in objects', andIn: ['arrays',],\n" "  \"backwardsCompatible\": \"with JSON\",\n" "}";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{ unquoted: 'and you can quote me on that', " "singleQuotes: 'I can use \"double quotes\" here', " "leadingDecimalPoint: 0.8675309, andTrailing: 8675309, " "positiveSign: 1, trailingComma: 'in objects', " "andIn: ['arrays'], backwardsCompatible: 'with JSON' }",
                    parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testConstraints) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{\n" "  g1 : { type: 'vGuideline', start: 44 },\n" "  g2 : { type: 'vGuideline', end: 44 },\n" "  image: {\n" "    width: 201, height: 179,\n" "    top: ['parent','top', 32],\n" "    start: 'g1'\n" "  },\n";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{ g1: { type: 'vGuideline', start: 44 }, " "g2: { type: 'vGuideline', end: 44 }, " "image: { width: 201, height: 179, top: ['parent', 'top', 32], " "start: 'g1' } }",
                    parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testConstraints2) {
    using namespace kiln::constraintlayout::core;
        std::string test = "            {\n" "              Variables: {\n" "                bottom: 20\n" "              },\n" "              Helpers: [\n" "                ['hChain', ['a','b','c'], {\n" "                  start: ['leftGuideline1', 'start'],\n" "                  style: 'packed'\n" "                }],\n" "                ['hChain', ['d','e','f']],\n" "                ['vChain', ['d','e','f'], {\n" "                  bottom: ['topGuideline1', 'top']\n" "                }],\n" "                ['vGuideline', {\n" "                  id: 'leftGuideline1', start: 100\n" "                }],\n" "                ['hGuideline', {\n" "                  id: 'topGuideline1', percent: 0.5\n" "                }]\n" "              ],\n" "              a: {\n" "                bottom: ['b', 'top', 'bottom']\n" "              },\n" "              b: {\n" "                width: '30%',\n" "                height: '1:1',\n" "                centerVertically: 'parent'\n" "              },\n" "              c: {\n" "                top: ['b', 'bottom']\n" "              }\n" "            }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{ " "Variables: { bottom: 20 }, " "Helpers: [" "['hChain', ['a', 'b', 'c'], { start: ['leftGuideline1', 'start'], " "style: 'packed' }], " "['hChain', ['d', 'e', 'f']], " "['vChain', ['d', 'e', 'f'], { bottom: ['topGuideline1', 'top'] }], " "['vGuideline', { id: 'leftGuideline1', start: 100 }], " "['hGuideline', { id: 'topGuideline1', percent: 0.5 }]" "], " "a: { bottom: ['b', 'top', 'bottom'] }, " "b: { width: '30%', height: '1:1', centerVertically: 'parent' }, " "c: { top: ['b', 'bottom'] } }",
                    parsedContent->toJSON());
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testConstraints3) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{\n" "                ConstraintSets: {\n" "                  start: {\n" "                    a: {\n" "                      width: 40,\n" "                      height: 40,\n" "                      start: ['parent', 'start', 16],\n" "                      bottom: ['parent', 'bottom', 16]\n" "                    }\n" "                  },\n" "                  end: {\n" "                    a: {\n" "                      width: 40,\n" "                      height: 40,\n" "                      //rotationZ: 390,\n" "                      end: ['parent', 'end', 16],\n" "                      top: ['parent', 'top', 16]\n" "                    }\n" "                  }\n" "                },\n" "                Transitions: {\n" "                  default: {\n" "                    from: 'start',\n" "                    to: 'end',\n" "                    pathMotionArc: 'startHorizontal',\n" "                    KeyFrames: {\n" "//                      KeyPositions: [\n" "//                        {\n" "//                          target: ['a'],\n" "//                          frames: [25, 50, 75],\n" "////                          percentX: [0.4, 0.8, 0.1],\n" "////                          percentY: [0.4, 0.8, 0.3]\n" "//                        }\n" "//                      ],\n" "                      KeyAttributes: [\n" "                        {\n" "                          target: ['a'],\n" "                          frames: [25, 50],\n" "                          scaleX: 3,\n" "                          scaleY: .3\n" "                        }\n" "                      ]\n" "                    }\n" "                  }\n" "                }\n" "            }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{ ConstraintSets: { start: { a: { width: 40, height: 40, " "start: ['parent', 'start', 16], bottom: ['parent', 'bottom', 16] }" " }, " "end: { a: { width: 40, height: 40, end: ['parent', 'end', 16]," " top: ['parent', 'top', 16] } } }, " "Transitions: { default: { from: 'start', to: 'end', " "pathMotionArc: 'startHorizontal', " "KeyFrames: { KeyAttributes: [{ target: ['a'], frames: [25, 50], " "scaleX: 3, scaleY: 0.3 }] } } } }",
                    parsedContent->toJSON());
            CLObject* transitions = parsedContent->getObject("Transitions");
            CLObject* transition = transitions->getObject("default");
            CLObject* keyframes = transition->getObjectOrNull("KeyFrames");
            CLArray* keyattributes = keyframes->getArrayOrNull("KeyAttributes");
            CHECK(keyattributes != nullptr);
        } catch (const CLParsingException& e) {
            CHECK(false);
        }
    }
    TEST(ClParser_testFormatting) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ firstName: 'John', lastName: 'Smith', isAlive: true, " "age: 27, address: { streetAddress: '21 2nd Street', city: 'New York', " "state: 'NY', postalCode: '10021-3100' }, " "phoneNumbers: [{ type: 'home', number: '212 555-1234' }, " "{ type: 'office', number: '646 555-4567' }], " "children: [], spouse: null }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  firstName: 'John',\n" "  lastName: 'Smith',\n" "  isAlive: true,\n" "  age: 27,\n" "  address: {\n" "    streetAddress: '21 2nd Street',\n" "    city: 'New York',\n" "    state: 'NY',\n" "    postalCode: '10021-3100'\n" "  },\n" "  phoneNumbers: [\n" "    {\n" "      type: 'home',\n" "      number: '212 555-1234'\n" "    },\n" "    {\n" "      type: 'office',\n" "      number: '646 555-4567'\n" "    }\n" "  ],\n" "  children: [],\n" "  spouse: null\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting2) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ ConstraintSets: { start: { a: { width: 40, height: 40, " "start: ['parent', 'start', 16], bottom: ['parent', 'bottom', 16] } }, end: " "{ a: { width: 40, height: 40, end: ['parent', 'end', 16]," " top: ['parent', 'top', 16]" " } } }, Transitions: { default: { from: 'start', to: 'end', " "pathMotionArc: 'startHorizontal', KeyFrames: { KeyAttributes: [{ target: ['a'], " "frames: [25, 50], scaleX: 3, scaleY: 0.3 }] } } } }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  ConstraintSets: {\n" "    start: {\n" "      a: {\n" "        width: 40,\n" "        height: 40,\n" "        start: ['parent', 'start', 16],\n" "        bottom: ['parent', 'bottom', 16]\n" "      }\n" "    },\n" "    end: {\n" "      a: {\n" "        width: 40,\n" "        height: 40,\n" "        end: ['parent', 'end', 16],\n" "        top: ['parent', 'top', 16]\n" "      }\n" "    }\n" "  },\n" "  Transitions: {\n" "    default: {\n" "      from: 'start',\n" "      to: 'end',\n" "      pathMotionArc: 'startHorizontal',\n" "      KeyFrames: {\n" "        KeyAttributes: [\n" "          {\n" "            target: ['a'],\n" "            frames: [25, 50],\n" "            scaleX: 3,\n" "            scaleY: 0.3\n" "          }\n" "        ]\n" "      }\n" "    }\n" "  }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting3) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ ConstraintSets: {\n" "      Generate: { texts: { top: ['parent', 'top', 'margin'], " "start: ['parent', 'end', 16] } } } }\n";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  ConstraintSets: {\n" "    Generate: {\n" "      texts: {\n" "        top: ['parent', 'top', 'margin'],\n" "        start: ['parent', 'end', 16]\n" "      }\n" "    }\n" "  }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting4) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ Transitions: { default: { from: 'start', to: 'end', " "pathMotionArc: 'startHorizontal', KeyFrames: { KeyAttributes: [{ target: ['a'], " "frames: [25, 50], scaleX: 3, scaleY: 0.3 }] } } } }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  Transitions: {\n" "    default: {\n" "      from: 'start',\n" "      to: 'end',\n" "      pathMotionArc: 'startHorizontal',\n" "      KeyFrames: {\n" "        KeyAttributes: [\n" "          {\n" "            target: ['a'],\n" "            frames: [25, 50],\n" "            scaleX: 3,\n" "            scaleY: 0.3\n" "          }\n" "        ]\n" "      }\n" "    }\n" "  }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting5) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ Debug: { name: 'motion6' }, ConstraintSets: {\n" "    start: { Variables: { texts: { tag: 'text' }, " "margin: { from: 0, step: 50 }\n" "      }, Generate: { texts: { top: ['parent', 'top', 'margin'], " "start: ['parent', 'end', 16] }\n" "      }, box: { width: 'spread', height: 64, centerHorizontally: 'parent',\n" "        bottom: ['parent', 'bottom'] }, content: { width: 'spread',\n" "        height: '400', centerHorizontally: 'parent', " "top: ['box', 'bottom', 32]\n" "      }, name: { centerVertically: 'box', start: ['parent', 'start', 16] }\n" "    }, end: { Variables: { texts: { tag: 'text' },\n" "        margin: { from: 0, step: 50 } }, Generate: {\n" "        texts: { start: ['parent', 'start', 32], " "top: ['content', 'top', 'margin'] }\n" "      }, box: { width: 'spread', height: 200, centerHorizontally: 'parent',\n" "        top: ['parent', 'top'] }, content: {\n" "        width: 'spread', height: 'spread', centerHorizontally: 'parent',\n" "        top: ['box', 'bottom'], bottom: ['parent', 'bottom']\n" "      }, name: { rotationZ: 90, scaleX: 2, scaleY: 2,\n" "        end: ['parent', 'end', 16], top: ['parent', 'top', 90]\n" "      } } }, Transitions: { default: { from: 'start', to: 'end',\n" "      pathMotionArc: 'startHorizontal', KeyFrames: {\n" "        KeyAttributes: [ { target: ['box', 'content'],\n" "            frames: [50], rotationZ: [25], rotationY: [25]\n" "          } ] } } } }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  Debug: { name: 'motion6' },\n" "  ConstraintSets: {\n" "    start: {\n" "      Variables: {\n" "        texts: {\n" "          tag: 'text'\n" "        },\n" "        margin: {\n" "          from: 0,\n" "          step: 50\n" "        }\n" "      },\n" "      Generate: {\n" "        texts: {\n" "          top: ['parent', 'top', 'margin'],\n" "          start: ['parent', 'end', 16]\n" "        }\n" "      },\n" "      box: {\n" "        width: 'spread',\n" "        height: 64,\n" "        centerHorizontally: 'parent',\n" "        bottom: ['parent', 'bottom']\n" "      },\n" "      content: {\n" "        width: 'spread',\n" "        height: '400',\n" "        centerHorizontally: 'parent',\n" "        top: ['box', 'bottom', 32]\n" "      },\n" "      name: { centerVertically: 'box', start: ['parent', 'start', 16] }\n" "    },\n" "    end: {\n" "      Variables: {\n" "        texts: {\n" "          tag: 'text'\n" "        },\n" "        margin: {\n" "          from: 0,\n" "          step: 50\n" "        }\n" "      },\n" "      Generate: {\n" "        texts: {\n" "          start: ['parent', 'start', 32],\n" "          top: ['content', 'top', 'margin']\n" "        }\n" "      },\n" "      box: {\n" "        width: 'spread',\n" "        height: 200,\n" "        centerHorizontally: 'parent',\n" "        top: ['parent', 'top']\n" "      },\n" "      content: {\n" "        width: 'spread',\n" "        height: 'spread',\n" "        centerHorizontally: 'parent',\n" "        top: ['box', 'bottom'],\n" "        bottom: ['parent', 'bottom']\n" "      },\n" "      name: {\n" "        rotationZ: 90,\n" "        scaleX: 2,\n" "        scaleY: 2,\n" "        end: ['parent', 'end', 16],\n" "        top: ['parent', 'top', 90]\n" "      }\n" "    }\n" "  },\n" "  Transitions: {\n" "    default: {\n" "      from: 'start',\n" "      to: 'end',\n" "      pathMotionArc: 'startHorizontal',\n" "      KeyFrames: {\n" "        KeyAttributes: [\n" "          {\n" "            target: ['box', 'content'],\n" "            frames: [50],\n" "            rotationZ: [25],\n" "            rotationY: [25]\n" "          }\n" "        ]\n" "      }\n" "    }\n" "  }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting6) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ root: {interpolated: {left: 0, top: 0, right: 800, bottom: 772}}, " "button: {interpolated: {left: 0, top: 372, right: 800, bottom: 401}}, " "text1: {interpolated: {left: 100, top: 285, right: 208, bottom: 301}}, " "text2: {interpolated: {left: 723, top: 736, right: 780, bottom: 752}}, " "g1: {type: 'vGuideline',interpolated: {left: 100, top: 0," " right: 100, bottom: 772}}, }";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  root: { interpolated: { left: 0, top: 0, right: 800, bottom: 772 } },\n" "  button: { interpolated: { left: 0, top: 372, " "right: 800, bottom: 401 } },\n" "  text1: { interpolated: { left: 100, top: 285, " "right: 208, bottom: 301 } },\n" "  text2: { interpolated: { left: 723, top: 736, " "right: 780, bottom: 752 } },\n" "  g1: {\n" "    type: 'vGuideline',\n" "    interpolated: { left: 100, top: 0, right: 100, bottom: 772 }\n" "  }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting7) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ root: {left: 0, top: 0, right: 800, bottom: 772}, " "button: {left: 0, top: 372, right: 800, bottom: 401}, ";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  root: { left: 0, top: 0, right: 800, bottom: 772 },\n" "  button: { left: 0, top: 372, right: 800, bottom: 401 }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
    TEST(ClParser_testFormatting8) {
    using namespace kiln::constraintlayout::core;
        std::string test = "{ root: { bottom: 772}, " "button: { bottom: 401 }, ";
        try {
            auto parsedContent = CLParser::parse(test);
            CHECK_EQ("{\n" "  root: { bottom: 772 },\n" "  button: { bottom: 401 }\n" "}", parsedContent->toFormattedJSON());
        } catch (const CLParsingException& e) {
        }
    }
