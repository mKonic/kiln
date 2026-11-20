#include "kiln/constraintlayout/core/LinearSystem.h"

namespace kiln::constraintlayout::core {

    namespace {
        // Java's Float.toString for the readable rows, close enough for debugging.
        std::string FloatText(float v) {
            std::string s = std::to_string(v);
            while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
            return s;
        }
    }

    std::string ArrayRow::toReadableString() const {
        std::string s = mVariable == nullptr ? "0" : mVariable->toString();
        s += " = ";
        bool addedVariable = false;
        if (mConstantValue != 0) {
            s += FloatText(mConstantValue);
            addedVariable = true;
        }
        const int count = variables.getCurrentSize();
        for (int i = 0; i < count; i++) {
            SolverVariable* v = variables.getVariable(i);
            if (v == nullptr) continue;
            float amount = variables.getVariableValue(i);
            if (amount == 0) continue;
            const std::string name = v->toString();
            if (!addedVariable) {
                if (amount < 0) {
                    s += "- ";
                    amount *= -1;
                }
            } else {
                if (amount > 0) s += " + ";
                else {
                    s += " - ";
                    amount *= -1;
                }
            }
            if (amount == 1) s += name;
            else s += FloatText(amount) + " " + name;
            addedVariable = true;
        }
        if (!addedVariable) s += "0.0";
        return s;
    }

    void ArrayRow::reset() {
        mVariable = nullptr;
        variables.clear();
        mConstantValue = 0;
        mIsSimpleDefinition = false;
    }

    ArrayRow* ArrayRow::createRowDefinition(SolverVariable* variable, int value) {
        mVariable = variable;
        variable->computedValue = float(value);
        mConstantValue = float(value);
        mIsSimpleDefinition = true;
        return this;
    }

    ArrayRow* ArrayRow::createRowEquals(SolverVariable* variable, int value) {
        if (value < 0) {
            mConstantValue = float(-1 * value);
            variables.put(variable, 1);
        } else {
            mConstantValue = float(value);
            variables.put(variable, -1);
        }
        return this;
    }

    ArrayRow* ArrayRow::createRowEquals(SolverVariable* variableA, SolverVariable* variableB, int margin) {
        bool inverse = false;
        if (margin != 0) {
            int m = margin;
            if (m < 0) {
                m = -1 * m;
                inverse = true;
            }
            mConstantValue = float(m);
        }
        if (!inverse) {
            variables.put(variableA, -1);
            variables.put(variableB, 1);
        } else {
            variables.put(variableA, 1);
            variables.put(variableB, -1);
        }
        return this;
    }

    ArrayRow* ArrayRow::addSingleError(SolverVariable* error, int sign) {
        variables.put(error, float(sign));
        return this;
    }

    ArrayRow* ArrayRow::createRowGreaterThan(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* slack, int margin) {
        bool inverse = false;
        if (margin != 0) {
            int m = margin;
            if (m < 0) {
                m = -1 * m;
                inverse = true;
            }
            mConstantValue = float(m);
        }
        if (!inverse) {
            variables.put(variableA, -1);
            variables.put(variableB, 1);
            variables.put(slack, 1);
        } else {
            variables.put(variableA, 1);
            variables.put(variableB, -1);
            variables.put(slack, -1);
        }
        return this;
    }

    ArrayRow* ArrayRow::createRowGreaterThan(SolverVariable* a, int b, SolverVariable* /*slack*/) {
        mConstantValue = float(b);
        variables.put(a, -1);
        return this;
    }

    ArrayRow* ArrayRow::createRowLowerThan(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* slack, int margin) {
        bool inverse = false;
        if (margin != 0) {
            int m = margin;
            if (m < 0) {
                m = -1 * m;
                inverse = true;
            }
            mConstantValue = float(m);
        }
        if (!inverse) {
            variables.put(variableA, -1);
            variables.put(variableB, 1);
            variables.put(slack, -1);
        } else {
            variables.put(variableA, 1);
            variables.put(variableB, -1);
            variables.put(slack, 1);
        }
        return this;
    }

