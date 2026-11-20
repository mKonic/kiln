#include "kiln/constraintlayout/core/widgets/HelperWidget.h"

#include "kiln/constraintlayout/core/widgets/analyzer/Grouping.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    void HelperWidget::add(ConstraintWidget* widget) {
        if (widget == this || widget == nullptr) return;
        if (mWidgetsCount + 1 > int(mWidgets.size())) mWidgets.resize(mWidgets.size() * 2, nullptr);
        mWidgets[std::size_t(mWidgetsCount)] = widget;
        mWidgetsCount++;
    }

    void HelperWidget::copy(const ConstraintWidget& src, const WidgetMap& map) {
        ConstraintWidget::copy(src, map);
        const auto& srcHelper = static_cast<const HelperWidget&>(src);
        mWidgetsCount = 0;
        for (int i = 0; i < srcHelper.mWidgetsCount; i++) {
            auto it = map.find(srcHelper.mWidgets[std::size_t(i)]);
            add(it == map.end() ? nullptr : it->second);
        }
    }

    void HelperWidget::removeAllIds() {
        mWidgetsCount = 0;
        std::fill(mWidgets.begin(), mWidgets.end(), nullptr);
    }

    void HelperWidget::addDependents(std::vector<std::shared_ptr<WidgetGroup>>& dependencyLists, int orientation, WidgetGroup* group) {
        for (int i = 0; i < mWidgetsCount; i++) group->add(mWidgets[std::size_t(i)]);
        for (int i = 0; i < mWidgetsCount; i++) Grouping::findDependents(mWidgets[std::size_t(i)], orientation, dependencyLists, group);
    }

    int HelperWidget::findGroupInDependents(int orientation) const {
        for (int i = 0; i < mWidgetsCount; i++) {
            const ConstraintWidget* widget = mWidgets[std::size_t(i)];
            if (orientation == HORIZONTAL && widget->horizontalGroup != -1) return widget->horizontalGroup;
            if (orientation == VERTICAL && widget->verticalGroup != -1) return widget->verticalGroup;
        }
        return -1;
    }

}
