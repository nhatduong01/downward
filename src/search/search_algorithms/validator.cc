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
#include <regex>
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
      only_add_leaves(opts.get<bool>("only_add_leaves")),
      follow_path(opts.get<bool>("use_solution")),
      output_dir(opts.get<string>("output_dir")), // add this
      num_instances(opts.get<int>("num_instances")),
      evaluator(opts.get<shared_ptr<Evaluator>>("h")),
      num_endeavors(opts.get<int>("num_endeavors")),
      clean(opts.get<bool>("clean")) {
}

void Validator::initialize() {
    assert(evaluator);
    log << "Beginning solving the problem" << endl;
    if (python_file.compare("None") != 0) {
        string plan_path = run_plan_generator(problem_file);
        if (plan_path == "ERROR") {
            log << "[ERROR]: Initial plan generation errored, will return FAILED\n";
            parsed_plan = {};
            attempts = INT_MAX;
            return;
        }
        if (plan_path == "TIMEOUT") {
            log << "[TIMEOUT]: Initial plan generation timed out, will return FAILED\n";
            parsed_plan = {};
            attempts = INT_MAX;
            return;
        }
        PlanParser parser(plan_path);
        parsed_plan = parser.parse();
        attempts = 0;
    }
}

SearchStatus Validator::step() {
    if (python_file != "None") {
        filesystem::path curr_file = this->problem_file;
        string original_base = curr_file.stem().string();
        State curr = this->task_proxy.get_initial_state();
        int start_idx = 0;
        while (attempts <= num_endeavors) {
            int result = validate(curr);
            if (static_cast<unsigned>(result) == parsed_plan.actions.size()) {
                State final_state =
                    get_intermediate_state(start_idx, result, curr);
                if (task_properties::is_goal_state(task_proxy, final_state)) {
                    set_plan(plan);
                } else {
                    log << "[ERROR]: Generated sequence does not reach goal"
                        << endl;
                }
                break;
            }
            if (attempts + 1 > num_endeavors)
                break;
            State failed_state =
                get_intermediate_state(start_idx, result, curr);
            curr = failed_state;
            bool new_file =
                copy_and_write_new_problem_file(failed_state, attempts, ".");
            if (new_file) {
                filesystem::path curr_path = filesystem::current_path();
                string new_name =
                    original_base + "-" + std::to_string(attempts) + ".pddl";
                this->problem_file = curr_path / new_name;
                string plan_path = run_plan_generator(new_name);
                if (plan_path == "ERROR") {
                    log << "[ERROR]: Generating plan for idx: " << attempts << endl;
                    break;
                }
                if (plan_path == "TIMEOUT") {
                    log << "[TIMEOUT]: Generating plan for idx: " << attempts << endl;
                    break;
                }
                PlanParser parser(plan_path);
                parsed_plan = parser.parse();
                attempts++;
            } else {
                log << "[ERROR]: Error generating the intermediate state file\n";
                break;
            }
            start_idx += result;
        }

        if (clean) {
            // Clean up all temporary variant files: instance-10-0.pddl,
            // instance-10-0.log, etc.
            filesystem::path curr_path = filesystem::current_path();
            for (const auto &entry :
                 filesystem::directory_iterator(curr_path)) {
                if (!entry.is_regular_file())
                    continue;

                string filename = entry.path().filename().string();
                string stem = entry.path().stem().string();

                if (stem == original_base ||
                    stem.rfind(original_base + "-", 0) == 0) {
                    filesystem::remove(entry.path());
                }
            }
        }
        if (found_solution()) {
            return SOLVED;
        }
    }
    return FAILED;
}

