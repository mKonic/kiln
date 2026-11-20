#include "kiln/constraintlayout/core/LinearSystem.h"

namespace kiln::constraintlayout::core {

    // The slot for a new entry, growing the arrays (Arrays.copyOf pads with zeros) when full.
    void ArrayLinkedVariables::grow(int& availableIndice) {
        const int length = int(mArrayIndices.size());
        if (availableIndice >= length) {
            if (mCurrentSize < length) {
                for (int i = 0; i < length; i++) {
                    if (mArrayIndices[std::size_t(i)] == NONE) {
                        availableIndice = i;
                        break;
                    }
                }
            }
        }
        if (availableIndice >= length) {
            availableIndice = length;
            mRowSize *= 2;
            mDidFillOnce = false;
            mLast = availableIndice - 1;
            mArrayValues.resize(std::size_t(mRowSize), 0.0f);
            mArrayIndices.resize(std::size_t(mRowSize), 0);
            mArrayNextIndices.resize(std::size_t(mRowSize), 0);
        }
    }

    void ArrayLinkedVariables::put(SolverVariable* variable, float value) {
        if (value == 0) {
            remove(variable, true);
            return;
        }
        if (mHead == NONE) {
            mHead = 0;
            mArrayValues[std::size_t(mHead)] = value;
            mArrayIndices[std::size_t(mHead)] = variable->id;
            mArrayNextIndices[std::size_t(mHead)] = NONE;
            variable->usageInRowCount++;
            variable->addToRow(mRow);
            mCurrentSize++;
            if (!mDidFillOnce) {
                mLast++;
                if (mLast >= int(mArrayIndices.size())) {
                    mDidFillOnce = true;
                    mLast = int(mArrayIndices.size()) - 1;
                }
            }
            return;
        }
        int current = mHead;
        int previous = NONE;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (mArrayIndices[std::size_t(current)] == variable->id) {
                mArrayValues[std::size_t(current)] = value;
                return;
            }
            if (mArrayIndices[std::size_t(current)] < variable->id) previous = current;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        int availableIndice = mLast + 1;
        if (mDidFillOnce) {
            if (mArrayIndices[std::size_t(mLast)] == NONE) availableIndice = mLast;
            else availableIndice = int(mArrayIndices.size());
        }
        grow(availableIndice);
        mArrayIndices[std::size_t(availableIndice)] = variable->id;
        mArrayValues[std::size_t(availableIndice)] = value;
        if (previous != NONE) {
            mArrayNextIndices[std::size_t(availableIndice)] = mArrayNextIndices[std::size_t(previous)];
            mArrayNextIndices[std::size_t(previous)] = availableIndice;
        } else {
            mArrayNextIndices[std::size_t(availableIndice)] = mHead;
            mHead = availableIndice;
        }
        variable->usageInRowCount++;
        variable->addToRow(mRow);
        mCurrentSize++;
        if (!mDidFillOnce) mLast++;
        if (mCurrentSize >= int(mArrayIndices.size())) mDidFillOnce = true;
        if (mLast >= int(mArrayIndices.size())) {
            mDidFillOnce = true;
            mLast = int(mArrayIndices.size()) - 1;
        }
    }

    void ArrayLinkedVariables::add(SolverVariable* variable, float value, bool removeFromDefinition) {
        if (value > -sEpsilon && value < sEpsilon) return;
        if (mHead == NONE) {
            mHead = 0;
            mArrayValues[std::size_t(mHead)] = value;
            mArrayIndices[std::size_t(mHead)] = variable->id;
            mArrayNextIndices[std::size_t(mHead)] = NONE;
            variable->usageInRowCount++;
            variable->addToRow(mRow);
            mCurrentSize++;
            if (!mDidFillOnce) {
                mLast++;
                if (mLast >= int(mArrayIndices.size())) {
                    mDidFillOnce = true;
                    mLast = int(mArrayIndices.size()) - 1;
                }
            }
            return;
        }
        int current = mHead;
        int previous = NONE;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            const int idx = mArrayIndices[std::size_t(current)];
            if (idx == variable->id) {
                float v = mArrayValues[std::size_t(current)] + value;
                if (v > -sEpsilon && v < sEpsilon) v = 0;
                mArrayValues[std::size_t(current)] = v;
                if (v == 0) {
                    if (current == mHead) mHead = mArrayNextIndices[std::size_t(current)];
                    else mArrayNextIndices[std::size_t(previous)] = mArrayNextIndices[std::size_t(current)];
                    if (removeFromDefinition) variable->removeFromRow(mRow);
                    if (mDidFillOnce) mLast = current;
                    variable->usageInRowCount--;
                    mCurrentSize--;
                }
                return;
            }
            if (mArrayIndices[std::size_t(current)] < variable->id) previous = current;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        int availableIndice = mLast + 1;
        if (mDidFillOnce) {
            if (mArrayIndices[std::size_t(mLast)] == NONE) availableIndice = mLast;
            else availableIndice = int(mArrayIndices.size());
        }
        grow(availableIndice);
        mArrayIndices[std::size_t(availableIndice)] = variable->id;
        mArrayValues[std::size_t(availableIndice)] = value;
        if (previous != NONE) {
            mArrayNextIndices[std::size_t(availableIndice)] = mArrayNextIndices[std::size_t(previous)];
            mArrayNextIndices[std::size_t(previous)] = availableIndice;
        } else {
            mArrayNextIndices[std::size_t(availableIndice)] = mHead;
            mHead = availableIndice;
        }
        variable->usageInRowCount++;
        variable->addToRow(mRow);
        mCurrentSize++;
        if (!mDidFillOnce) mLast++;
        if (mLast >= int(mArrayIndices.size())) {
            mDidFillOnce = true;
            mLast = int(mArrayIndices.size()) - 1;
        }
    }

