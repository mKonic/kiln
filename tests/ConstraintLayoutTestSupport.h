#pragma once

// Shared by the ConstraintLayout core tests ported from constraintlayout-core: a widget arena
// (upstream's tests rely on the GC) and upstream's test measurers.

#include "Check.h"

#include "kiln/constraintlayout/core/widgets/Barrier.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/Guideline.h"
#include "kiln/constraintlayout/core/widgets/Optimizer.h"
#include "kiln/constraintlayout/core/widgets/Flow.h"
#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"

#include <climits>
#include <initializer_list>
#include <cmath>
#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    // Owns a test's widgets; containers are destroyed first so children never outlive them.
    class Widgets {
    public:
        template<typename T, typename... A>
        T* make(A&&... args) {
            auto w = std::make_unique<T>(std::forward<A>(args)...);
            T* p = w.get();
            mWidgets.push_back(std::move(w));
            return p;
        }
        ~Widgets() {
            for (auto it = mWidgets.rbegin(); it != mWidgets.rend(); ++it) it->reset();
        }

    private:
        std::vector<std::unique_ptr<ConstraintWidget>> mWidgets;
    };

    // BasicTest.sMeasurer: fixed and match-constraint sizes as given; wrap is left as it was.
    struct BasicTestMeasurer : BasicMeasure::Measurer {
        void measure(ConstraintWidget* widget, BasicMeasure::Measure& measure) override {
            using DB = ConstraintWidget::DimensionBehaviour;
            if (measure.horizontalBehavior == DB::FIXED) measure.measuredWidth = measure.horizontalDimension;
            else if (measure.horizontalBehavior == DB::MATCH_CONSTRAINT) measure.measuredWidth = measure.horizontalDimension;
            if (measure.verticalBehavior == DB::FIXED) measure.measuredHeight = measure.verticalDimension;
            widget->setMeasureRequested(false);
        }
        void didMeasures() override {}
    };
    inline BasicTestMeasurer sMeasurer;

    // The tests' applyChain helpers: each widget's start to the previous one's end and back.
    inline void applyChain(int direction, std::initializer_list<ConstraintWidget*> list) {
        std::vector<ConstraintWidget*> widgets(list);
        ConstraintWidget* previous = widgets[0];
        for (std::size_t i = 1; i < widgets.size(); i++) {
            ConstraintWidget* widget = widgets[i];
            if (direction == ConstraintWidget::HORIZONTAL) {
                widget->connect(ConstraintAnchor::Type::LEFT, previous, ConstraintAnchor::Type::RIGHT);
                previous->connect(ConstraintAnchor::Type::RIGHT, widget, ConstraintAnchor::Type::LEFT);
            } else {
                widget->connect(ConstraintAnchor::Type::TOP, previous, ConstraintAnchor::Type::BOTTOM);
                previous->connect(ConstraintAnchor::Type::BOTTOM, widget, ConstraintAnchor::Type::TOP);
            }
            previous = widget;
        }
    }
    inline void applyChain(const std::vector<ConstraintWidget*>& widgets, int direction) {
        ConstraintWidget* previous = widgets[0];
        for (std::size_t i = 1; i < widgets.size(); i++) {
            ConstraintWidget* widget = widgets[i];
            if (direction == 0) {
                widget->connect(ConstraintAnchor::Type::LEFT, previous, ConstraintAnchor::Type::RIGHT);
                previous->connect(ConstraintAnchor::Type::RIGHT, widget, ConstraintAnchor::Type::LEFT);
            } else {
                widget->connect(ConstraintAnchor::Type::TOP, previous, ConstraintAnchor::Type::BOTTOM);
                previous->connect(ConstraintAnchor::Type::BOTTOM, widget, ConstraintAnchor::Type::TOP);
            }
            previous = widget;
        }
    }

}
