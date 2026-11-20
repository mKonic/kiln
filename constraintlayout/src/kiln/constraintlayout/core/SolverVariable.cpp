#include "kiln/constraintlayout/core/LinearSystem.h"

namespace kiln::constraintlayout::core {

    void SolverVariable::addToRow(ArrayRow* row) {
        for (int i = 0; i < mClientEquationsCount; i++)
            if (mClientEquations[std::size_t(i)] == row) return;
        if (mClientEquationsCount >= int(mClientEquations.size())) mClientEquations.resize(mClientEquations.size() * 2);
        mClientEquations[std::size_t(mClientEquationsCount)] = row;
        mClientEquationsCount++;
    }

    void SolverVariable::removeFromRow(ArrayRow* row) {
        const int count = mClientEquationsCount;
        for (int i = 0; i < count; i++) {
            if (mClientEquations[std::size_t(i)] == row) {
                for (int j = i; j < count - 1; j++) mClientEquations[std::size_t(j)] = mClientEquations[std::size_t(j + 1)];
                mClientEquationsCount--;
                return;
            }
        }
    }

    void SolverVariable::updateReferencesWithNewDefinition(LinearSystem& system, ArrayRow* definition) {
        const int count = mClientEquationsCount;
        for (int i = 0; i < count; i++) mClientEquations[std::size_t(i)]->updateFromRow(system, definition, false);
        mClientEquationsCount = 0;
    }

    void SolverVariable::setFinalValue(LinearSystem& system, float value) {
        computedValue = value;
        isFinalValue = true;
        mIsSynonym = false;
        mSynonym = -1;
        mSynonymDelta = 0;
        const int count = mClientEquationsCount;
        mDefinitionId = -1;
        for (int i = 0; i < count; i++) mClientEquations[std::size_t(i)]->updateFromFinalVariable(system, this, false);
        mClientEquationsCount = 0;
    }

    void SolverVariable::setSynonym(LinearSystem& system, SolverVariable* synonymVariable, float value) {
        mIsSynonym = true;
        mSynonym = synonymVariable->id;
        mSynonymDelta = value;
        const int count = mClientEquationsCount;
        mDefinitionId = -1;
        for (int i = 0; i < count; i++) mClientEquations[std::size_t(i)]->updateFromSynonymVariable(system, this, false);
        mClientEquationsCount = 0;
        system.displayReadableRows();
    }

    void SolverVariable::reset() {
        mName.clear();
        mType = Type::UNKNOWN;
        strength = STRENGTH_NONE;
        id = -1;
        mDefinitionId = -1;
        computedValue = 0;
        isFinalValue = false;
        mIsSynonym = false;
        mSynonym = -1;
        mSynonymDelta = 0;
        const int count = mClientEquationsCount;
        for (int i = 0; i < count; i++) mClientEquations[std::size_t(i)] = nullptr;
        mClientEquationsCount = 0;
        usageInRowCount = 0;
        inGoal = false;
        mGoalStrengthVector.fill(0);
    }

}
