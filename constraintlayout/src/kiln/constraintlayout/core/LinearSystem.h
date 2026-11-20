#pragma once

// The ConstraintLayout solver (androidx constraintlayout-core: Cache, Pools, SolverVariable,
// ArrayRow, ArrayLinkedVariables, PriorityGoalRow, GoalRow, LinearSystem), a line port. Names
// follow upstream so the two can be read side by side. The cache owns every row and variable;
// the pools are free lists over them. Only the default engine is ported (ArrayLinkedVariables:
// upstream's OPTIMIZED_ENGINE is never enabled), without Metrics.
//
// Floats stay floats and the library builds with -ffp-contract=off, so results match Java's.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::constraintlayout::core {

    // Java's (int) cast of a float: toward zero, saturating, NaN as 0.
    inline int JavaInt(float v) {
        if (std::isnan(v)) return 0;
        if (v >= 2147483647.0f) return std::numeric_limits<int>::max();
        if (v <= -2147483648.0f) return std::numeric_limits<int>::min();
        return int(v);
    }
    inline int JavaInt(double v) {
        if (std::isnan(v)) return 0;
        if (v >= 2147483647.0) return std::numeric_limits<int>::max();
        if (v <= -2147483648.0) return std::numeric_limits<int>::min();
        return int(v);
    }

    class ArrayRow;
    class LinearSystem;
    class Cache;
    class ConstraintAnchor;
    class ConstraintWidget;

    class SolverVariable {
    public:
        static constexpr int STRENGTH_NONE = 0, STRENGTH_LOW = 1, STRENGTH_MEDIUM = 2, STRENGTH_HIGH = 3, STRENGTH_HIGHEST = 4,
                             STRENGTH_EQUALITY = 5, STRENGTH_BARRIER = 6, STRENGTH_CENTERING = 7, STRENGTH_FIXED = 8;
        static constexpr int MAX_STRENGTH = 9;

        enum class Type { UNRESTRICTED, CONSTANT, SLACK, ERROR, UNKNOWN };

        SolverVariable(std::string name, Type type) : mType(type), mName(std::move(name)) {}
        SolverVariable(Type type, const char* /*prefix*/) : mType(type) {}

        bool inGoal = false;
        int id = -1;
        int mDefinitionId = -1;
        int strength = 0;
        float computedValue = 0;
        bool isFinalValue = false;
        std::array<float, MAX_STRENGTH> mStrengthVector{};
        std::array<float, MAX_STRENGTH> mGoalStrengthVector{};
        Type mType;
        std::vector<ArrayRow*> mClientEquations = std::vector<ArrayRow*>(16);
        int mClientEquationsCount = 0;
        int usageInRowCount = 0;
        bool mIsSynonym = false;
        int mSynonym = -1;
        float mSynonymDelta = 0;

        static void increaseErrorId() { ++sUniqueErrorId; }

        void clearStrengths() { mStrengthVector.fill(0); }
        void addToRow(ArrayRow* row);
        void removeFromRow(ArrayRow* row);
        void updateReferencesWithNewDefinition(LinearSystem& system, ArrayRow* definition);
        void setFinalValue(LinearSystem& system, float value);
        void setSynonym(LinearSystem& system, SolverVariable* synonymVariable, float value);
        void reset();
        const std::string& getName() const { return mName; }
        void setName(std::string name) { mName = std::move(name); }
        void setType(Type type, const char* /*prefix*/) { mType = type; }
        std::string toString() const { return mName.empty() ? std::to_string(id) : mName; }

    private:
        static inline int sUniqueErrorId = 1;
        std::string mName;
    };

    // ArrayLinkedVariables: a row's variables as a linked list in arrays, sorted by id.
    class ArrayLinkedVariables {
    public:
        static constexpr int NONE = -1;
        ArrayLinkedVariables(ArrayRow* row, Cache* cache) : mRow(row), mCache(cache) {}

        void put(SolverVariable* variable, float value);
        void add(SolverVariable* variable, float value, bool removeFromDefinition);
        float use(ArrayRow* definition, bool removeFromDefinition);
        float remove(SolverVariable* variable, bool removeFromDefinition);
        void clear();
        bool contains(SolverVariable* variable) const;
        int indexOf(SolverVariable* variable) const;
        bool hasAtLeastOnePositiveVariable() const;
        void invert();
        void divideByAmount(float amount);
        int getHead() const { return mHead; }
        int getCurrentSize() const { return mCurrentSize; }
        int getId(int index) const { return mArrayIndices[std::size_t(index)]; }
        float getValue(int index) const { return mArrayValues[std::size_t(index)]; }
        int getNextIndice(int index) const { return mArrayNextIndices[std::size_t(index)]; }
        SolverVariable* getPivotCandidate() const;
        SolverVariable* getVariable(int index) const;
        float getVariableValue(int index) const;
        float get(SolverVariable* v) const;
        int sizeInBytes() const { return 3 * (int(mArrayIndices.size()) * 4) + 9 * 4; }
        std::string toString() const;

        int mCurrentSize = 0;

    private:
        void grow(int& availableIndice);
        ArrayRow* mRow;
        Cache* mCache;
        int mRowSize = 8;
        SolverVariable* mCandidate = nullptr;
        // Java's new int[n]: zeros, not NONE.
        std::vector<int> mArrayIndices = std::vector<int>(8, 0);
        std::vector<int> mArrayNextIndices = std::vector<int>(8, 0);
        std::vector<float> mArrayValues = std::vector<float>(8, 0.0f);
        int mHead = NONE;
        int mLast = NONE;
        bool mDidFillOnce = false;
        static constexpr float sEpsilon = 0.001f;
    };

    // ArrayRow (and LinearSystem.Row): an equation, `mVariable = mConstantValue + variables`.
    class ArrayRow {
    public:
        explicit ArrayRow(Cache* cache) : variables(this, cache) {}
        virtual ~ArrayRow() = default;
        ArrayRow(const ArrayRow&) = delete;
        ArrayRow& operator=(const ArrayRow&) = delete;

        SolverVariable* mVariable = nullptr;
        float mConstantValue = 0;
        bool mUsed = false;
        std::vector<SolverVariable*> mVariablesToUpdate;
        ArrayLinkedVariables variables;
        bool mIsSimpleDefinition = false;

        bool hasKeyVariable() const {
            return !(mVariable == nullptr || (mVariable->mType != SolverVariable::Type::UNRESTRICTED && mConstantValue < 0));
        }
        std::string toReadableString() const;
        void reset();
        bool hasVariable(SolverVariable* v) const { return variables.contains(v); }
        ArrayRow* createRowDefinition(SolverVariable* variable, int value);
        ArrayRow* createRowEquals(SolverVariable* variable, int value);
        ArrayRow* createRowEquals(SolverVariable* variableA, SolverVariable* variableB, int margin);
        ArrayRow* addSingleError(SolverVariable* error, int sign);
        ArrayRow* createRowGreaterThan(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* slack, int margin);
        ArrayRow* createRowGreaterThan(SolverVariable* a, int b, SolverVariable* slack);
        ArrayRow* createRowLowerThan(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* slack, int margin);
        ArrayRow* createRowEqualMatchDimensions(float currentWeight, float totalWeights, float nextWeight, SolverVariable* variableStartA,
                                                SolverVariable* variableEndA, SolverVariable* variableStartB, SolverVariable* variableEndB);
        ArrayRow* createRowEqualDimension(float currentWeight, float totalWeights, float nextWeight, SolverVariable* variableStartA,
                                          int marginStartA, SolverVariable* variableEndA, int marginEndA, SolverVariable* variableStartB,
                                          int marginStartB, SolverVariable* variableEndB, int marginEndB);
        ArrayRow* createRowCentering(SolverVariable* variableA, SolverVariable* variableB, int marginA, float bias, SolverVariable* variableC,
                                     SolverVariable* variableD, int marginB);
        ArrayRow* addError(LinearSystem& system, int strength);
        ArrayRow* createRowDimensionPercent(SolverVariable* variableA, SolverVariable* variableC, float percent);
        ArrayRow* createRowDimensionRatio(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* variableC,
                                          SolverVariable* variableD, float ratio);
        ArrayRow* createRowWithAngle(SolverVariable* at, SolverVariable* ab, SolverVariable* bt, SolverVariable* bb, float angleComponent);
        int sizeInBytes() const;
        void ensurePositiveConstant();
        bool chooseSubject(LinearSystem& system);
        SolverVariable* chooseSubjectInVariables(LinearSystem& system);
        void pivot(SolverVariable* v);
        SolverVariable* pickPivot(SolverVariable* exclude);
        void updateFromSynonymVariable(LinearSystem& system, SolverVariable* variable, bool removeFromDefinition);

        // LinearSystem.Row.
        virtual SolverVariable* getPivotCandidate(LinearSystem& system, const std::vector<bool>* avoid);
        virtual void clear();
        virtual void initFromRow(const ArrayRow& row);
        virtual void addError(SolverVariable* error);
        virtual void updateFromSystem(LinearSystem& system);
        virtual SolverVariable* getKey() const { return mVariable; }
        virtual bool isEmpty() const { return mVariable == nullptr && mConstantValue == 0 && variables.getCurrentSize() == 0; }
        virtual void updateFromRow(LinearSystem& system, ArrayRow* definition, bool removeFromDefinition);
        virtual void updateFromFinalVariable(LinearSystem& system, SolverVariable* variable, bool removeFromDefinition);

    private:
        bool isNew(SolverVariable* variable, LinearSystem& system) const { (void)system; return variable->usageInRowCount <= 1; }
        SolverVariable* pickPivotInVariables(const std::vector<bool>* avoid, SolverVariable* exclude);
    };

    // GoalRow: error variables in it are not counted as used.
    class GoalRow : public ArrayRow {
    public:
        explicit GoalRow(Cache* cache) : ArrayRow(cache) {}
        void addError(SolverVariable* error) override {
            ArrayRow::addError(error);
            error->usageInRowCount--;
        }
    };

    // PriorityGoalRow: the goal, minimized strength by strength.
    class PriorityGoalRow : public ArrayRow {
    public:
        explicit PriorityGoalRow(Cache* cache) : ArrayRow(cache), mCache(cache) {}
        void clear() override { mNumGoals = 0; mConstantValue = 0; }
        bool isEmpty() const override { return mNumGoals == 0; }
        SolverVariable* getPivotCandidate(LinearSystem& system, const std::vector<bool>* avoid) override;
        void addError(SolverVariable* error) override;
        void updateFromRow(LinearSystem& system, ArrayRow* definition, bool removeFromDefinition) override;
        std::string toString() const;

    private:
        static constexpr float EPSILON = 0.0001f;
        static constexpr int NOT_FOUND = -1;
        // GoalVariableAccessor, over its variable.
        bool accessorAddToGoal(SolverVariable* variable, SolverVariable* other, float value);
        static bool accessorIsNegative(const SolverVariable* v);
        static bool accessorIsSmallerThan(const SolverVariable* v, const SolverVariable* other);
        void addToGoal(SolverVariable* variable);
        void removeGoal(SolverVariable* variable);
        Cache* mCache;
        std::vector<SolverVariable*> mArrayGoals = std::vector<SolverVariable*>(128);
        std::vector<SolverVariable*> mSortArray = std::vector<SolverVariable*>(128);
        int mNumGoals = 0;
    };

    // Cache: every row and variable the system made, with the pools (free lists) over them.
    class Cache {
    public:
        // Pools.SimplePool.acquire / release / releaseAll.
        ArrayRow* acquireRow();
        void releaseRow(ArrayRow* row) { mArrayRowPool.push_back(row); }
        SolverVariable* acquireVariable();
        void releaseVariables(const std::vector<SolverVariable*>& variables, int count);
        ArrayRow* newRow();
        SolverVariable* newVariable(SolverVariable::Type type);

        std::vector<SolverVariable*> mIndexedVariables = std::vector<SolverVariable*>(32);

    private:
        std::vector<std::unique_ptr<ArrayRow>> mRows;
        std::vector<std::unique_ptr<SolverVariable>> mVariables;
        std::vector<ArrayRow*> mArrayRowPool;
        std::vector<SolverVariable*> mSolverVariablePool;
    };

    class LinearSystem {
    public:
        static inline bool USE_DEPENDENCY_ORDERING = false;
        static constexpr bool USE_BASIC_SYNONYMS = true;
        static constexpr bool SIMPLIFY_SYNONYMS = true;
        static constexpr bool USE_SYNONYMS = true;
        static constexpr bool SKIP_COLUMNS = true;

        LinearSystem();
        LinearSystem(const LinearSystem&) = delete;
        LinearSystem& operator=(const LinearSystem&) = delete;

        bool hasSimpleDefinition = false;
        bool graphOptimizer = false;
        bool newgraphOptimizer = false;

        void reset();
        SolverVariable* createObjectVariable(ConstraintAnchor* anchor);
        ArrayRow* createRow();
        SolverVariable* createSlackVariable();
        SolverVariable* createExtraVariable();
        void addSingleError(ArrayRow* row, int sign, int strength);
        SolverVariable* createErrorVariable(int strength, const char* prefix);
        ArrayRow* getGoal() { return mGoal.get(); }
        ArrayRow* getRow(int n) { return mRows[std::size_t(n)]; }
        float getValueFor(const std::string& name);
        int getObjectVariableValue(ConstraintAnchor* anchor);
        SolverVariable* getVariable(const std::string& name, SolverVariable::Type type);
        void minimize();
        void minimizeGoal(ArrayRow* goal);
        void cleanupRows();
        void addConstraint(ArrayRow* row);
        void removeRow(ArrayRow* row);
        int getNumEquations() const { return mNumRows; }
        int getNumVariables() const { return mVariablesID; }
        Cache& getCache() { return mCache; }
        void addGreaterThan(SolverVariable* a, SolverVariable* b, int margin, int strength);
        void addGreaterBarrier(SolverVariable* a, SolverVariable* b, int margin, bool hasMatchConstraintWidgets);
        void addLowerThan(SolverVariable* a, SolverVariable* b, int margin, int strength);
        void addLowerBarrier(SolverVariable* a, SolverVariable* b, int margin, bool hasMatchConstraintWidgets);
        void addCentering(SolverVariable* a, SolverVariable* b, int m1, float bias, SolverVariable* c, SolverVariable* d, int m2, int strength);
        void addRatio(SolverVariable* a, SolverVariable* b, SolverVariable* c, SolverVariable* d, float ratio, int strength);
        void addSynonym(SolverVariable* a, SolverVariable* b, int margin);
        ArrayRow* addEquality(SolverVariable* a, SolverVariable* b, int margin, int strength);
        void addEquality(SolverVariable* a, int value);
        static ArrayRow* createRowDimensionPercent(LinearSystem& linearSystem, SolverVariable* variableA, SolverVariable* variableC, float percent);
        void addCenterPoint(ConstraintWidget* widget, ConstraintWidget* target, float angle, int radius);
        void displayReadableRows() const {}

        // Package-private in upstream; ArrayRow and SolverVariable read them.
        std::vector<ArrayRow*> mRows;
        int mNumColumns = 1;
        int mNumRows = 0;
        Cache mCache;

    private:
        void increaseTableSize();
        void releaseRows();
        SolverVariable* createVariable(const std::string& name, SolverVariable::Type type);
        SolverVariable* acquireSolverVariable(SolverVariable::Type type, const char* prefix);
        void addRow(ArrayRow* row);
        int optimize(ArrayRow* goal, bool b);
        int enforceBFS(ArrayRow* goal);
        void computeValues();

        int mPoolSize = 1000;
        int mVariablesID = 0;
        std::unordered_map<std::string, SolverVariable*> mVariables;
        std::unique_ptr<PriorityGoalRow> mGoal;
        int mTableSize = 32;
        int mMaxColumns = 32;
        std::vector<bool> mAlreadyTestedCandidates = std::vector<bool>(32);
        int mMaxRows = 32;
        std::vector<SolverVariable*> mPoolVariables = std::vector<SolverVariable*>(1000);
        int mPoolVariablesCount = 0;
        std::unique_ptr<ArrayRow> mTempGoal;
    };

}
