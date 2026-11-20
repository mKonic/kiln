#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include "kiln/constraintlayout/core/widgets/Guideline.h"
#include "kiln/constraintlayout/core/widgets/analyzer/Grouping.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    ConstraintAnchor::~ConstraintAnchor() {
        for (ConstraintAnchor* d : mTargetedBy) d->mTarget = nullptr;
        if (mTarget != nullptr) {
            mTarget->removeDependent(this);
            std::erase(mTarget->mTargetedBy, this);
        }
    }

    void ConstraintAnchor::setTarget(ConstraintAnchor* target) {
        if (mTarget == target) return;
        if (mTarget != nullptr) std::erase(mTarget->mTargetedBy, this);
        mTarget = target;
        if (mTarget != nullptr) mTarget->mTargetedBy.push_back(this);
    }

    void ConstraintAnchor::addDependent(ConstraintAnchor* a) {
        if (std::find(mDependents.begin(), mDependents.end(), a) == mDependents.end()) mDependents.push_back(a);
    }
    void ConstraintAnchor::removeDependent(ConstraintAnchor* a) { std::erase(mDependents, a); }

    void ConstraintAnchor::findDependents(int orientation, std::vector<std::shared_ptr<WidgetGroup>>& list, WidgetGroup* group) {
        for (ConstraintAnchor* anchor : std::vector<ConstraintAnchor*>(mDependents)) Grouping::findDependents(anchor->mOwner, orientation, list, group);
    }

    bool ConstraintAnchor::hasCenteredDependents() const {
        for (ConstraintAnchor* anchor : mDependents) {
            ConstraintAnchor* opposite = anchor->getOpposite();
            if (opposite->isConnected()) return true;
        }
        return false;
    }

    void ConstraintAnchor::copyFrom(const ConstraintAnchor& source, const WidgetMap& map) {
        if (mTarget != nullptr) mTarget->removeDependent(this);
        if (source.mTarget != nullptr) {
            const Type type = source.mTarget->getType();
            ConstraintWidget* owner = map.at(source.mTarget->mOwner);
            setTarget(owner->getAnchor(type));
        } else {
            setTarget(nullptr);
        }
        if (mTarget != nullptr) mTarget->addDependent(this);
        mMargin = source.mMargin;
        mGoneMargin = source.mGoneMargin;
    }

    void ConstraintAnchor::resetSolverVariable(Cache* /*cache*/) {
        if (mSolverVariable == nullptr) mSolverVariable = std::make_unique<SolverVariable>(SolverVariable::Type::UNRESTRICTED, nullptr);
        else mSolverVariable->reset();
    }

    int ConstraintAnchor::getMargin() const {
        if (mOwner->getVisibility() == ConstraintWidget::GONE) return 0;
        if (mGoneMargin != UNSET_GONE_MARGIN && mTarget != nullptr && mTarget->mOwner->getVisibility() == ConstraintWidget::GONE) return mGoneMargin;
        return mMargin;
    }

    void ConstraintAnchor::reset() {
        if (mTarget != nullptr) mTarget->removeDependent(this);
        mDependents.clear();
        setTarget(nullptr);
        mMargin = 0;
        mGoneMargin = UNSET_GONE_MARGIN;
        mHasFinalValue = false;
        mFinalValue = 0;
    }

    bool ConstraintAnchor::connect(ConstraintAnchor* toAnchor, int margin, int goneMargin, bool forceConnection) {
        if (toAnchor == nullptr) {
            reset();
            return true;
        }
        if (!forceConnection && !isValidConnection(toAnchor)) return false;
        setTarget(toAnchor);
        mTarget->addDependent(this);
        mMargin = margin;
        mGoneMargin = goneMargin;
        return true;
    }

    bool ConstraintAnchor::isValidConnection(ConstraintAnchor* anchor) const {
        if (anchor == nullptr) return false;
        const Type target = anchor->getType();
        if (target == mType) {
            if (mType == Type::BASELINE && (!anchor->getOwner()->hasBaseline() || !getOwner()->hasBaseline())) return false;
            return true;
        }
        switch (mType) {
            case Type::CENTER: return target != Type::BASELINE && target != Type::CENTER_X && target != Type::CENTER_Y;
            case Type::LEFT:
            case Type::RIGHT: {
                bool isCompatible = target == Type::LEFT || target == Type::RIGHT;
                if (anchor->getOwner()->is(ConstraintWidget::KIND_GUIDELINE)) isCompatible = isCompatible || target == Type::CENTER_X;
                return isCompatible;
            }
            case Type::TOP:
            case Type::BOTTOM: {
                bool isCompatible = target == Type::TOP || target == Type::BOTTOM;
                if (anchor->getOwner()->is(ConstraintWidget::KIND_GUIDELINE)) isCompatible = isCompatible || target == Type::CENTER_Y;
                return isCompatible;
            }
            case Type::BASELINE: return !(target == Type::LEFT || target == Type::RIGHT);
            case Type::CENTER_X:
            case Type::CENTER_Y:
            case Type::NONE: return false;
        }
        return false;
    }

    bool ConstraintAnchor::isSideAnchor() const {
        switch (mType) {
            case Type::LEFT:
            case Type::RIGHT:
            case Type::TOP:
            case Type::BOTTOM: return true;
            default: return false;
        }
    }

    bool ConstraintAnchor::isSimilarDimensionConnection(ConstraintAnchor* anchor) const {
        const Type target = anchor->getType();
        if (target == mType) return true;
        switch (mType) {
            case Type::CENTER: return target != Type::BASELINE;
            case Type::LEFT:
            case Type::RIGHT:
            case Type::CENTER_X: return target == Type::LEFT || target == Type::RIGHT || target == Type::CENTER_X;
            case Type::TOP:
            case Type::BOTTOM:
            case Type::CENTER_Y:
            case Type::BASELINE: return target == Type::TOP || target == Type::BOTTOM || target == Type::CENTER_Y || target == Type::BASELINE;
            case Type::NONE: return false;
        }
        return false;
    }

    bool ConstraintAnchor::isVerticalAnchor() const {
        switch (mType) {
            case Type::LEFT:
            case Type::RIGHT:
            case Type::CENTER:
            case Type::CENTER_X: return false;
            default: return true;
        }
    }

    std::string ConstraintAnchor::toString() const {
        static const char* names[] = { "NONE", "LEFT", "TOP", "RIGHT", "BOTTOM", "BASELINE", "CENTER", "CENTER_X", "CENTER_Y" };
        return mOwner->getDebugName() + ":" + names[int(mType)];
    }

    bool ConstraintAnchor::isConnectionAllowed(ConstraintWidget* target) const {
        std::unordered_set<ConstraintWidget*> checked;
        if (isConnectionToMe(target, checked)) return false;
        ConstraintWidget* parent = getOwner()->getParent();
        if (parent == target) return true;
        if (target->getParent() == parent) return true;
        return false;
    }

    bool ConstraintAnchor::isConnectionToMe(ConstraintWidget* target, std::unordered_set<ConstraintWidget*>& checked) const {
        if (checked.contains(target)) return false;
        checked.insert(target);
        if (target == getOwner()) return true;
        const auto& targetAnchors = target->getAnchors();
        for (ConstraintAnchor* anchor : targetAnchors) {
            if (anchor->isSimilarDimensionConnection(const_cast<ConstraintAnchor*>(this)) && anchor->isConnected()) {
                if (isConnectionToMe(anchor->getTarget()->getOwner(), checked)) return true;
            }
        }
        return false;
    }

    ConstraintAnchor* ConstraintAnchor::getOpposite() const {
        switch (mType) {
            case Type::LEFT: return &mOwner->mRight;
            case Type::RIGHT: return &mOwner->mLeft;
            case Type::TOP: return &mOwner->mBottom;
            case Type::BOTTOM: return &mOwner->mTop;
            default: return nullptr;
        }
    }

    // --- LinearSystem methods over anchors and widgets ----------------------------------------------

    SolverVariable* LinearSystem::createObjectVariable(ConstraintAnchor* anchor) {
        if (anchor == nullptr) return nullptr;
        if (mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        SolverVariable* variable = anchor->getSolverVariable();
        if (variable == nullptr) {
            anchor->resetSolverVariable(&mCache);
            variable = anchor->getSolverVariable();
        }
        if (variable->id == -1 || variable->id > mVariablesID || mCache.mIndexedVariables[std::size_t(variable->id)] == nullptr) {
            if (variable->id != -1) variable->reset();
            mVariablesID++;
            mNumColumns++;
            variable->id = mVariablesID;
            variable->mType = SolverVariable::Type::UNRESTRICTED;
            mCache.mIndexedVariables[std::size_t(mVariablesID)] = variable;
        }
        return variable;
    }

    int LinearSystem::getObjectVariableValue(ConstraintAnchor* anchor) {
        // Chain.USE_CHAIN_OPTIMIZATION is false upstream.
        SolverVariable* variable = anchor->getSolverVariable();
        if (variable != nullptr) return JavaInt(variable->computedValue + 0.5f);
        return 0;
    }

    void LinearSystem::addCenterPoint(ConstraintWidget* widget, ConstraintWidget* target, float angle, int radius) {
        using T = ConstraintAnchor::Type;
        SolverVariable* Al = createObjectVariable(widget->getAnchor(T::LEFT));
        SolverVariable* At = createObjectVariable(widget->getAnchor(T::TOP));
        SolverVariable* Ar = createObjectVariable(widget->getAnchor(T::RIGHT));
        SolverVariable* Ab = createObjectVariable(widget->getAnchor(T::BOTTOM));
        SolverVariable* Bl = createObjectVariable(target->getAnchor(T::LEFT));
        SolverVariable* Bt = createObjectVariable(target->getAnchor(T::TOP));
        SolverVariable* Br = createObjectVariable(target->getAnchor(T::RIGHT));
        SolverVariable* Bb = createObjectVariable(target->getAnchor(T::BOTTOM));
        ArrayRow* row = createRow();
        float angleComponent = float(std::sin(double(angle)) * radius);
        row->createRowWithAngle(At, Ab, Bt, Bb, angleComponent);
        addConstraint(row);
        row = createRow();
        angleComponent = float(std::cos(double(angle)) * radius);
        row->createRowWithAngle(Al, Ar, Bl, Br, angleComponent);
        addConstraint(row);
    }

}
