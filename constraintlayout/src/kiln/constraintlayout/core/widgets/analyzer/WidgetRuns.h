#pragma once

// constraintlayout-core analyzer runs: Dependency, DependencyNode, DimensionDependency,
// WidgetRun and its kinds (HorizontalWidgetRun, VerticalWidgetRun, ChainRun, GuidelineReference,
// HelperReferences), and RunGroup. A line port; instanceof is a kind field.
//
// Ownership: a widget owns its horizontal and vertical runs and its chain runs (shared, as several
// widgets share a chain run); the dependency graph holds the guideline and helper references it
// makes. Nodes refer to each other by pointer, as upstream's lists do.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class WidgetRun;
    class RunGroup;
    class DimensionDependency;
    class ConstraintWidgetContainer;

    // Java's (long) cast of a float.
    inline long long JavaLong(float v) {
        if (std::isnan(v)) return 0;
        if (v >= 9.2233720368547758e18f) return LLONG_MAX;
        if (v <= -9.2233720368547758e18f) return LLONG_MIN;
        return (long long)v;
    }

    class Dependency {
    public:
        virtual ~Dependency() = default;
        virtual void update(Dependency* node) = 0;
        // instanceof DependencyNode / WidgetRun.
        virtual bool isNode() const { return false; }
    };

    class DependencyNode : public Dependency {
    public:
        enum class Type { UNKNOWN, HORIZONTAL_DIMENSION, VERTICAL_DIMENSION, LEFT, RIGHT, TOP, BOTTOM, BASELINE };

        explicit DependencyNode(WidgetRun* run) : mRun(run) {}
        bool isNode() const override { return true; }
        virtual bool isDimension() const { return false; }   // instanceof DimensionDependency

        Dependency* updateDelegate = nullptr;
        bool delegateToWidgetRun = false;
        bool readyToSolve = false;
        WidgetRun* mRun;
        Type mType = Type::UNKNOWN;
        int mMargin = 0;
        int value = 0;
        int mMarginFactor = 1;
        DimensionDependency* mMarginDependency = nullptr;
        bool resolved = false;
        std::vector<Dependency*> mDependencies;
        std::vector<DependencyNode*> mTargets;

        virtual void resolve(int value);
        void update(Dependency* node) override;
        void addDependency(Dependency* dependency);
        void clear();
        std::string toString() const;
    };

    class DimensionDependency : public DependencyNode {
    public:
        explicit DimensionDependency(WidgetRun* run);
        bool isDimension() const override { return true; }
        int wrapValue = 0;
    };

    class WidgetRun : public Dependency {
    public:
        enum class Kind { HORIZONTAL, VERTICAL, CHAIN, GUIDELINE, HELPER };
        enum class RunType { NONE, START, END, CENTER };

        WidgetRun(ConstraintWidget* widget, Kind kind) : mKind(kind), mWidget(widget) {}
        WidgetRun(const WidgetRun&) = delete;
        WidgetRun& operator=(const WidgetRun&) = delete;

        const Kind mKind;   // first: mDimension's constructor reads it (instanceof HorizontalWidgetRun)
        int matchConstraintsType = 0;
        ConstraintWidget* mWidget;
        RunGroup* mRunGroup = nullptr;
        ConstraintWidget::DimensionBehaviour mDimensionBehavior = ConstraintWidget::DimensionBehaviour::FIXED;
        DimensionDependency mDimension{ this };
        int orientation = ConstraintWidget::HORIZONTAL;
        bool mResolved = false;
        DependencyNode start{ this };
        DependencyNode end{ this };
        RunType mRunType = RunType::NONE;

        virtual void clear() = 0;
        virtual void apply() = 0;
        virtual void applyToWidget() = 0;
        virtual void reset() = 0;
        virtual bool supportsWrapComputation() const = 0;
        bool isDimensionResolved() const { return mDimension.resolved; }
        bool isCenterConnection() const;
        long long wrapSize(int direction) const;
        void update(Dependency* /*dependency*/) override {}
        virtual long long getWrapDimension() const { return mDimension.resolved ? mDimension.value : 0; }
        bool isResolved() const { return mResolved; }

    protected:
        DependencyNode* getTarget(ConstraintAnchor* anchor);
        DependencyNode* getTarget(ConstraintAnchor* anchor, int orientation);
        void updateRunCenter(Dependency* dependency, ConstraintAnchor* startAnchor, ConstraintAnchor* endAnchor, int orientation);
        void updateRunStart(Dependency* /*dependency*/) {}
        void updateRunEnd(Dependency* /*dependency*/) {}
        int getLimitedDimension(int dimension, int orientation) const;
        void addTarget(DependencyNode* node, DependencyNode* target, int margin);
        void addTarget(DependencyNode* node, DependencyNode* target, int marginFactor, DimensionDependency* dimensionDependency);

    private:
        void resolveDimension(int orientation, int distance);
    };

    class HorizontalWidgetRun : public WidgetRun {
    public:
        explicit HorizontalWidgetRun(ConstraintWidget* widget);
        std::string toString() const { return "HorizontalRun " + mWidget->getDebugName(); }
        void clear() override;
        void reset() override;
        bool supportsWrapComputation() const override;
        void apply() override;
        void update(Dependency* dependency) override;
        void applyToWidget() override;

    private:
        static void computeInsetRatio(int* dimensions, int x1, int x2, int y1, int y2, float ratio, int side);
    };

    class VerticalWidgetRun : public WidgetRun {
    public:
        explicit VerticalWidgetRun(ConstraintWidget* widget);
        std::string toString() const { return "VerticalRun " + mWidget->getDebugName(); }
        DependencyNode baseline{ this };
        // Upstream's BaselineDimensionDependency: a DimensionDependency (its update overload is
        // never reached through Dependency).
        std::unique_ptr<DimensionDependency> mBaselineDimension;
        void clear() override;
        void reset() override;
        bool supportsWrapComputation() const override;
        void update(Dependency* dependency) override;
        void apply() override;
        void applyToWidget() override;

    private:
        DimensionDependency* baselineDimension();
        // apply() replaces the baseline dimension; a node's mMarginDependency may still point at
        // an earlier one (upstream's GC keeps it), so those stay until nothing refers to them.
        void retireBaselineDimension();
        std::vector<std::unique_ptr<DimensionDependency>> mRetiredBaselineDimensions;
    };

    class ChainRun : public WidgetRun {
    public:
        ChainRun(ConstraintWidget* widget, int orientation);
        // Points each chain member's horizontalChainRun / verticalChainRun at this run (upstream
        // does it in the constructor).
        static void attach(const std::shared_ptr<ChainRun>& run);
        std::string toString() const;
        bool supportsWrapComputation() const override;
        long long getWrapDimension() const override;
        void clear() override;
        void reset() override;
        void update(Dependency* dependency) override;
        void applyToWidget() override;
        void apply() override;

        std::vector<WidgetRun*> mWidgets;

    private:
        void build();
        ConstraintWidget* getFirstVisibleWidget() const;
        ConstraintWidget* getLastVisibleWidget() const;
        int mChainStyle = 0;
    };

    class GuidelineReference : public WidgetRun {
    public:
        explicit GuidelineReference(ConstraintWidget* widget);
        void clear() override { start.clear(); }
        void reset() override {
            start.resolved = false;
            end.resolved = false;
        }
        bool supportsWrapComputation() const override { return false; }
        void update(Dependency* dependency) override;
        void apply() override;
        void applyToWidget() override;

    private:
        void addDependency(DependencyNode* node);
    };

    class HelperReferences : public WidgetRun {
    public:
        explicit HelperReferences(ConstraintWidget* widget) : WidgetRun(widget, Kind::HELPER) {}
        void clear() override {
            mRunGroup = nullptr;
            start.clear();
        }
        void reset() override { start.resolved = false; }
        bool supportsWrapComputation() const override { return false; }
        void apply() override;
        void update(Dependency* dependency) override;
        void applyToWidget() override;

    private:
        void addDependency(DependencyNode* node);
    };

    class RunGroup {
    public:
        static constexpr int START = 0, END = 1, BASELINE = 2;
        static inline int index = 0;

        RunGroup(WidgetRun* run, int dir) : mGroupIndex(index), mFirstRun(run), mLastRun(run), mDirection(dir) { index++; }

        int position = 0;
        bool dual = false;
        int mGroupIndex = 0;
        WidgetRun* mFirstRun = nullptr;
        WidgetRun* mLastRun = nullptr;
        std::vector<WidgetRun*> mRuns;
        int mDirection;

        void add(WidgetRun* run) {
            mRuns.push_back(run);
            mLastRun = run;
        }
        long long computeWrapSize(ConstraintWidgetContainer* container, int orientation);
        void defineTerminalWidgets(bool horizontalCheck, bool verticalCheck);

    private:
        long long traverseStart(DependencyNode* node, long long startPosition);
        long long traverseEnd(DependencyNode* node, long long startPosition);
        bool defineTerminalWidget(WidgetRun* run, int orientation);
    };

}
