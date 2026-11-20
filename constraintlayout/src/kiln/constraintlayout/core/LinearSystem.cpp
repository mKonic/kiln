#include "kiln/constraintlayout/core/LinearSystem.h"

#include <algorithm>
#include <cfloat>

namespace kiln::constraintlayout::core {

    // --- Cache ------------------------------------------------------------------------------------

    ArrayRow* Cache::newRow() {
        mRows.push_back(std::make_unique<ArrayRow>(this));
        return mRows.back().get();
    }
    SolverVariable* Cache::newVariable(SolverVariable::Type type) {
        mVariables.push_back(std::make_unique<SolverVariable>(type, nullptr));
        return mVariables.back().get();
    }
    ArrayRow* Cache::acquireRow() {
        if (mArrayRowPool.empty()) return nullptr;
        ArrayRow* r = mArrayRowPool.back();
        mArrayRowPool.pop_back();
        return r;
    }
    SolverVariable* Cache::acquireVariable() {
        if (mSolverVariablePool.empty()) return nullptr;
        SolverVariable* v = mSolverVariablePool.back();
        mSolverVariablePool.pop_back();
        return v;
    }
    void Cache::releaseVariables(const std::vector<SolverVariable*>& variables, int count) {
        if (count > int(variables.size())) count = int(variables.size());
        for (int i = 0; i < count; i++) mSolverVariablePool.push_back(variables[std::size_t(i)]);
    }

    // --- LinearSystem -----------------------------------------------------------------------------

    LinearSystem::LinearSystem() {
        mRows.assign(std::size_t(mTableSize), nullptr);
        releaseRows();
        mGoal = std::make_unique<PriorityGoalRow>(&mCache);
        mTempGoal = std::make_unique<ArrayRow>(&mCache);
    }

    void LinearSystem::increaseTableSize() {
        mTableSize *= 2;
        mRows.resize(std::size_t(mTableSize), nullptr);
        mCache.mIndexedVariables.resize(std::size_t(mTableSize), nullptr);
        mAlreadyTestedCandidates.assign(std::size_t(mTableSize), false);
        mMaxColumns = mTableSize;
        mMaxRows = mTableSize;
    }

    void LinearSystem::releaseRows() {
        for (int i = 0; i < mNumRows; i++) {
            ArrayRow* row = mRows[std::size_t(i)];
            if (row != nullptr) mCache.releaseRow(row);
            mRows[std::size_t(i)] = nullptr;
        }
    }

    void LinearSystem::reset() {
        for (SolverVariable* variable : mCache.mIndexedVariables)
            if (variable != nullptr) variable->reset();
        mCache.releaseVariables(mPoolVariables, mPoolVariablesCount);
        mPoolVariablesCount = 0;
        std::fill(mCache.mIndexedVariables.begin(), mCache.mIndexedVariables.end(), nullptr);
        mVariables.clear();
        mVariablesID = 0;
        mGoal->clear();
        mNumColumns = 1;
        for (int i = 0; i < mNumRows; i++)
            if (mRows[std::size_t(i)] != nullptr) mRows[std::size_t(i)]->mUsed = false;
        releaseRows();
        mNumRows = 0;
        // A fresh temporary goal, as upstream allocates one; nothing refers to the old one now
        // that every variable was reset.
        mTempGoal = std::make_unique<ArrayRow>(&mCache);
    }

    ArrayRow* LinearSystem::createRow() {
        ArrayRow* row = mCache.acquireRow();
        if (row == nullptr) row = mCache.newRow();
        else row->reset();
        SolverVariable::increaseErrorId();
        return row;
    }

