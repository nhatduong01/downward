//
// Created by Duong Nguyen on 24.09.25.
//

#ifndef DOWNWARD_VALIDATOR_H
#define DOWNWARD_VALIDATOR_H

#include "plan_parser.h"

#include "../search_algorithm.h"

namespace plugins {
class Options;
}

namespace validator {

class Validator : public SearchAlgorithm {
    std::string python_file;
    std::string problem_file;
    std::string domain_file;
    int num_actions_applied;
    int seed;
    ParsedPlan plan;
public:
    Validator(const plugins::Options &opts);
    SearchStatus step() override;
    static void print_static_facts(State &);
    void validate();
    // TODO: Would be better if this is a Set
    std::vector<State> random_walk(const State &, int, int);
    void random_walk_recursive(const State &, std::vector<State> &, int);
    void print_statistics() const override;
    void run_plan_generator();
    void print_fluent_facts(const State &) const;
    void copy_and_write_new_problem_file(const State &, int) const;
    State traverse(int num_actions);
    OperatorProxy pick_random_operator(const std::vector<OperatorProxy> &ops);
};

}

#endif // DOWNWARD_VALIDATOR_H