    ArrayRow* ArrayRow::createRowEqualMatchDimensions(float currentWeight, float totalWeights, float nextWeight, SolverVariable* variableStartA,
                                                      SolverVariable* variableEndA, SolverVariable* variableStartB, SolverVariable* variableEndB) {
        mConstantValue = 0;
        if (totalWeights == 0 || (currentWeight == nextWeight)) {
            variables.put(variableStartA, 1);
            variables.put(variableEndA, -1);
            variables.put(variableEndB, 1);
            variables.put(variableStartB, -1);
        } else {
            if (currentWeight == 0) {
                variables.put(variableStartA, 1);
                variables.put(variableEndA, -1);
            } else if (nextWeight == 0) {
                variables.put(variableStartB, 1);
                variables.put(variableEndB, -1);
            } else {
                const float cw = currentWeight / totalWeights;
                const float nw = nextWeight / totalWeights;
                const float w = cw / nw;
                variables.put(variableStartA, 1);
                variables.put(variableEndA, -1);
                variables.put(variableEndB, w);
                variables.put(variableStartB, -w);
            }
        }
        return this;
    }

    ArrayRow* ArrayRow::createRowEqualDimension(float currentWeight, float totalWeights, float nextWeight, SolverVariable* variableStartA,
                                                int marginStartA, SolverVariable* variableEndA, int marginEndA, SolverVariable* variableStartB,
                                                int marginStartB, SolverVariable* variableEndB, int marginEndB) {
        if (totalWeights == 0 || (currentWeight == nextWeight)) {
            mConstantValue = float(-marginStartA - marginEndA + marginStartB + marginEndB);
            variables.put(variableStartA, 1);
            variables.put(variableEndA, -1);
            variables.put(variableEndB, 1);
            variables.put(variableStartB, -1);
        } else {
            const float cw = currentWeight / totalWeights;
            const float nw = nextWeight / totalWeights;
            const float w = cw / nw;
            mConstantValue = float(-marginStartA - marginEndA) + w * float(marginStartB) + w * float(marginEndB);
            variables.put(variableStartA, 1);
            variables.put(variableEndA, -1);
            variables.put(variableEndB, w);
            variables.put(variableStartB, -w);
        }
        return this;
    }

    ArrayRow* ArrayRow::createRowCentering(SolverVariable* variableA, SolverVariable* variableB, int marginA, float bias, SolverVariable* variableC,
                                           SolverVariable* variableD, int marginB) {
        if (variableB == variableC) {
            variables.put(variableA, 1);
            variables.put(variableD, 1);
            variables.put(variableB, -2);
            return this;
        }
        if (bias == 0.5f) {
            variables.put(variableA, 1.0f);
            variables.put(variableB, -1.0f);
            variables.put(variableC, -1.0f);
            variables.put(variableD, 1.0f);
            if (marginA > 0 || marginB > 0) mConstantValue = float(-marginA + marginB);
        } else if (bias <= 0) {
            variables.put(variableA, -1);
            variables.put(variableB, 1);
            mConstantValue = float(marginA);
        } else if (bias >= 1) {
            variables.put(variableD, -1);
            variables.put(variableC, 1);
            mConstantValue = float(-marginB);
        } else {
            variables.put(variableA, 1 * (1 - bias));
            variables.put(variableB, -1 * (1 - bias));
            variables.put(variableC, -1 * bias);
            variables.put(variableD, 1 * bias);
            // Java: -marginA * (1 - bias) + marginB * bias, int promoted to float per term.
            if (marginA > 0 || marginB > 0) mConstantValue = float(-marginA) * (1 - bias) + float(marginB) * bias;
        }
        return this;
    }

    ArrayRow* ArrayRow::addError(LinearSystem& system, int strength) {
        variables.put(system.createErrorVariable(strength, "ep"), 1);
        variables.put(system.createErrorVariable(strength, "em"), -1);
        return this;
    }

    ArrayRow* ArrayRow::createRowDimensionPercent(SolverVariable* variableA, SolverVariable* variableC, float percent) {
        variables.put(variableA, -1);
        variables.put(variableC, percent);
        return this;
    }

    ArrayRow* ArrayRow::createRowDimensionRatio(SolverVariable* variableA, SolverVariable* variableB, SolverVariable* variableC,
                                                SolverVariable* variableD, float ratio) {
        variables.put(variableA, -1);
        variables.put(variableB, 1);
        variables.put(variableC, ratio);
        variables.put(variableD, -ratio);
        return this;
    }

    ArrayRow* ArrayRow::createRowWithAngle(SolverVariable* at, SolverVariable* ab, SolverVariable* bt, SolverVariable* bb, float angleComponent) {
        variables.put(bt, 0.5f);
        variables.put(bb, 0.5f);
        variables.put(at, -0.5f);
        variables.put(ab, -0.5f);
        mConstantValue = -angleComponent;
        return this;
    }

