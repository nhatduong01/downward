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
    void print_statistics() const override;
};

}


#endif // DOWNWARD_VALIDATOR_H
