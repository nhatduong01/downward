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
    ParsedPlan parsed_plan;
    bool clean;
protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;
    Plan plan;
public:
    std::string problem_file;
    std::string domain_file;
    int num_actions_applied;
    int seed;
    int depth;
    bool only_add_leaves;
    bool follow_path;
    int num_walks;
    std::string output_dir;
    int num_instances;
    int num_endeavors;
    int attempts;
    Plan returned_plan;
    shared_ptr<Evaluator> evaluator;
    Validator(const plugins::Options &opts);
    static void print_static_facts(State &);
    int validate(State);
    // TODO: Would be better if this is a Set
    std::vector<State> random_walk(const State &, int, int);
    void random_walk_recursive(const State &, std::vector<State> &, int);
    void print_statistics() const override;
    string run_plan_generator(string) const;
    void print_fluent_facts(const State &) const;
    bool copy_and_write_new_problem_file(
        const State &, int, const string &) const;
    State traverse(int num_actions);
    State get_intermediate_state(int, int, State) const;
    OperatorProxy pick_random_operator(const std::vector<OperatorProxy> &ops);
    bool checkIfSolvable(string file_path) const;
    unordered_set<StateID> random_walk();
    void recursive_random_walk(
        unordered_set<StateID> &visited_states, State curr, int depth,
        bool only_add_leaves);
    void writing_new_files(vector<StateID>);
    void random_walk_and_write(const Plan &);
};

}

#endif // DOWNWARD_VALIDATOR_H