string Validator::run_plan_generator(string file_name) const {
    filesystem::path file_path = file_name;
    string base_name = file_path.stem().string();
    std::string generated_plan = base_name + ".plan";
    std::string generated_log = base_name + ".log";
    const std::string python_generator =
        "/Users/duongnguyen/UdS_Master/MasterThesis/genplan-strategy-refine-private/generate_plan_for_example.py";
    const std::string python_exec =
        "/Users/duongnguyen/UdS_Master/MasterThesis/genplan-strategy-refine-private/venv/bin/python";

    std::ostringstream cmd;
    cmd << python_exec << " " << python_generator << " -t 45 -p " << domain_file
        << " " << problem_file << " " << python_file << " " << generated_plan
        << " " << generated_log;

    int ret = std::system(cmd.str().c_str());

    if (ret == -1) {
        log << "[ERROR]: Failed to start Python process\n";
        return "ERROR";
    }

    if (!WIFEXITED(ret)) {
        log << "[ERROR]: Python process terminated abnormally\n";
        return "ERROR";
    }

    if (!filesystem::exists(generated_plan)) {
        if (filesystem::exists(generated_log)) {
            std::ifstream log_in(generated_log);
            std::string log_content(
                (std::istreambuf_iterator<char>(log_in)),
                std::istreambuf_iterator<char>());
            if (log_content.find("result of a timeout") != std::string::npos) {
                log << "[TIMEOUT]: Plan generator timed out for " << file_name
                    << "\n";
                return "TIMEOUT";
            }
            if (log_content.find("Generated plan is empty") !=
                std::string::npos) {
                log << "[EMPTY]: Plan generator returned empty plan for "
                    << file_name << "\n";
                return "ERROR";
            }
            if (log_content.find("threw error") != std::string::npos) {
                log << "[ERROR]: Plan generator threw error for " << file_name
                    << "\n";
                return "ERROR";
            }
        }
        log << "[ERROR]: Plan file not created for " << file_name << "\n";
        return "ERROR";
    }

    return generated_plan;
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
    string base_name =
        old_path.stem().string(); // e.g. "instance-10-0" or "instance-10"

    std::regex has_variant_suffix(R"(^(.*-\d+)-\d+$)");
    std::smatch match;
    string clean_base;
    if (std::regex_match(base_name, match, has_variant_suffix)) {
        clean_base = match[1].str(); // e.g. "instance-10-0" -> "instance-10"
    } else {
        clean_base = base_name; // e.g. "instance-10" -> "instance-10"
    }

    filesystem::path output_path = filesystem::path(output_dir);
    filesystem::create_directories(output_path);
    filesystem::path new_path =
        output_path / (clean_base + "-" + std::to_string(variant_id) + ".pddl");
    {
        ifstream old_file(old_path.string());
        ofstream new_file(new_path.string());
        if (!new_file) {
            throw std::runtime_error(
                "Could not open OUTPUT file: " + new_path.string());
        }
        if (!old_file) {
            throw std::runtime_error(
                "Could not open INPUT file: " + old_path.string());
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
    if (!this->follow_path) {
        try {
            if (!checkIfSolvable(new_path.string())) {
                cout << "No solution found, deleting file: "
                     << new_path.string() << "\n";
                filesystem::remove(new_path);
                return false;
            }
        } catch (const std::runtime_error &e) {
            cout << "Planner error: " << e.what()
                 << ", deleting file: " << new_path.string() << "\n";
            filesystem::remove(new_path);
            return false;
        }
    }

    return true;
}
State Validator::traverse(int num_actions) {
    State curr = state_registry.get_initial_state();
    for (int i = 0; i < num_actions; i++) {
        OperatorProxy op = this->task_proxy.get_operators()[returned_plan[i]];
        curr = state_registry.get_successor_state(curr, op);
    }
    return curr;
}
State Validator::get_intermediate_state(
    int start_idx, int num_actions, State starting_state) const {
    State curr = starting_state;
    for (int i = start_idx; i < start_idx + num_actions; i++) {
        OperatorProxy op = this->task_proxy.get_operators()[plan[i]];
        curr = curr.get_unregistered_successor(op);
    }
    return curr;
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
    // Use a unique log file per call to avoid collisions
    string log_file = file_path + ".log";
    string sas_file = file_path + ".sas";
    string plan_file = file_path + ".plan";
    std::ostringstream cmd;
    cmd << "timeout 300 ../alternative_downward/fast-downward.py "
        << " --sas-file " << sas_file << " " << " --plan-file " << plan_file
        << " " << this->domain_file << " " << file_path
        << " --search \"eager_greedy(evals=[ff()])\""
        << " > " << log_file << " 2>&1";

    int ret = std::system(cmd.str().c_str());
    filesystem::remove(sas_file);
    filesystem::remove(plan_file);
    int exit_code = WEXITSTATUS(ret);

    if (exit_code == 0) {
        // Success - discard the log
        filesystem::remove(log_file);
        return true;
    } else if (exit_code == 12) {
        // Truly unsolvable - discard the log
        filesystem::remove(log_file);
        return false;
    } else if (exit_code == 11) {
        cout << "Unsolvable with current bound for file: " << file_path << "\n";
        filesystem::remove(log_file);
        return false;
    } else if (exit_code == 124) {
        cout << "Planner timed out after 4 minutes for file: " << file_path
             << "\n";
        cout << "Log saved to: " << log_file << "\n";
        return false;
    } else if (exit_code == 1) {
        cout << "Planner ran out of memory for file: " << file_path << "\n";
        cout << "Log saved to: " << log_file << "\n";
        return false;
    } else {
        // Unexpected error - keep the log
        cout << "Log saved to: " << log_file << "\n";
        throw std::runtime_error(
            "Planner failed with exit code: " + std::to_string(exit_code));
    }
}
unordered_set<StateID> Validator::random_walk() {
    unordered_set<StateID> visited_states;
    Plan plan = this->returned_plan;
    if (!this->follow_path) {
        cout << "Starting random walk\n";

        while (this->num_actions_applied > (int)plan.size()) {
            cout << "Number of applied action greater than the plan size!\n";
            cout << "Reducing the number by half\n";
            this->num_actions_applied /= 2;
        }
        if (this->num_actions_applied == 0) {
            cout << "Applying from initial state for " << this->problem_file
                 << "\n";
        }
        State starting_state = traverse(this->num_actions_applied);
        for (int j = 0; j < this->num_walks; j++)
            recursive_random_walk(
                visited_states, starting_state, this->depth,
                this->only_add_leaves);

        cout << "End random walk\n";
    } else {
        if (plan.size() == 0) {
            throw std::runtime_error(
                "Cannot follow the solution because there is no solution");
        }
        State curr = state_registry.get_initial_state();
        // Minus 1 to except the goal state

        for (int i = 0; i < (int)plan.size() - 1; i++) {
            OperatorProxy op = this->task_proxy.get_operators()[plan[i]];
            curr = state_registry.get_successor_state(curr, op);
            visited_states.insert(curr.get_id());
        }
    }

    return visited_states;
}
void Validator::recursive_random_walk(
    unordered_set<StateID> &visited_states, State curr, int depth,
    bool only_add_leaves) {
    if (depth < 0)
        return;

    vector<OperatorProxy> applicable_ops =
        task_properties::find_applicable_operators(
            curr, task_proxy.get_operators());

    if (applicable_ops.empty() ||
        task_properties::is_goal_state(task_proxy, curr))
        return;

    if (!only_add_leaves) {
        visited_states.insert(curr.get_id());
    } else {
        // When depth == 0
        if (depth == 0) {
            visited_states.insert(curr.get_id());
        }
    }

    OperatorProxy random_op = this->pick_random_operator(applicable_ops);
    State successor = state_registry.get_successor_state(curr, random_op);
    recursive_random_walk(
        visited_states, successor, depth - 1, only_add_leaves);
}
void Validator::writing_new_files(vector<StateID> states) {
    cout << "Beginning writing_new_files\n";
    int idx = 0;
    int written = 0;
    for (auto state : states) {
        if (written >= num_instances)
            break;
        bool success = this->copy_and_write_new_problem_file(
            state_registry.lookup_state(state), idx, output_dir);
        if (success)
            written++;
        idx++;
    }
    cout << "Written " << written << " files" << " for" << this->problem_file
         << "\n";
    cout << "Ending writing_new_files\n";
}
void Validator::random_walk_and_write(const Plan &plan) {
    this->returned_plan = plan;
    unordered_set<StateID> states = this->random_walk();
    if ((int)states.size() < num_instances) {
        cout << "Warning: only " << states.size() << " for "
             << this->problem_file << " states available, needed "
             << num_instances << "\n";
    }
    vector<StateID> states_vec(states.begin(), states.end());
    std::shuffle(
        states_vec.begin(), states_vec.end(),
        std::mt19937{(unsigned)this->seed});
    cout << "Number of states: " << states_vec.size() << endl;
    writing_new_files(states_vec);
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

int Validator::validate(State starting_state) {
    State curr_state = starting_state;
    OperatorsProxy all_ops = this->task_proxy.get_operators();
    int idx = 0;
    for (auto &each_op : parsed_plan.actions) {
        OperatorProxy op =
            task_properties::find_operator(each_op.to_string(), all_ops);
        if (task_properties::is_applicable(op, curr_state)) {
            curr_state = curr_state.get_unregistered_successor(op);
            plan.emplace_back(op.get_id());
            idx++;
        } else {
            cout << "Operator is not applicable: " << each_op.to_string()
                 << endl;
            return idx;
        }
    }
    return idx;
}

class ValidatorFeature
    : public plugins::TypedFeature<SearchAlgorithm, Validator> {
public:
    ValidatorFeature() : TypedFeature("validator") {
        add_option<std::string>(
            "python_program",
            "Path to the"
            "python program",
            "None");
        add_option<std::string>("domain_file");
        add_option<std::string>("problem_file");
        add_option<int>("num_actions", "Number of actions applied", "3");
        add_option<int>("seed", "random seed", "0");
        add_option<utils::Verbosity>("verbosity", "Verbosity level");
        add_option<OperatorCost>("cost_type", "Cost type");
        add_option<double>("max_time", "Max time");
        add_option<int>("bound", "Bound");
        add_option<int>("depth", "Depth of the Walk", "4");
        add_option<bool>(
            "only_add_leaves", "whether to only add leaves node", "true");
        add_option<bool>(
            "use_solution", "whether to follow the path of the algorithm",
            "false");
        add_option<std::string>(
            "output_dir", "Directory to write new instances to", "None");
        add_option<int>("num_walks", "Number of random walks", "3");
        add_option<int>(
            "num_instances", "Number of new instances to generate per task",
            "2");
        add_option<shared_ptr<Evaluator>>("h", "", "ff()");
        add_option<int>(
            "num_endeavors", "Number of times trying to solve the task", "3");
        add_option<bool>(
            "clean", "whether to clean the generated files", "true");
    }

    virtual shared_ptr<Validator> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<Validator>(opts);
    }
};
static plugins::FeaturePlugin<ValidatorFeature> _plugin;

}