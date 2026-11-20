#include "kiln/constraintlayout/core/state/State.h"

#include "kiln/constraintlayout/core/state/GridReference.h"
#include "kiln/constraintlayout/core/state/StateHelpers.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/HelperWidget.h"

#include <algorithm>

namespace kiln::constraintlayout::core::state {

    const std::string State::PARENT = std::string(1, '\0');

    int State::chainValueByString(const std::string& str) {
        if (str == "packed") return ConstraintWidget::CHAIN_PACKED;
        if (str == "spread_inside") return ConstraintWidget::CHAIN_SPREAD_INSIDE;
        if (str == "spread") return ConstraintWidget::CHAIN_SPREAD;
        return UNKNOWN;
    }

    std::optional<State::Chain> State::chainByString(const std::string& str) {
        if (str == "packed") return Chain::PACKED;
        if (str == "spread_inside") return Chain::SPREAD_INSIDE;
        if (str == "spread") return Chain::SPREAD;
        return std::nullopt;
    }

    int State::wrapValueByString(const std::string& str) {
        if (str == "none") return 0;
        if (str == "chain") return 3;   // Flow.WRAP_CHAIN_NEW
        if (str == "aligned") return 2;
        return UNKNOWN;
    }

    State::State() : mParent(std::make_shared<ConstraintReference>(*this)) {
        mParent->setKey(PARENT);
        mReferences.put(PARENT, mParent);
    }

    State::~State() = default;

    void State::reset() {
        for (const std::string& ref : mReferences.keys()) (*mReferences.find(ref))->getConstraintWidget()->reset();
        mReferences.forEach([&](const std::string&, const std::shared_ptr<Reference>& r) {
            if (r != mParent) mRetiredReferences.push_back(r);
        });
        mHelperReferences.forEach([&](const std::string&, const std::shared_ptr<HelperReference>& r) { mRetiredReferences.push_back(r); });
        mReferences.clear();
        mReferences.put(PARENT, mParent);
        mHelperReferences.clear();
        mTags.clear();
        mBaselineNeeded.clear();
        mDirtyBaselineNeededWidgets = true;
        // The container still holds these until the next apply().
        for (auto& w : mWidgets) mRetiredWidgets.push_back(std::move(w));
        mWidgets.clear();
    }

    std::shared_ptr<ConstraintReference> State::createConstraintReference(const std::string& /*key*/) {
        return std::make_shared<ConstraintReference>(*this);
    }

    Reference* State::reference(const std::string& key) const {
        const auto* found = mReferences.find(key);
        return found ? found->get() : nullptr;
    }

    ConstraintReference* State::constraints(const std::string& key) {
        std::shared_ptr<Reference>* found = mReferences.find(key);
        Reference* reference = found ? found->get() : nullptr;
        if (reference == nullptr) {
            std::shared_ptr<ConstraintReference> created = createConstraintReference(key);
            reference = created.get();
            mReferences.put(key, created);
            reference->setKey(key);
        }
        return reference->asConstraintReference();
    }

    HelperReference* State::helper(std::string key, int type) {
        if (key.empty()) key = createHelperKey();
        if (std::shared_ptr<HelperReference>* found = mHelperReferences.find(key)) return found->get();
        std::shared_ptr<HelperReference> reference;
        switch (type) {
            case HORIZONTAL_CHAIN:
                reference = std::make_shared<HorizontalChainReference>(*this);
                break;
            case VERTICAL_CHAIN:
                reference = std::make_shared<VerticalChainReference>(*this);
                break;
            case ALIGN_HORIZONTALLY:
                reference = std::make_shared<AlignHorizontallyReference>(*this);
                break;
            case ALIGN_VERTICALLY:
                reference = std::make_shared<AlignVerticallyReference>(*this);
                break;
            case BARRIER:
                reference = std::make_shared<BarrierReference>(*this);
                break;
            case VERTICAL_FLOW:
            case HORIZONTAL_FLOW:
                reference = std::make_shared<FlowReference>(*this, type);
                break;
            case GRID:
            case ROW:
            case COLUMN:
                reference = std::make_shared<GridReference>(*this, type);
                break;
            default:
                reference = std::make_shared<HelperReference>(*this, type);
                break;
        }
        reference->setKey(key);
        HelperReference* raw = reference.get();
        mHelperReferences.put(key, std::move(reference));
        return raw;
    }

