
/*
 * block2: Efficient MPO implementation of quantum chemistry DMRG
 * Copyright (C) 2020 Huanchen Zhai <hczhai@caltech.edu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "integral.hpp"
#include "parallel_rule.hpp"
#include "rule.hpp"
#include <memory>

using namespace std;

namespace block2 {

// Rule for parallel dispatcher for quantum chemistry sum MPO
template <typename S> struct ParallelRuleSumMPO : ParallelRule<S> {
    using ParallelRule<S>::comm;
    uint16_t n_sites;
    ParallelRuleSumMPO(const shared_ptr<ParallelCommunicator<S>> &comm)
        : ParallelRule<S>(comm) {}
    ParallelProperty
    operator()(const shared_ptr<OpElement<S>> &op) const override {
        return ParallelProperty(comm->rank, ParallelOpTypes::None);
    }
    bool index_available() const noexcept { return comm->rank == comm->root; }
    bool index_available(uint16_t i) const noexcept {
        // return comm->rank == i * comm->size / n_sites;
        return comm->rank == i % comm->size;
    }
    bool index_available(uint16_t i, uint16_t j) const noexcept {
        return index_available(i);
    }
    bool index_available(uint16_t i, uint16_t j, uint16_t k,
                         uint16_t l) const noexcept {
        return index_available(i);
    }
};

// Symmetry rules for simplifying quantum chemistry sum MPO (non-spin-adapted)
template <typename S> struct SumMPORule : Rule<S> {
    shared_ptr<Rule<S>> prim_rule;
    shared_ptr<ParallelRuleSumMPO<S>> para_rule;
    SumMPORule(const shared_ptr<Rule<S>> &rule,
               const shared_ptr<ParallelRuleSumMPO<S>> &para_rule)
        : prim_rule(rule), para_rule(para_rule) {}
    shared_ptr<OpElementRef<S>>
    operator()(const shared_ptr<OpElement<S>> &op) const override {
        if (op->site_index.size() == 1 ||
            (op->site_index.size() == 2 &&
             para_rule->index_available(op->site_index[0]) &&
             para_rule->index_available(op->site_index[1])))
            return prim_rule->operator()(op);
        else
            return nullptr;
    }
};

// One- and two-electron integrals
// distriubed over mpi procs
template <typename S> struct ParallelFCIDUMP : FCIDUMP {
    using FCIDUMP::const_e;
    using FCIDUMP::e;
    using FCIDUMP::n_sites;
    using FCIDUMP::t;
    using FCIDUMP::v;
    shared_ptr<ParallelRuleSumMPO<S>> rule;
    ParallelFCIDUMP(const shared_ptr<ParallelRuleSumMPO<S>> &rule)
        : rule(rule), FCIDUMP() {}
    // One-electron integral element (SU(2))
    double t(uint16_t i, uint16_t j) const override {
        if (rule->n_sites == 0)
            rule->n_sites = n_sites();
        return rule->index_available(i, j) ? FCIDUMP::t(i, j) : 0;
    }
    // One-electron integral element (SZ)
    double t(uint8_t s, uint16_t i, uint16_t j) const override {
        if (rule->n_sites == 0)
            rule->n_sites = n_sites();
        return rule->index_available(i, j) ? FCIDUMP::t(s, i, j) : 0;
    }
    // Two-electron integral element (SU(2))
    double v(uint16_t i, uint16_t j, uint16_t k, uint16_t l) const override {
        if (rule->n_sites == 0)
            rule->n_sites = n_sites();
        return rule->index_available(i, j, k, l) ? FCIDUMP::v(i, j, k, l) : 0;
    }
    // Two-electron integral element (SZ)
    double v(uint8_t sl, uint8_t sr, uint16_t i, uint16_t j, uint16_t k,
             uint16_t l) const override {
        if (rule->n_sites == 0)
            rule->n_sites = n_sites();
        return rule->index_available(i, j, k, l)
                   ? FCIDUMP::v(sl, sr, i, j, k, l)
                   : 0;
    }
    double e() const override { return rule->index_available() ? const_e : 0; }
};

} // namespace block2
