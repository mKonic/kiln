// The ConstraintLayout solver (constraintlayout-core LinearSystem): the row lists, and systems
// whose answers are fixed by the constraints. The widget tests check it against upstream's
// expected layouts.
#include "Check.h"

#include "kiln/constraintlayout/core/LinearSystem.h"

#include <cmath>
#include <memory>

using namespace kiln::constraintlayout::core;

// ArrayLinkedVariablesTest.testNestedLayout: adds and removes keep the list consistent.
TEST(ConstraintSolverLinkedVariablesAddAndRemove) {
    Cache cache;
    ArrayRow row(&cache);
    ArrayLinkedVariables variables(&row, &cache);
    std::vector<std::unique_ptr<SolverVariable>> v;
    for (int i = 0; i < 9; i++) {
        const int p = i ^ 3;
        v.push_back(std::make_unique<SolverVariable>("dog" + std::to_string(p) + "(" + std::to_string(i) + ")" + std::to_string(p),
                                                     SolverVariable::Type::UNRESTRICTED));
        cache.mIndexedVariables[std::size_t(i)] = v.back().get();
        v.back()->id = i;
        variables.add(v.back().get(), 20.0f, true);
        if (i % 2 == 1) variables.remove(v[std::size_t(i / 2)].get(), true);
    }
    // 0..3 were removed (1 -> v0, 3 -> v1, 5 -> v2, 7 -> v3); 4..8 remain, in id order.
    CHECK(variables.getCurrentSize() == 5);
    for (int i = 0; i < 5; i++) CHECK(variables.getVariable(i) == v[std::size_t(4 + i)].get() && variables.getVariableValue(i) == 20.0f);
    for (int i = 0; i < 9; i++)
        if (i % 2 == 1) variables.add(v[std::size_t(i / 2)].get(), 24.0f, true);
    CHECK(variables.getCurrentSize() == 9);
    for (int i = 0; i < 9; i++) CHECK(variables.getVariable(i)->id == i);
    CHECK(variables.get(v[0].get()) == 24.0f && variables.get(v[8].get()) == 20.0f);
    // Adding the opposite amount removes it.
    variables.add(v[8].get(), -20.0f, true);
    CHECK(variables.getCurrentSize() == 8 && !variables.contains(v[8].get()));
}

TEST(ConstraintSolverEqualitiesAndCentering) {
    LinearSystem s;
    SolverVariable* l = s.getVariable("l", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* r = s.getVariable("r", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* a = s.getVariable("a", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* b = s.getVariable("b", SolverVariable::Type::UNRESTRICTED);
    s.addEquality(l, 0);
    s.addEquality(r, 1000);
    // b = a + 200, then centered between l and r (begin, its target, bias, end target, end): (a - l) = (r - b).
    s.addEquality(b, a, 200, SolverVariable::STRENGTH_FIXED);
    s.addCentering(a, l, 0, 0.5f, r, b, 0, SolverVariable::STRENGTH_CENTERING);
    s.minimize();
    CHECK(s.getValueFor("l") == 0 && s.getValueFor("r") == 1000);
    CHECK(s.getValueFor("a") == 400 && s.getValueFor("b") == 600);

    // A bias of 0.25: (a - l) * 0.75 = (r - b) * 0.25.
    s.reset();
    l = s.getVariable("l", SolverVariable::Type::UNRESTRICTED);
    r = s.getVariable("r", SolverVariable::Type::UNRESTRICTED);
    a = s.getVariable("a", SolverVariable::Type::UNRESTRICTED);
    b = s.getVariable("b", SolverVariable::Type::UNRESTRICTED);
    s.addEquality(l, 0);
    s.addEquality(r, 1000);
    s.addEquality(b, a, 200, SolverVariable::STRENGTH_FIXED);
    s.addCentering(a, l, 0, 0.25f, r, b, 0, SolverVariable::STRENGTH_CENTERING);
    s.minimize();
    CHECK(std::abs(s.getValueFor("a") - 200) < 0.01f && std::abs(s.getValueFor("b") - 400) < 0.01f);
}

TEST(ConstraintSolverInequalitiesWinByStrength) {
    // x wants 500 (weak) but must stay at most 300 (fixed) and at least 100.
    LinearSystem s;
    SolverVariable* zero = s.getVariable("zero", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* max = s.getVariable("max", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* x = s.getVariable("x", SolverVariable::Type::UNRESTRICTED);
    SolverVariable* want = s.getVariable("want", SolverVariable::Type::UNRESTRICTED);
    s.addEquality(zero, 0);
    s.addEquality(max, 300);
    s.addEquality(want, 500);
    s.addLowerThan(x, max, 0, SolverVariable::STRENGTH_FIXED);
    s.addGreaterThan(x, zero, 100, SolverVariable::STRENGTH_FIXED);
    s.addEquality(x, want, 0, SolverVariable::STRENGTH_LOW);
    s.minimize();
    CHECK(s.getValueFor("x") == 300);

    // A stronger pull toward 50 loses to the fixed minimum of 100.
    s.reset();
    zero = s.getVariable("zero", SolverVariable::Type::UNRESTRICTED);
    x = s.getVariable("x", SolverVariable::Type::UNRESTRICTED);
    want = s.getVariable("want", SolverVariable::Type::UNRESTRICTED);
    s.addEquality(zero, 0);
    s.addEquality(want, 50);
    s.addGreaterThan(x, zero, 100, SolverVariable::STRENGTH_FIXED);
    s.addEquality(x, want, 0, SolverVariable::STRENGTH_HIGHEST);
    s.minimize();
    CHECK(s.getValueFor("x") == 100);
}