    GuidelineReference* State::horizontalGuideline(const std::string& key) { return guideline(key, ConstraintWidget::HORIZONTAL); }
    GuidelineReference* State::verticalGuideline(const std::string& key) { return guideline(key, ConstraintWidget::VERTICAL); }

    GuidelineReference* State::guideline(const std::string& key, int orientation) {
        ConstraintReference* reference = constraints(key);
        if (reference->getFacade() == nullptr || reference->getFacade()->asGuidelineReference() == nullptr) {
            auto guidelineReference = std::make_shared<GuidelineReference>(*this);
            guidelineReference->setOrientation(orientation);
            guidelineReference->setKey(key);
            reference->setFacade(guidelineReference);
        }
        return reference->getFacade()->asGuidelineReference();
    }

    BarrierReference* State::barrier(const std::string& key, Direction direction) {
        ConstraintReference* reference = constraints(key);
        if (reference->getFacade() == nullptr || reference->getFacade()->asBarrierReference() == nullptr) {
            auto barrierReference = std::make_shared<BarrierReference>(*this);
            barrierReference->setBarrierDirection(direction);
            reference->setFacade(barrierReference);
        }
        return reference->getFacade()->asBarrierReference();
    }

    GridReference* State::getGrid(const std::string& key, const std::string& gridType) {
        ConstraintReference* reference = constraints(key);
        if (reference->getFacade() == nullptr || reference->getFacade()->asGridReference() == nullptr) {
            int type = GRID;
            if (!gridType.empty() && gridType[0] == 'r') type = ROW;
            else if (!gridType.empty() && gridType[0] == 'c') type = COLUMN;
            reference->setFacade(std::make_shared<GridReference>(*this, type));
        }
        return reference->getFacade()->asGridReference();
    }

    FlowReference* State::getFlow(const std::string& key, bool vertical) {
        ConstraintReference* reference = constraints(key);
        if (reference->getFacade() == nullptr || reference->getFacade()->asFlowReference() == nullptr) {
            reference->setFacade(std::make_shared<FlowReference>(*this, vertical ? VERTICAL_FLOW : HORIZONTAL_FLOW));
        }
        return reference->getFacade()->asFlowReference();
    }

    VerticalChainReference* State::verticalChain() { return static_cast<VerticalChainReference*>(helper({}, VERTICAL_CHAIN)); }
    VerticalChainReference* State::verticalChain(std::initializer_list<std::string> references) {
        auto* reference = verticalChain();
        reference->add(references);
        return reference;
    }
    HorizontalChainReference* State::horizontalChain() { return static_cast<HorizontalChainReference*>(helper({}, HORIZONTAL_CHAIN)); }
    HorizontalChainReference* State::horizontalChain(std::initializer_list<std::string> references) {
        auto* reference = horizontalChain();
        reference->add(references);
        return reference;
    }
    FlowReference* State::getVerticalFlow() { return static_cast<FlowReference*>(helper({}, VERTICAL_FLOW)); }
    FlowReference* State::getHorizontalFlow() { return static_cast<FlowReference*>(helper({}, HORIZONTAL_FLOW)); }
    AlignHorizontallyReference* State::centerHorizontally(std::initializer_list<std::string> references) {
        auto* reference = static_cast<AlignHorizontallyReference*>(helper({}, ALIGN_HORIZONTALLY));
        reference->add(references);
        return reference;
    }
    AlignVerticallyReference* State::centerVertically(std::initializer_list<std::string> references) {
        auto* reference = static_cast<AlignVerticallyReference*>(helper({}, ALIGN_VERTICALLY));
        reference->add(references);
        return reference;
    }

    void State::directMapping() {
        for (const std::string& key : mReferences.keys()) {
            ConstraintReference* reference = constraints(key);
            if (reference == nullptr) continue;
            // Java maps the key object itself; here a reference's view is opaque.
            reference->setView(nullptr);
        }
    }

    void State::map(const std::string& key, void* view) {
        if (ConstraintReference* ref = constraints(key)) ref->setView(view);
    }

    void State::setTag(const std::string& key, const std::string& tag) {
        ConstraintReference* reference = constraints(key);
        if (reference == nullptr) return;
        reference->setTag(tag);
        std::vector<std::string>* list = mTags.find(tag);
        if (list == nullptr) list = &mTags.put(tag, {});
        list->push_back(key);
    }

