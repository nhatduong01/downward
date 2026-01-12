//
// Created by Duong Nguyen on 24.09.25.
//
#include "validator.h"

#include "plan_parser.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"

#include <random>

using namespace std;
using utils::ExitCode;

namespace validator {
Validator::Validator(const plugins::Options &opts)
    : SearchAlgorithm(opts), plan_file(opts.get<string>("python_program")) {
}

void Validator::print_statistics() const {
    cout << "Validation statistics:" << endl;
}

OperatorProxy pick_random_operator(const std::vector<OperatorProxy> &ops) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, ops.size() - 1);
    return ops[dist(rng)];
}

void Validator::random_walk_recursive(
    const State &curr, std::vector<State> &visited_states, int depth) {
    if (depth == 0)
        return;
    vector<OperatorProxy> applicable_ops =
        task_properties::find_applicable_operators(
            curr, task_proxy.get_operators());
    OperatorProxy random_op = pick_random_operator(applicable_ops);
    // State successor = state_registry.get_successor_state(curr, random_op);
    State successor = curr.get_unregistered_successor(random_op);
    if (!task_properties::contained_in_vector(visited_states, successor)) {
        visited_states.push_back(successor);
    }
    int next_depth =
        task_properties::is_goal_state(task_proxy, successor) ? 0 : depth - 1;
    random_walk_recursive(successor, visited_states, next_depth);
}

std::vector<State> Validator::random_walk(
    const State &curr_state, int num_walks, int depth) {
    vector visited_states = {curr_state};
    for (int i = 0; i < num_walks; i++) {
        random_walk_recursive(curr_state, visited_states, depth);
    }
    return visited_states;
}

void Validator::print_static_facts(State &curr_state) {
    std::cout << "Print out the facts of the current state\n";
    TaskProxy curr_task = curr_state.get_task();
    VariablesProxy all_var = curr_task.get_variables();
    FactsProxy all_fact = all_var.get_facts();
    for (const FactProxy &fact : all_fact) {
        std::cout << "Fact name: " << fact.get_name()
                  << " value: " << fact.get_value() << "\n";
    }
    std::cout << "Print out the true fact from mutex, num variables: "
              << all_var.size() << "\n";
    for (VariableProxy var : all_var) {
        FactProxy fact = curr_state[var]; // get value index
        std::cout << "Fact: " << fact.get_name() << std::endl;
    }

    int i = 0;
    int k = 0;
    std::cout << "Print all the fact";
    for (VariableProxy var : all_var) {
        std::cout << "Var " << i++ << ": " << var.get_name()
                  << "  (domain size: " << var.get_domain_size() << ")\n";
        for (int j = 0; j < var.get_domain_size(); ++j) {
            FactProxy curr_fact = var.get_fact(j);
            std::cout << "Fact " << ++k << ": " << curr_fact.get_name()
                      << " Value: " << curr_fact.get_value() << "\n";
        }
    }
}

void Validator::validate() {
    PlanParser parser(this->plan_file);
    ParsedPlan plan = parser.parse();
    plan.print_plan();
    State curr_state = this->state_registry.get_initial_state();
    OperatorsProxy all_ops = this->task_proxy.get_operators();
    int idx = 0;
    bool is_inapplicable = false;
    for (auto &each_op : plan.actions) {
        OperatorProxy op =
            task_properties::find_operator(each_op.to_string(), all_ops);
        if (task_properties::is_applicable(op, curr_state)) {
            curr_state =
                this->state_registry.get_successor_state(curr_state, op);
            idx++;
            if (idx == 1) {
                print_static_facts(curr_state);
            }
        } else {
            cout << "Operator is not applicable: " << each_op.to_string()
                 << endl;
            is_inapplicable = true;
            break;
        }
    }
    bool is_goal = task_properties::is_goal_state(this->task_proxy, curr_state);
    if (!is_inapplicable && is_goal) {
        cout << "The plan is valid!!\n";
    } else {
        cout << "The plan is not valid\n";
    }
}

SearchStatus Validator::step() {
    vector<State> visited_states =
        random_walk(this->state_registry.get_initial_state(), 2, 3);
    cout << "Size of the visited states: " << visited_states.size() << endl;
    return SearchStatus::SOLVED;
}

class ValidatorFeature
    : public plugins::TypedFeature<SearchAlgorithm, Validator> {
public:
    ValidatorFeature() : TypedFeature("validator") {
        add_option<std::string>(
            "python_program", "Path to the"
                              "python program");
        add_option<utils::Verbosity>("verbosity", "Verbosity level");
        add_option<OperatorCost>("cost_type", "Cost type");
        add_option<double>("max_time", "Max time");
        add_option<int>("bound", "Bound");
    }

    virtual shared_ptr<Validator> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<Validator>(opts);
    }
};
static plugins::FeaturePlugin<ValidatorFeature> _plugin;

}