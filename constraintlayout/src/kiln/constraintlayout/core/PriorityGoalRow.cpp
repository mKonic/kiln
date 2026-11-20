#include "kiln/constraintlayout/core/LinearSystem.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    bool PriorityGoalRow::accessorAddToGoal(SolverVariable* variable, SolverVariable* other, float value) {
        if (variable->inGoal) {
            bool empty = true;
            for (int i = 0; i < SolverVariable::MAX_STRENGTH; i++) {
                variable->mGoalStrengthVector[std::size_t(i)] += other->mGoalStrengthVector[std::size_t(i)] * value;
                const float v = variable->mGoalStrengthVector[std::size_t(i)];
                if (std::abs(v) < EPSILON) variable->mGoalStrengthVector[std::size_t(i)] = 0;
                else empty = false;
            }
            if (empty) removeGoal(variable);
        } else {
            for (int i = 0; i < SolverVariable::MAX_STRENGTH; i++) {
                const float strength = other->mGoalStrengthVector[std::size_t(i)];
                if (strength != 0) {
                    float v = value * strength;
                    if (std::abs(v) < EPSILON) v = 0;
                    variable->mGoalStrengthVector[std::size_t(i)] = v;
                } else {
                    variable->mGoalStrengthVector[std::size_t(i)] = 0;
                }
            }
            return true;
        }
        return false;
    }

    bool PriorityGoalRow::accessorIsNegative(const SolverVariable* v) {
        for (int i = SolverVariable::MAX_STRENGTH - 1; i >= 0; i--) {
            const float value = v->mGoalStrengthVector[std::size_t(i)];
            if (value > 0) return false;
            if (value < 0) return true;
        }
        return false;
    }

    bool PriorityGoalRow::accessorIsSmallerThan(const SolverVariable* v, const SolverVariable* other) {
        for (int i = SolverVariable::MAX_STRENGTH - 1; i >= 0; i--) {
            const float comparedValue = other->mGoalStrengthVector[std::size_t(i)];
            const float value = v->mGoalStrengthVector[std::size_t(i)];
            if (value == comparedValue) continue;
            return value < comparedValue;
        }
        return false;
    }

    SolverVariable* PriorityGoalRow::getPivotCandidate(LinearSystem& /*system*/, const std::vector<bool>* avoid) {
        int pivot = NOT_FOUND;
        for (int i = 0; i < mNumGoals; i++) {
            SolverVariable* variable = mArrayGoals[std::size_t(i)];
            if ((*avoid)[std::size_t(variable->id)]) continue;
            if (pivot == NOT_FOUND) {
                if (accessorIsNegative(variable)) pivot = i;
            } else if (accessorIsSmallerThan(variable, mArrayGoals[std::size_t(pivot)])) {
                pivot = i;
            }
        }
        if (pivot == NOT_FOUND) return nullptr;
        return mArrayGoals[std::size_t(pivot)];
    }

    void PriorityGoalRow::addError(SolverVariable* error) {
        error->mGoalStrengthVector.fill(0);
        error->mGoalStrengthVector[std::size_t(error->strength)] = 1;
        addToGoal(error);
    }

    void PriorityGoalRow::addToGoal(SolverVariable* variable) {
        if (mNumGoals + 1 > int(mArrayGoals.size())) {
            mArrayGoals.resize(mArrayGoals.size() * 2);
            mSortArray = mArrayGoals;
            mSortArray.resize(mArrayGoals.size() * 2);
        }
        mArrayGoals[std::size_t(mNumGoals)] = variable;
        mNumGoals++;
        // Upstream compares the variable it just added with itself, so this never sorts; kept as is.
        if (mNumGoals > 1 && mArrayGoals[std::size_t(mNumGoals - 1)]->id > variable->id) {
            for (int i = 0; i < mNumGoals; i++) mSortArray[std::size_t(i)] = mArrayGoals[std::size_t(i)];
            std::stable_sort(mSortArray.begin(), mSortArray.begin() + mNumGoals, [](SolverVariable* a, SolverVariable* b) { return a->id < b->id; });
            for (int i = 0; i < mNumGoals; i++) mArrayGoals[std::size_t(i)] = mSortArray[std::size_t(i)];
        }
        variable->inGoal = true;
        variable->addToRow(this);
    }

    void PriorityGoalRow::removeGoal(SolverVariable* variable) {
        for (int i = 0; i < mNumGoals; i++) {
            if (mArrayGoals[std::size_t(i)] == variable) {
                for (int j = i; j < mNumGoals - 1; j++) mArrayGoals[std::size_t(j)] = mArrayGoals[std::size_t(j + 1)];
                mNumGoals--;
                variable->inGoal = false;
                return;
            }
        }
    }

    void PriorityGoalRow::updateFromRow(LinearSystem& /*system*/, ArrayRow* definition, bool /*removeFromDefinition*/) {
        SolverVariable* goalVariable = definition->mVariable;
        if (goalVariable == nullptr) return;
        const ArrayLinkedVariables& rowVariables = definition->variables;
        const int currentSize = rowVariables.getCurrentSize();
        for (int i = 0; i < currentSize; i++) {
            SolverVariable* solverVariable = rowVariables.getVariable(i);
            const float value = rowVariables.getVariableValue(i);
            if (accessorAddToGoal(solverVariable, goalVariable, value)) addToGoal(solverVariable);
            mConstantValue += definition->mConstantValue * value;
        }
        removeGoal(goalVariable);
    }

    std::string PriorityGoalRow::toString() const {
        std::string result = " goal -> (" + std::to_string(mConstantValue) + ") : ";
        for (int i = 0; i < mNumGoals; i++) {
            const SolverVariable* v = mArrayGoals[std::size_t(i)];
            result += "[ ";
            for (float s : v->mGoalStrengthVector) result += std::to_string(s) + " ";
            result += "] " + v->toString() + " ";
        }
        return result;
    }

}