    SolverVariable* LinearSystem::createSlackVariable() {
        if (mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        SolverVariable* variable = acquireSolverVariable(SolverVariable::Type::SLACK, nullptr);
        mVariablesID++;
        mNumColumns++;
        variable->id = mVariablesID;
        mCache.mIndexedVariables[std::size_t(mVariablesID)] = variable;
        return variable;
    }

    SolverVariable* LinearSystem::createExtraVariable() {
        if (mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        SolverVariable* variable = acquireSolverVariable(SolverVariable::Type::SLACK, nullptr);
        mVariablesID++;
        mNumColumns++;
        variable->id = mVariablesID;
        mCache.mIndexedVariables[std::size_t(mVariablesID)] = variable;
        return variable;
    }

    void LinearSystem::addSingleError(ArrayRow* row, int sign, int strength) {
        SolverVariable* error = createErrorVariable(strength, nullptr);
        row->addSingleError(error, sign);
    }

    SolverVariable* LinearSystem::createVariable(const std::string& name, SolverVariable::Type type) {
        if (mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        SolverVariable* variable = acquireSolverVariable(type, nullptr);
        variable->setName(name);
        mVariablesID++;
        mNumColumns++;
        variable->id = mVariablesID;
        mVariables[name] = variable;
        mCache.mIndexedVariables[std::size_t(mVariablesID)] = variable;
        return variable;
    }

    SolverVariable* LinearSystem::createErrorVariable(int strength, const char* prefix) {
        if (mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        SolverVariable* variable = acquireSolverVariable(SolverVariable::Type::ERROR, prefix);
        mVariablesID++;
        mNumColumns++;
        variable->id = mVariablesID;
        variable->strength = strength;
        mCache.mIndexedVariables[std::size_t(mVariablesID)] = variable;
        mGoal->addError(variable);
        return variable;
    }

    SolverVariable* LinearSystem::acquireSolverVariable(SolverVariable::Type type, const char* prefix) {
        SolverVariable* variable = mCache.acquireVariable();
        if (variable == nullptr) {
            variable = mCache.newVariable(type);
            variable->setType(type, prefix);
        } else {
            variable->reset();
            variable->setType(type, prefix);
        }
        if (mPoolVariablesCount >= mPoolSize) {
            mPoolSize *= 2;
            mPoolVariables.resize(std::size_t(mPoolSize), nullptr);
        }
        mPoolVariables[std::size_t(mPoolVariablesCount++)] = variable;
        return variable;
    }

    float LinearSystem::getValueFor(const std::string& name) {
        SolverVariable* v = getVariable(name, SolverVariable::Type::UNRESTRICTED);
        if (v == nullptr) return 0;
        return v->computedValue;
    }

    SolverVariable* LinearSystem::getVariable(const std::string& name, SolverVariable::Type type) {
        auto it = mVariables.find(name);
        if (it != mVariables.end()) return it->second;
        return createVariable(name, type);
    }

    void LinearSystem::minimize() {
        if (mGoal->isEmpty()) {
            computeValues();
            return;
        }
        if (graphOptimizer || newgraphOptimizer) {
            bool fullySolved = true;
            for (int i = 0; i < mNumRows; i++) {
                if (!mRows[std::size_t(i)]->mIsSimpleDefinition) {
                    fullySolved = false;
                    break;
                }
            }
            if (!fullySolved) minimizeGoal(mGoal.get());
            else computeValues();
        } else {
            minimizeGoal(mGoal.get());
        }
    }

    void LinearSystem::minimizeGoal(ArrayRow* goal) {
        enforceBFS(goal);
        optimize(goal, false);
        computeValues();
    }

    void LinearSystem::cleanupRows() {
        int i = 0;
        while (i < mNumRows) {
            ArrayRow* current = mRows[std::size_t(i)];
            if (current->variables.getCurrentSize() == 0) current->mIsSimpleDefinition = true;
            if (current->mIsSimpleDefinition) {
                current->mVariable->computedValue = current->mConstantValue;
                current->mVariable->removeFromRow(current);
                for (int j = i; j < mNumRows - 1; j++) mRows[std::size_t(j)] = mRows[std::size_t(j + 1)];
                mRows[std::size_t(mNumRows - 1)] = nullptr;
                mNumRows--;
                i--;
                mCache.releaseRow(current);
            }
            i++;
        }
    }

    void LinearSystem::addConstraint(ArrayRow* row) {
        if (row == nullptr) return;
        if (mNumRows + 1 >= mMaxRows || mNumColumns + 1 >= mMaxColumns) increaseTableSize();
        bool added = false;
        if (!row->mIsSimpleDefinition) {
            row->updateFromSystem(*this);
            if (row->isEmpty()) return;
            row->ensurePositiveConstant();
            if (row->chooseSubject(*this)) {
                SolverVariable* extra = createExtraVariable();
                row->mVariable = extra;
                const int numRows = mNumRows;
                addRow(row);
                if (mNumRows == numRows + 1) {
                    added = true;
                    mTempGoal->initFromRow(*row);
                    optimize(mTempGoal.get(), true);
                    if (extra->mDefinitionId == -1) {
                        if (row->mVariable == extra) {
                            SolverVariable* pivotCandidate = row->pickPivot(extra);
                            if (pivotCandidate != nullptr) row->pivot(pivotCandidate);
                        }
                        if (!row->mIsSimpleDefinition) row->mVariable->updateReferencesWithNewDefinition(*this, row);
                        mCache.releaseRow(row);
                        mNumRows--;
                    }
                }
            }
            if (!row->hasKeyVariable()) return;
        }
        if (!added) addRow(row);
    }

    void LinearSystem::addRow(ArrayRow* row) {
        if (SIMPLIFY_SYNONYMS && row->mIsSimpleDefinition) {
            row->mVariable->setFinalValue(*this, row->mConstantValue);
        } else {
            mRows[std::size_t(mNumRows)] = row;
            row->mVariable->mDefinitionId = mNumRows;
            mNumRows++;
            row->mVariable->updateReferencesWithNewDefinition(*this, row);
        }
        if (SIMPLIFY_SYNONYMS && hasSimpleDefinition) {
            for (int i = 0; i < mNumRows; i++) {
                if (mRows[std::size_t(i)] != nullptr && mRows[std::size_t(i)]->mIsSimpleDefinition) {
                    ArrayRow* removedRow = mRows[std::size_t(i)];
                    removedRow->mVariable->setFinalValue(*this, removedRow->mConstantValue);
                    mCache.releaseRow(removedRow);
                    mRows[std::size_t(i)] = nullptr;
                    int lastRow = i + 1;
                    for (int j = i + 1; j < mNumRows; j++) {
                        mRows[std::size_t(j - 1)] = mRows[std::size_t(j)];
                        if (mRows[std::size_t(j - 1)]->mVariable->mDefinitionId == j) mRows[std::size_t(j - 1)]->mVariable->mDefinitionId = j - 1;
                        lastRow = j;
                    }
                    if (lastRow < mNumRows) mRows[std::size_t(lastRow)] = nullptr;
                    mNumRows--;
                    i--;
                }
            }
            hasSimpleDefinition = false;
        }
    }

    void LinearSystem::removeRow(ArrayRow* row) {
        if (row->mIsSimpleDefinition && row->mVariable != nullptr) {
            if (row->mVariable->mDefinitionId != -1) {
                for (int i = row->mVariable->mDefinitionId; i < mNumRows - 1; i++) {
                    SolverVariable* rowVariable = mRows[std::size_t(i + 1)]->mVariable;
                    if (rowVariable->mDefinitionId == i + 1) rowVariable->mDefinitionId = i;
                    mRows[std::size_t(i)] = mRows[std::size_t(i + 1)];
                }
                mNumRows--;
            }
            if (!row->mVariable->isFinalValue) row->mVariable->setFinalValue(*this, row->mConstantValue);
            mCache.releaseRow(row);
        }
    }

    int LinearSystem::optimize(ArrayRow* goal, bool /*b*/) {
        bool done = false;
        int tries = 0;
        for (int i = 0; i < mNumColumns; i++) mAlreadyTestedCandidates[std::size_t(i)] = false;
        while (!done) {
            tries++;
            if (tries >= 2 * mNumColumns) return tries;
            if (goal->getKey() != nullptr) mAlreadyTestedCandidates[std::size_t(goal->getKey()->id)] = true;
            SolverVariable* pivotCandidate = goal->getPivotCandidate(*this, &mAlreadyTestedCandidates);
            if (pivotCandidate != nullptr) {
                if (mAlreadyTestedCandidates[std::size_t(pivotCandidate->id)]) return tries;
                mAlreadyTestedCandidates[std::size_t(pivotCandidate->id)] = true;
            }
            if (pivotCandidate != nullptr) {
                float min = FLT_MAX;
                int pivotRowIndex = -1;
                for (int i = 0; i < mNumRows; i++) {
                    ArrayRow* current = mRows[std::size_t(i)];
                    SolverVariable* variable = current->mVariable;
                    if (variable->mType == SolverVariable::Type::UNRESTRICTED) continue;
                    if (current->mIsSimpleDefinition) continue;
                    if (current->hasVariable(pivotCandidate)) {
                        const float a_j = current->variables.get(pivotCandidate);
                        if (a_j < 0) {
                            const float value = -current->mConstantValue / a_j;
                            if (value < min) {
                                min = value;
                                pivotRowIndex = i;
                            }
                        }
                    }
                }
                if (pivotRowIndex > -1) {
                    ArrayRow* pivotEquation = mRows[std::size_t(pivotRowIndex)];
                    pivotEquation->mVariable->mDefinitionId = -1;
                    pivotEquation->pivot(pivotCandidate);
                    pivotEquation->mVariable->mDefinitionId = pivotRowIndex;
                    pivotEquation->mVariable->updateReferencesWithNewDefinition(*this, pivotEquation);
                }
            } else {
                done = true;
            }
        }
        return tries;
    }

    int LinearSystem::enforceBFS(ArrayRow* /*goal*/) {
        int tries = 0;
        bool done;
        bool infeasibleSystem = false;
        for (int i = 0; i < mNumRows; i++) {
            SolverVariable* variable = mRows[std::size_t(i)]->mVariable;
            if (variable->mType == SolverVariable::Type::UNRESTRICTED) continue;
            if (mRows[std::size_t(i)]->mConstantValue < 0) {
                infeasibleSystem = true;
                break;
            }
        }
        if (infeasibleSystem) {
            done = false;
            tries = 0;
            while (!done) {
                tries++;
                float min = FLT_MAX;
                int strength = 0;
                int pivotRowIndex = -1;
                int pivotColumnIndex = -1;
                for (int i = 0; i < mNumRows; i++) {
                    ArrayRow* current = mRows[std::size_t(i)];
                    SolverVariable* variable = current->mVariable;
                    if (variable->mType == SolverVariable::Type::UNRESTRICTED) continue;
                    if (current->mIsSimpleDefinition) continue;
                    if (current->mConstantValue < 0) {
                        // SKIP_COLUMNS: only the row's own variables.
                        const int size = current->variables.getCurrentSize();
                        for (int j = 0; j < size; j++) {
                            SolverVariable* candidate = current->variables.getVariable(j);
                            const float a_j = current->variables.get(candidate);
                            if (a_j <= 0) continue;
                            for (int k = 0; k < SolverVariable::MAX_STRENGTH; k++) {
                                const float value = candidate->mStrengthVector[std::size_t(k)] / a_j;
                                if ((value < min && k == strength) || k > strength) {
                                    min = value;
                                    pivotRowIndex = i;
                                    pivotColumnIndex = candidate->id;
                                    strength = k;
                                }
                            }
                        }
                    }
                }
                if (pivotRowIndex != -1) {
                    ArrayRow* pivotEquation = mRows[std::size_t(pivotRowIndex)];
                    pivotEquation->mVariable->mDefinitionId = -1;
                    pivotEquation->pivot(mCache.mIndexedVariables[std::size_t(pivotColumnIndex)]);
                    pivotEquation->mVariable->mDefinitionId = pivotRowIndex;
                    pivotEquation->mVariable->updateReferencesWithNewDefinition(*this, pivotEquation);
                } else {
                    done = true;
                }
                if (tries > mNumColumns / 2) done = true;
            }
        }
        return tries;
    }

    void LinearSystem::computeValues() {
        for (int i = 0; i < mNumRows; i++) {
            ArrayRow* row = mRows[std::size_t(i)];
            row->mVariable->computedValue = row->mConstantValue;
        }
    }

    void LinearSystem::addGreaterThan(SolverVariable* a, SolverVariable* b, int margin, int strength) {
        ArrayRow* row = createRow();
        SolverVariable* slack = createSlackVariable();
        slack->strength = 0;
        row->createRowGreaterThan(a, b, slack, margin);
        if (strength != SolverVariable::STRENGTH_FIXED) {
            const float slackValue = row->variables.get(slack);
            addSingleError(row, JavaInt(-1 * slackValue), strength);
        }
        addConstraint(row);
    }

    void LinearSystem::addGreaterBarrier(SolverVariable* a, SolverVariable* b, int margin, bool /*hasMatchConstraintWidgets*/) {
        ArrayRow* row = createRow();
        SolverVariable* slack = createSlackVariable();
        slack->strength = 0;
        row->createRowGreaterThan(a, b, slack, margin);
        addConstraint(row);
    }

    void LinearSystem::addLowerThan(SolverVariable* a, SolverVariable* b, int margin, int strength) {
        ArrayRow* row = createRow();
        SolverVariable* slack = createSlackVariable();
        slack->strength = 0;
        row->createRowLowerThan(a, b, slack, margin);
        if (strength != SolverVariable::STRENGTH_FIXED) {
            const float slackValue = row->variables.get(slack);
            addSingleError(row, JavaInt(-1 * slackValue), strength);
        }
        addConstraint(row);
    }

    void LinearSystem::addLowerBarrier(SolverVariable* a, SolverVariable* b, int margin, bool /*hasMatchConstraintWidgets*/) {
        ArrayRow* row = createRow();
        SolverVariable* slack = createSlackVariable();
        slack->strength = 0;
        row->createRowLowerThan(a, b, slack, margin);
        addConstraint(row);
    }

    void LinearSystem::addCentering(SolverVariable* a, SolverVariable* b, int m1, float bias, SolverVariable* c, SolverVariable* d, int m2, int strength) {
        ArrayRow* row = createRow();
        row->createRowCentering(a, b, m1, bias, c, d, m2);
        if (strength != SolverVariable::STRENGTH_FIXED) row->addError(*this, strength);
        addConstraint(row);
    }

    void LinearSystem::addRatio(SolverVariable* a, SolverVariable* b, SolverVariable* c, SolverVariable* d, float ratio, int strength) {
        ArrayRow* row = createRow();
        row->createRowDimensionRatio(a, b, c, d, ratio);
        if (strength != SolverVariable::STRENGTH_FIXED) row->addError(*this, strength);
        addConstraint(row);
    }

    void LinearSystem::addSynonym(SolverVariable* a, SolverVariable* b, int margin) {
        if (a->mDefinitionId == -1 && margin == 0) {
            if (b->mIsSynonym) {
                margin += JavaInt(b->mSynonymDelta);
                b = mCache.mIndexedVariables[std::size_t(b->mSynonym)];
            }
            if (a->mIsSynonym) {
                margin -= JavaInt(a->mSynonymDelta);
                a = mCache.mIndexedVariables[std::size_t(a->mSynonym)];
            } else {
                a->setSynonym(*this, b, 0);
            }
        } else {
            addEquality(a, b, margin, SolverVariable::STRENGTH_FIXED);
        }
    }

    ArrayRow* LinearSystem::addEquality(SolverVariable* a, SolverVariable* b, int margin, int strength) {
        if (USE_BASIC_SYNONYMS && strength == SolverVariable::STRENGTH_FIXED && b->isFinalValue && a->mDefinitionId == -1) {
            a->setFinalValue(*this, b->computedValue + float(margin));
            return nullptr;
        }
        ArrayRow* row = createRow();
        row->createRowEquals(a, b, margin);
        if (strength != SolverVariable::STRENGTH_FIXED) row->addError(*this, strength);
        addConstraint(row);
        return row;
    }

    void LinearSystem::addEquality(SolverVariable* a, int value) {
        if (USE_BASIC_SYNONYMS && a->mDefinitionId == -1) {
            a->setFinalValue(*this, float(value));
            for (int i = 0; i < mVariablesID + 1; i++) {
                SolverVariable* variable = mCache.mIndexedVariables[std::size_t(i)];
                if (variable != nullptr && variable->mIsSynonym && variable->mSynonym == a->id)
                    variable->setFinalValue(*this, float(value) + variable->mSynonymDelta);
            }
            return;
        }
        const int idx = a->mDefinitionId;
        if (a->mDefinitionId != -1) {
            ArrayRow* row = mRows[std::size_t(idx)];
            if (row->mIsSimpleDefinition) {
                row->mConstantValue = float(value);
            } else {
                if (row->variables.getCurrentSize() == 0) {
                    row->mIsSimpleDefinition = true;
                    row->mConstantValue = float(value);
                } else {
                    ArrayRow* newRow = createRow();
                    newRow->createRowEquals(a, value);
                    addConstraint(newRow);
                }
            }
        } else {
            ArrayRow* row = createRow();
            row->createRowDefinition(a, value);
            addConstraint(row);
        }
    }

    ArrayRow* LinearSystem::createRowDimensionPercent(LinearSystem& linearSystem, SolverVariable* variableA, SolverVariable* variableC, float percent) {
        ArrayRow* row = linearSystem.createRow();
        return row->createRowDimensionPercent(variableA, variableC, percent);
    }

}