    int ArrayRow::sizeInBytes() const {
        int size = 0;
        if (mVariable != nullptr) size += 4;
        size += 4;
        size += 4;
        size += variables.sizeInBytes();
        return size;
    }

    void ArrayRow::ensurePositiveConstant() {
        if (mConstantValue < 0) {
            mConstantValue *= -1;
            variables.invert();
        }
    }

    bool ArrayRow::chooseSubject(LinearSystem& system) {
        bool addedExtra = false;
        SolverVariable* pivotCandidate = chooseSubjectInVariables(system);
        if (pivotCandidate == nullptr) addedExtra = true;
        else pivot(pivotCandidate);
        if (variables.getCurrentSize() == 0) mIsSimpleDefinition = true;
        return addedExtra;
    }

    SolverVariable* ArrayRow::chooseSubjectInVariables(LinearSystem& system) {
        SolverVariable* restrictedCandidate = nullptr;
        SolverVariable* unrestrictedCandidate = nullptr;
        float unrestrictedCandidateAmount = 0;
        float restrictedCandidateAmount = 0;
        bool unrestrictedCandidateIsNew = false;
        bool restrictedCandidateIsNew = false;
        const int currentSize = variables.getCurrentSize();
        for (int i = 0; i < currentSize; i++) {
            const float amount = variables.getVariableValue(i);
            SolverVariable* variable = variables.getVariable(i);
            if (variable->mType == SolverVariable::Type::UNRESTRICTED) {
                if (unrestrictedCandidate == nullptr) {
                    unrestrictedCandidate = variable;
                    unrestrictedCandidateAmount = amount;
                    unrestrictedCandidateIsNew = isNew(variable, system);
                } else if (unrestrictedCandidateAmount > amount) {
                    unrestrictedCandidate = variable;
                    unrestrictedCandidateAmount = amount;
                    unrestrictedCandidateIsNew = isNew(variable, system);
                } else if (!unrestrictedCandidateIsNew && isNew(variable, system)) {
                    unrestrictedCandidate = variable;
                    unrestrictedCandidateAmount = amount;
                    unrestrictedCandidateIsNew = true;
                }
            } else if (unrestrictedCandidate == nullptr) {
                if (amount < 0) {
                    if (restrictedCandidate == nullptr) {
                        restrictedCandidate = variable;
                        restrictedCandidateAmount = amount;
                        restrictedCandidateIsNew = isNew(variable, system);
                    } else if (restrictedCandidateAmount > amount) {
                        restrictedCandidate = variable;
                        restrictedCandidateAmount = amount;
                        restrictedCandidateIsNew = isNew(variable, system);
                    } else if (!restrictedCandidateIsNew && isNew(variable, system)) {
                        restrictedCandidate = variable;
                        restrictedCandidateAmount = amount;
                        restrictedCandidateIsNew = true;
                    }
                }
            }
        }
        if (unrestrictedCandidate != nullptr) return unrestrictedCandidate;
        return restrictedCandidate;
    }

    void ArrayRow::pivot(SolverVariable* v) {
        if (mVariable != nullptr) {
            variables.put(mVariable, -1.0f);
            mVariable->mDefinitionId = -1;
            mVariable = nullptr;
        }
        const float amount = variables.remove(v, true) * -1;
        mVariable = v;
        if (amount == 1) return;
        mConstantValue = mConstantValue / amount;
        variables.divideByAmount(amount);
    }

    void ArrayRow::updateFromRow(LinearSystem& system, ArrayRow* definition, bool removeFromDefinition) {
        const float value = variables.use(definition, removeFromDefinition);
        mConstantValue += definition->mConstantValue * value;
        if (removeFromDefinition) definition->mVariable->removeFromRow(this);
        if (LinearSystem::SIMPLIFY_SYNONYMS && mVariable != nullptr && variables.getCurrentSize() == 0) {
            mIsSimpleDefinition = true;
            system.hasSimpleDefinition = true;
        }
    }

    void ArrayRow::updateFromFinalVariable(LinearSystem& system, SolverVariable* variable, bool removeFromDefinition) {
        if (variable == nullptr || !variable->isFinalValue) return;
        const float value = variables.get(variable);
        mConstantValue += variable->computedValue * value;
        variables.remove(variable, removeFromDefinition);
        if (removeFromDefinition) variable->removeFromRow(this);
        if (LinearSystem::SIMPLIFY_SYNONYMS && variables.getCurrentSize() == 0) {
            mIsSimpleDefinition = true;
            system.hasSimpleDefinition = true;
        }
    }