    void State::apply(ConstraintWidgetContainer& container) {
        container.removeAllChildren();
        // The widgets reset() discarded may still be indexed by the container's solver; clear
        // it before they go (its next layout resets it anyway).
        container.getSystem().reset();
        container.invalidateGraph();
        mRetiredWidgets.clear();
        mRetiredReferences.clear();

        mParent->getWidth().apply(*this, &container, ConstraintWidget::HORIZONTAL);
        mParent->getHeight().apply(*this, &container, ConstraintWidget::VERTICAL);

        for (const std::string& key : mHelperReferences.keys()) {
            HelperReference* reference = mHelperReferences.find(key)->get();
            HelperWidget* helperWidget = reference->getHelperWidget();
            if (helperWidget != nullptr) {
                Reference* constraintReference = this->reference(key);
                if (constraintReference == nullptr) constraintReference = constraints(key);
                constraintReference->setConstraintWidget(helperWidget);
            }
        }
        for (const std::string& key : mReferences.keys()) {
            Reference* reference = mReferences.find(key)->get();
            if (reference != mParent.get() && reference->getFacade() != nullptr && reference->getFacade()->asHelperReference() != nullptr) {
                HelperWidget* helperWidget = reference->getFacade()->asHelperReference()->getHelperWidget();
                if (helperWidget != nullptr) {
                    Reference* constraintReference = this->reference(key);
                    if (constraintReference == nullptr) constraintReference = constraints(key);
                    constraintReference->setConstraintWidget(helperWidget);
                }
            }
        }
        for (const std::string& key : mReferences.keys()) {
            Reference* reference = mReferences.find(key)->get();
            if (reference != mParent.get()) {
                ConstraintWidget* widget = reference->getConstraintWidget();
                widget->setDebugName(keyToString(reference->getKey()));
                widget->setParent(nullptr);
                if (reference->getFacade() != nullptr && reference->getFacade()->asGuidelineReference() != nullptr) reference->apply();
                container.add(widget);
            } else {
                reference->setConstraintWidget(&container);
            }
        }
        for (const std::string& key : mHelperReferences.keys()) {
            HelperReference* reference = mHelperReferences.find(key)->get();
            HelperWidget* helperWidget = reference->getHelperWidget();
            if (helperWidget != nullptr) {
                for (const std::string& keyRef : reference->mReferences) {
                    Reference* constraintReference = this->reference(keyRef);
                    if (constraintReference != nullptr) reference->getHelperWidget()->add(constraintReference->getConstraintWidget());
                }
                reference->apply();
            } else {
                reference->apply();
            }
        }
        for (const std::string& key : mReferences.keys()) {
            Reference* reference = mReferences.find(key)->get();
            if (reference != mParent.get() && reference->getFacade() != nullptr && reference->getFacade()->asHelperReference() != nullptr) {
                HelperReference* helperReference = reference->getFacade()->asHelperReference();
                HelperWidget* helperWidget = helperReference->getHelperWidget();
                if (helperWidget != nullptr) {
                    for (const std::string& keyRef : helperReference->mReferences) {
                        Reference* constraintReference = this->reference(keyRef);
                        if (constraintReference != nullptr) helperWidget->add(constraintReference->getConstraintWidget());
                    }
                    reference->apply();
                }
            }
        }
        for (const std::string& key : mReferences.keys()) {
            Reference* reference = mReferences.find(key)->get();
            reference->apply();
            ConstraintWidget* widget = reference->getConstraintWidget();
            if (widget != nullptr) widget->stringId = keyToString(key);
        }
    }

    bool State::isBaselineNeeded(ConstraintWidget* constraintWidget) {
        if (mDirtyBaselineNeededWidgets) {
            mBaselineNeededWidgets.clear();
            for (const std::string& id : mBaselineNeeded) {
                Reference* reference = this->reference(id);
                ConstraintWidget* widget = reference != nullptr ? reference->getConstraintWidget() : nullptr;
                if (widget != nullptr) mBaselineNeededWidgets.push_back(widget);
            }
            mDirtyBaselineNeededWidgets = false;
        }
        return std::find(mBaselineNeededWidgets.begin(), mBaselineNeededWidgets.end(), constraintWidget) != mBaselineNeededWidgets.end();
    }

    std::optional<std::string> State::getKeyId(const HelperWidget* helperWidget) const {
        std::optional<std::string> result;
        mHelperReferences.forEach([&](const std::string& key, const std::shared_ptr<HelperReference>& r) {
            if (!result && r->getHelperWidget() == helperWidget) result = key;
        });
        return result;
    }

}
