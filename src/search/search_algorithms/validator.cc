//
// Created by Duong Nguyen on 24.09.25.
//
#include "validator.h"

#include "plan_parser.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
// #include <string>

using namespace std;
using utils::ExitCode;

namespace validator {
Validator::Validator(const plugins::Options &opts)
    : SearchAlgorithm(opts),
      python_file(opts.get<string>("python_program")),
      problem_file(opts.get<string>("problem_file")),
      domain_file(opts.get<string>("domain_file")),
      num_actions_applied(opts.get<int>("num_actions")),
      seed(opts.get<int>("seed")),
      depth(opts.get<int>("depth")),
      num_walks(opts.get<int>("num_walks")),
      only_add_leaves(opts.get<bool>("only_add_leaves")) {
    // run_plan_generator();
    // PlanParser parser("generated_plan.plan");
    // plan = parser.parse();
    // plan.print_plan();
}

void Validator::run_plan_generator() {
    std::string generated_plan = "generated_plan.plan";
    std::string generated_log = "generated_plan.log";
    const std::string python_generator =
        "/Users/duongnguyen/UdS_Master/MasterThesis/genplan-strategy-refine/generate_plan_for_example.py";
    const std::string python_exec =
        "/Users/duongnguyen/UdS_Master/MasterThesis/genplan-strategy-refine/venv/bin/python";
    std::ostringstream cmd;
    cmd << python_exec << " " << python_generator << " -t 30 -p " << domain_file
        << " " << problem_file << " " << python_file << " " << generated_plan
        << " " << generated_log;

    int ret = std::system(cmd.str().c_str());

    if (ret == -1) {
        throw std::runtime_error("Failed to start Python process");
    }

    if (WIFEXITED(ret)) {
        int exit_code = WEXITSTATUS(ret);
        if (exit_code != 0) {
            std::ostringstream err;
            err << "Python plan generator failed with exit code " << exit_code;
            throw std::runtime_error(err.str());
        }
    } else {
        throw std::runtime_error("Python process terminated abnormally");
    }
}

void Validator::print_fluent_facts(const State &curr_state) const {
    std::cout << "Print out the facts of the current state\n";
    for (int i = 0; i < curr_state.size(); i++) {
        FactProxy fact = curr_state[i];
        cout << fact.get_name() << "\n";
        cout << "fact name is: " << fact.printPDDLformat() << "\n";
    }
}

bool Validator::copy_and_write_new_problem_file(
    const State &curr_state, int variant_id, const string &output_dir) const {
        filesystem::path old_path = this->problem_file;
        string base_name = old_path.stem().string();

    filesystem::path output_path = filesystem::path(output_dir);
    filesystem::create_directories(output_path);  // create if not exists
    filesystem::path new_path = output_path / (base_name + "-" + std::to_string(variant_id) + ".pddl");
    {
        ifstream old_file(old_path.string());
        ofstream new_file(new_path.string());
        if (!old_file || !new_file) {
            throw std::runtime_error("Could not open file");
        }

        std::string each_line;
        bool inside_init = false;
        bool after_init_before_goal = false;

        while (getline(old_file, each_line)) {
            if (each_line.find("(:init") != std::string::npos) {
                inside_init = true;
                after_init_before_goal = false;

                new_file << "(:init\n";

                for (auto i = 0; i < curr_state.size(); i++) {
                    string new_line = curr_state[i].printPDDLformat();
                    const string none_of_those = "none of those";
                    if (new_line.find(none_of_those) == string::npos)
                        new_file << "    " << new_line << "\n";
                }

                new_file << ")\n"; // close init
                continue;
            }

            if (inside_init) {
                inside_init = false;
                after_init_before_goal = true;
                continue;
            }

            // We reach goal section
            if (after_init_before_goal) {
                if (each_line.find("(:goal") != std::string::npos) {
                    after_init_before_goal = false;
                    new_file << each_line << "\n";
                }
                continue;
            }

            // No problem, just copy!
            new_file << each_line << "\n";
        }
    }
    cout << "Copied to file: " << new_path.string() << "\n";

    try {
        if (!checkIfSolvable(new_path.string())) {
            cout << "No solution found, deleting file: " << new_path.string()
                 << "\n";
            filesystem::remove(new_path);
            return false;
        }
    } catch (const std::runtime_error &e) {
        cout << "Planner error: " << e.what()
             << ", deleting file: " << new_path.string() << "\n";
        filesystem::remove(new_path);
        return false;
    }
    return true;
}
State Validator::traverse(int num_actions) {
    State curr_state = task_proxy.get_initial_state();
    for (int i = 0; i < num_actions; i++) {
        OperatorProxy op = task_properties::find_operator(
            plan.actions[i].to_string(), task_proxy.get_operators());
        cout << "Applied action: " << op.get_name() << "\n";
        if (task_properties::is_applicable(op, curr_state)) {
            curr_state = curr_state.get_unregistered_successor(op);
        } else {
            cout << "Operator is not applicable: "
                 << plan.actions[i].to_string() << endl;
            throw std::runtime_error("Inapplicable action");
        }
    }
    return curr_state;
}

void Validator::print_statistics() const {
    cout << "Validation statistics:" << endl;
}

OperatorProxy Validator::pick_random_operator(
    const std::vector<OperatorProxy> &ops) {
    static thread_local std::mt19937 rng{(unsigned)this->seed};
    std::uniform_int_distribution<std::size_t> dist(0, ops.size() - 1);
    return ops[dist(rng)];
}
bool Validator::checkIfSolvable(string file_path) const {
    std::ostringstream cmd;
    // Time out after 10 minutes
    cmd << "timeout 240 ../alternative_downward/fast-downward.py " << this->domain_file << " " << file_path
        << " --search \"ehc(ff(), bound=infinity)\"" << " > /dev/null 2>&1";  // suppress all output;
    int ret = std::system(cmd.str().c_str());
    int exit_code = WEXITSTATUS(ret);

    if (exit_code == 0) {
        return true; // Solution found
    } else if (exit_code == 12) {
        return false; // No solution exists
    }
    else if (exit_code == 11) {
        cout << "Unsolvable with current bound\n";
        return false;
    }
     else if (exit_code == 124) {
        cout << "Planner timed out after 4 minutes for file: "<< file_path << "\n";
        return false;
    }
    else {
        throw std::runtime_error(
            "Planner failed with exit code: " + std::to_string(exit_code));
    }
}

void Validator::random_walk_recursive(
    const State &curr, std::vector<State> &visited_states, int depth) {
    if (depth == 0)
        return;
    vector<OperatorProxy> applicable_ops =
        task_properties::find_applicable_operators(
            curr, task_proxy.get_operators());
    if (applicable_ops.empty()) {
        return;
    }
    OperatorProxy random_op = pick_random_operator(applicable_ops);
    State successor = state_registry.get_successor_state(curr, random_op);
    // State successor = curr.get_unregistered_successor(random_op);
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

void Validator::validate() {
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
    // validate();
    // vector<State> visited_states =
    //     random_walk(this->state_registry.get_initial_state(), 2, 3);
    // cout << "Size of the visited states: " << visited_states.size() << endl;
    // for (int i = 0; i < visited_states.size(); i++) {
    //     copy_and_write_new_problem_file(visited_states[i], i);
    // }
    // copy_and_write_new_problem_file(traverse(num_actions_applied));
    return SearchStatus::SOLVED;
}

class ValidatorFeature
    : public plugins::TypedFeature<SearchAlgorithm, Validator> {
public:
    ValidatorFeature() : TypedFeature("validator") {
        add_option<std::string>(
            "python_program", "Path to the"
                              "python program");
        add_option<std::string>("domain_file");
        add_option<std::string>("problem_file");
        add_option<int>("num_actions");
        add_option<int>("seed");
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