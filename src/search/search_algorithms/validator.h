//
// Created by Duong Nguyen on 24.09.25.
//

#ifndef DOWNWARD_VALIDATOR_H
#define DOWNWARD_VALIDATOR_H

#include "../search_algorithm.h"

namespace plugins {
class Options;
}

namespace validator {

class Validator : public SearchAlgorithm {
    std::string plan_file;
public:
    Validator(const plugins::Options &opts);
    SearchStatus step() override;
    static void print_static_facts(State &);
    void validate();
    // TODO: Would be better if this is a Set
    std::vector<State> random_walk(const State &, int, int);
    void random_walk_recursive(const State &, std::vector<State> &, int);
    void print_statistics() const override;
};

}

#endif // DOWNWARD_VALIDATOR_H