    float ArrayLinkedVariables::use(ArrayRow* definition, bool removeFromDefinition) {
        const float value = get(definition->mVariable);
        remove(definition->mVariable, removeFromDefinition);
        const ArrayLinkedVariables& definitionVariables = definition->variables;
        const int definitionSize = definitionVariables.getCurrentSize();
        for (int i = 0; i < definitionSize; i++) {
            SolverVariable* definitionVariable = definitionVariables.getVariable(i);
            const float definitionValue = definitionVariables.get(definitionVariable);
            add(definitionVariable, definitionValue * value, removeFromDefinition);
        }
        return value;
    }

    float ArrayLinkedVariables::remove(SolverVariable* variable, bool removeFromDefinition) {
        if (mCandidate == variable) mCandidate = nullptr;
        if (mHead == NONE) return 0;
        int current = mHead;
        int previous = NONE;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            const int idx = mArrayIndices[std::size_t(current)];
            if (idx == variable->id) {
                if (current == mHead) mHead = mArrayNextIndices[std::size_t(current)];
                else mArrayNextIndices[std::size_t(previous)] = mArrayNextIndices[std::size_t(current)];
                if (removeFromDefinition) variable->removeFromRow(mRow);
                variable->usageInRowCount--;
                mCurrentSize--;
                mArrayIndices[std::size_t(current)] = NONE;
                if (mDidFillOnce) mLast = current;
                return mArrayValues[std::size_t(current)];
            }
            previous = current;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return 0;
    }

    void ArrayLinkedVariables::clear() {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            SolverVariable* variable = mCache->mIndexedVariables[std::size_t(mArrayIndices[std::size_t(current)])];
            if (variable != nullptr) variable->removeFromRow(mRow);
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        mHead = NONE;
        mLast = NONE;
        mDidFillOnce = false;
        mCurrentSize = 0;
    }

    bool ArrayLinkedVariables::contains(SolverVariable* variable) const { return indexOf(variable) != -1; }

    int ArrayLinkedVariables::indexOf(SolverVariable* variable) const {
        if (mHead == NONE) return -1;
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (mArrayIndices[std::size_t(current)] == variable->id) return current;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return -1;
    }

    bool ArrayLinkedVariables::hasAtLeastOnePositiveVariable() const {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (mArrayValues[std::size_t(current)] > 0) return true;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return false;
    }

    void ArrayLinkedVariables::invert() {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            mArrayValues[std::size_t(current)] *= -1;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
    }

    void ArrayLinkedVariables::divideByAmount(float amount) {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            mArrayValues[std::size_t(current)] /= amount;
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
    }

    SolverVariable* ArrayLinkedVariables::getPivotCandidate() const {
        if (mCandidate == nullptr) {
            int current = mHead;
            int counter = 0;
            SolverVariable* pivot = nullptr;
            while (current != NONE && counter < mCurrentSize) {
                if (mArrayValues[std::size_t(current)] < 0) {
                    SolverVariable* v = mCache->mIndexedVariables[std::size_t(mArrayIndices[std::size_t(current)])];
                    if (pivot == nullptr || pivot->strength < v->strength) pivot = v;
                }
                current = mArrayNextIndices[std::size_t(current)];
                counter++;
            }
            return pivot;
        }
        return mCandidate;
    }

    SolverVariable* ArrayLinkedVariables::getVariable(int index) const {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (counter == index) return mCache->mIndexedVariables[std::size_t(mArrayIndices[std::size_t(current)])];
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return nullptr;
    }

    float ArrayLinkedVariables::getVariableValue(int index) const {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (counter == index) return mArrayValues[std::size_t(current)];
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return 0;
    }

    float ArrayLinkedVariables::get(SolverVariable* v) const {
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            if (mArrayIndices[std::size_t(current)] == v->id) return mArrayValues[std::size_t(current)];
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return 0;
    }

    std::string ArrayLinkedVariables::toString() const {
        std::string result;
        int current = mHead;
        int counter = 0;
        while (current != NONE && counter < mCurrentSize) {
            result += " -> " + std::to_string(mArrayValues[std::size_t(current)]) + " : ";
            if (SolverVariable* v = mCache->mIndexedVariables[std::size_t(mArrayIndices[std::size_t(current)])]) result += v->toString();
            current = mArrayNextIndices[std::size_t(current)];
            counter++;
        }
        return result;
    }

}
