#pragma once

// Barrier (constraintlayout-core): a line at the farthest edge of its widgets on one side.

#include "kiln/constraintlayout/core/widgets/HelperWidget.h"

namespace kiln::constraintlayout::core {

    class Barrier : public HelperWidget {
    public:
        static constexpr int LEFT = 0, RIGHT = 1, TOP = 2, BOTTOM = 3;

        Barrier() { mKind |= KIND_BARRIER; }
        explicit Barrier(const std::string& debugName) { mKind |= KIND_BARRIER; setDebugName(debugName); }

        bool allowedInBarrier() const override { return true; }
        int getBarrierType() const { return mBarrierType; }
        void setBarrierType(int barrierType) { mBarrierType = barrierType; }
        void setAllowsGoneWidget(bool v) { mAllowsGoneWidget = v; }
        bool allowsGoneWidget() const { return mAllowsGoneWidget; }
        bool getAllowsGoneWidget() const { return mAllowsGoneWidget; }
        bool isResolvedHorizontally() const override { return mResolved; }
        bool isResolvedVertically() const override { return mResolved; }
        void copy(const ConstraintWidget& src, const WidgetMap& map) override;
        std::string toString() const;
        void markWidgets();
        void addToSolver(LinearSystem& system, bool optimize) override;
        void setMargin(int margin) { mMargin = margin; }
        int getMargin() const { return mMargin; }
        int getOrientation() const;
        bool allSolved();

        bool mResolved = false;

    private:
        int mBarrierType = LEFT;
        bool mAllowsGoneWidget = true;
        int mMargin = 0;
    };

}