    void ArrayRow::updateFromSynonymVariable(LinearSystem& system, SolverVariable* variable, bool removeFromDefinition) {
        if (variable == nullptr || !variable->mIsSynonym) return;
        const float value = variables.get(variable);
        mConstantValue += variable->mSynonymDelta * value;
        variables.remove(variable, removeFromDefinition);
        if (removeFromDefinition) variable->removeFromRow(this);
        variables.add(system.mCache.mIndexedVariables[std::size_t(variable->mSynonym)], value, removeFromDefinition);
        if (LinearSystem::SIMPLIFY_SYNONYMS && variables.getCurrentSize() == 0) {
            mIsSimpleDefinition = true;
            system.hasSimpleDefinition = true;
        }
    }

    SolverVariable* ArrayRow::pickPivotInVariables(const std::vector<bool>* avoid, SolverVariable* exclude) {
        // Upstream's `all` is always true: the most negative slack or error variable.
        float value = 0;
        SolverVariable* pivot = nullptr;
        const int currentSize = variables.getCurrentSize();
        for (int i = 0; i < currentSize; i++) {
            const float currentValue = variables.getVariableValue(i);
            if (currentValue < 0) {
                SolverVariable* v = variables.getVariable(i);
                if (!((avoid != nullptr && (*avoid)[std::size_t(v->id)]) || (v == exclude))) {
                    if (v->mType == SolverVariable::Type::SLACK || v->mType == SolverVariable::Type::ERROR) {
                        if (currentValue < value) {
                            value = currentValue;
                            pivot = v;
                        }
                    }
                }
            }
        }
        return pivot;
    }

    SolverVariable* ArrayRow::pickPivot(SolverVariable* exclude) { return pickPivotInVariables(nullptr, exclude); }

    SolverVariable* ArrayRow::getPivotCandidate(LinearSystem& /*system*/, const std::vector<bool>* avoid) {
        return pickPivotInVariables(avoid, nullptr);
    }

    void ArrayRow::clear() {
        variables.clear();
        mVariable = nullptr;
        mConstantValue = 0;
    }

    void ArrayRow::initFromRow(const ArrayRow& copiedRow) {
        mVariable = nullptr;
        variables.clear();
        for (int i = 0; i < copiedRow.variables.getCurrentSize(); i++) {
            SolverVariable* var = copiedRow.variables.getVariable(i);
            const float val = copiedRow.variables.getVariableValue(i);
            variables.add(var, val, true);
        }
    }

    void ArrayRow::addError(SolverVariable* error) {
        float weight = 1;
        if (error->strength == SolverVariable::STRENGTH_LOW) weight = 1.0f;
        else if (error->strength == SolverVariable::STRENGTH_MEDIUM) weight = 1E3F;
        else if (error->strength == SolverVariable::STRENGTH_HIGH) weight = 1E6F;
        else if (error->strength == SolverVariable::STRENGTH_HIGHEST) weight = 1E9F;
        else if (error->strength == SolverVariable::STRENGTH_EQUALITY) weight = 1E12F;
        variables.put(error, weight);
    }

    void ArrayRow::updateFromSystem(LinearSystem& system) {
        if (system.mRows.empty()) return;
        bool done = false;
        while (!done) {
            const int currentSize = variables.getCurrentSize();
            for (int i = 0; i < currentSize; i++) {
                SolverVariable* variable = variables.getVariable(i);
                if (variable->mDefinitionId != -1 || variable->isFinalValue || variable->mIsSynonym) mVariablesToUpdate.push_back(variable);
            }
            const std::size_t size = mVariablesToUpdate.size();
            if (size > 0) {
                for (std::size_t i = 0; i < size; i++) {
                    SolverVariable* variable = mVariablesToUpdate[i];
                    if (variable->isFinalValue) updateFromFinalVariable(system, variable, true);
                    else if (variable->mIsSynonym) updateFromSynonymVariable(system, variable, true);
                    else updateFromRow(system, system.mRows[std::size_t(variable->mDefinitionId)], true);
                }
                mVariablesToUpdate.clear();
            } else {
                done = true;
            }
        }
        if (LinearSystem::SIMPLIFY_SYNONYMS && mVariable != nullptr && variables.getCurrentSize() == 0) {
            mIsSimpleDefinition = true;
            system.hasSimpleDefinition = true;
        }
    }

}
