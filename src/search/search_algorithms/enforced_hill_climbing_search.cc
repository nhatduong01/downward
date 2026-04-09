#include "enforced_hill_climbing_search.h"
#include <algorithm>

#include "../algorithms/ordered_set.h"
#include "../evaluators/g_evaluator.h"
#include "../evaluators/pref_evaluator.h"
#include "../open_lists/best_first_open_list.h"
#include "../open_lists/tiebreaking_open_list.h"
#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"
#include "../utils/system.h"

using namespace std;
using utils::ExitCode;

namespace enforced_hill_climbing_search {

using GEval = g_evaluator::GEvaluator;
using PrefEval = pref_evaluator::PrefEvaluator;

static shared_ptr<OpenListFactory> create_ehc_open_list_factory(
    utils::Verbosity verbosity, bool use_preferred,
    PreferredUsage preferred_usage) {
    /*
      TODO: this g-evaluator should probably be set up to always
      ignore costs since EHC is supposed to implement a breadth-first
      search, not a uniform-cost search. So this seems to be a bug.
    */
    shared_ptr<Evaluator> g_evaluator =
        make_shared<GEval>("ehc.g_eval", verbosity);

    if (!use_preferred ||
        preferred_usage == PreferredUsage::PRUNE_BY_PREFERRED) {
        return make_shared<standard_scalar_open_list::BestFirstOpenListFactory>(
            g_evaluator, false);
    } else {
        /*
          TODO: Reduce code duplication with search_common.cc,
          function create_astar_open_list_factory_and_f_eval.

          It would probably make sense to add a factory function or
          constructor that encapsulates this work to the tie-breaking
          open list code.
        */
        vector<shared_ptr<Evaluator>> evals = {
            g_evaluator, make_shared<PrefEval>("ehc.pref_eval", verbosity)};
        return make_shared<tiebreaking_open_list::TieBreakingOpenListFactory>(
            evals, false, true);
    }
}

EnforcedHillClimbingSearch::EnforcedHillClimbingSearch(
    const shared_ptr<Evaluator> &h, PreferredUsage preferred_usage,
    const vector<shared_ptr<Evaluator>> &preferred, OperatorCost cost_type,
    int bound, double max_time, const string &description,
    utils::Verbosity verbosity, const plugins::Options &opts)
    : SearchAlgorithm(cost_type, bound, max_time, description, verbosity),
      evaluator(h),
      preferred_operator_evaluators(preferred),
      preferred_usage(preferred_usage),
      current_eval_context(state_registry.get_initial_state(), &statistics),
      current_phase_start_g(-1),
      num_ehc_phases(0),
      last_num_expanded(-1),
      validator(opts),
      output_dir(opts.get<string>("output_dir")),      // add this
      num_instances(opts.get<int>("num_instances")){
    for (const shared_ptr<Evaluator> &eval : preferred_operator_evaluators) {
        eval->get_path_dependent_evaluators(path_dependent_evaluators);
    }
    evaluator->get_path_dependent_evaluators(path_dependent_evaluators);

    State initial_state = state_registry.get_initial_state();
    for (Evaluator *evaluator : path_dependent_evaluators) {
        evaluator->notify_initial_state(initial_state);
    }
    use_preferred = find(
                        preferred_operator_evaluators.begin(),
                        preferred_operator_evaluators.end(),
                        evaluator) != preferred_operator_evaluators.end();

    open_list =
        create_ehc_open_list_factory(verbosity, use_preferred, preferred_usage)
            ->create_edge_open_list();
}

void EnforcedHillClimbingSearch::reach_state(
    const State &parent, OperatorID op_id, const State &state) {
    for (Evaluator *evaluator : path_dependent_evaluators) {
        evaluator->notify_state_transition(parent, op_id, state);
    }
}

void EnforcedHillClimbingSearch::initialize() {
    assert(evaluator);
    log << "Conducting enforced hill-climbing search, (real) bound = " << bound
        << endl;
    if (use_preferred) {
        log << "Using preferred operators for "
            << (preferred_usage == PreferredUsage::RANK_PREFERRED_FIRST
                    ? "ranking successors"
                    : "pruning")
            << endl;
    }

    bool dead_end =
        current_eval_context.is_evaluator_value_infinite(evaluator.get());
    statistics.inc_evaluated_states();
    print_initial_evaluator_values(current_eval_context);

    if (dead_end) {
        log << "Initial state is a dead end, no solution" << endl;
        if (evaluator->dead_ends_are_reliable())
            utils::exit_with(ExitCode::SEARCH_UNSOLVABLE);
        else
            utils::exit_with(ExitCode::SEARCH_UNSOLVED_INCOMPLETE);
    }

    SearchNode node = search_space.get_node(current_eval_context.get_state());
    node.open_initial();

    current_phase_start_g = 0;
}

void EnforcedHillClimbingSearch::insert_successor_into_open_list(
    const EvaluationContext &eval_context, int parent_g, OperatorID op_id,
    bool preferred) {
    OperatorProxy op = task_proxy.get_operators()[op_id];
    int succ_g = parent_g + get_adjusted_cost(op);
    const State &state = eval_context.get_state();
    EdgeOpenListEntry entry = make_pair(state.get_id(), op_id);
    EvaluationContext new_eval_context(
        eval_context, succ_g, preferred, &statistics);
    open_list->insert(new_eval_context, entry);
    statistics.inc_generated_ops();
}

void EnforcedHillClimbingSearch::expand(EvaluationContext &eval_context) {
    SearchNode node = search_space.get_node(eval_context.get_state());
    int node_g = node.get_g();

    ordered_set::OrderedSet<OperatorID> preferred_operators;
    if (use_preferred) {
        for (const shared_ptr<Evaluator> &preferred_operator_evaluator :
             preferred_operator_evaluators) {
            collect_preferred_operators(
                eval_context, preferred_operator_evaluator.get(),
                preferred_operators);
        }
    }

    if (use_preferred &&
        preferred_usage == PreferredUsage::PRUNE_BY_PREFERRED) {
        for (OperatorID op_id : preferred_operators) {
            insert_successor_into_open_list(eval_context, node_g, op_id, true);
        }
    } else {
        /* The successor ranking implied by RANK_BY_PREFERRED is done
           by the open list. */
        vector<OperatorID> successor_operators;
        successor_generator.generate_applicable_ops(
            eval_context.get_state(), successor_operators);
        for (OperatorID op_id : successor_operators) {
            bool preferred =
                use_preferred && preferred_operators.contains(op_id);
            insert_successor_into_open_list(
                eval_context, node_g, op_id, preferred);
        }
    }

    statistics.inc_expanded();
    node.close();
}

void EnforcedHillClimbingSearch::random_walk_and_write() {
    unordered_set<StateID> states = random_walk();
    if ((int)states.size() < num_instances) {
        cout << "Warning: only " << states.size() << " for "<< validator.problem_file
             << " states available, needed " << num_instances << "\n";
    }
    vector<StateID> states_vec(states.begin(), states.end());
    std::shuffle(states_vec.begin(), states_vec.end(), std::mt19937{(unsigned)validator.seed});
    cout << "Number of states: " << states_vec.size() << endl;
    writing_new_files(states_vec);
}

void EnforcedHillClimbingSearch::recursive_random_walk(
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


    OperatorProxy random_op = validator.pick_random_operator(applicable_ops);
    State successor = state_registry.get_successor_state(curr, random_op);
    recursive_random_walk(
        visited_states, successor, depth - 1, only_add_leaves);
}

State EnforcedHillClimbingSearch::traverse(int num_actions) {
    State curr = state_registry.get_initial_state();
    Plan plan = this->get_plan();
    for (int i = 0; i < num_actions; i++) {
        OperatorProxy op = this->task_proxy.get_operators()[plan[i]];
        curr = state_registry.get_successor_state(curr, op);
    }
    return curr;
}
void EnforcedHillClimbingSearch::writing_new_files(
    vector<StateID> states) {
    cout << "Beginning writing_new_files\n";
    int idx = 0;
    int written = 0;
    for (auto state : states) {
        if (written >= num_instances) break;
        bool success = validator.copy_and_write_new_problem_file(
            state_registry.lookup_state(state), idx, output_dir);
        if (success) written++;
        idx++;
    }
    cout << "Written " << written << " files" << " for" << validator.problem_file  <<"\n";
    cout << "Ending writing_new_files\n";
}

unordered_set<StateID> EnforcedHillClimbingSearch::random_walk() {
    unordered_set<StateID> visited_states;
    Plan plan = this->get_plan();
    if (!validator.follow_path) {
        cout << "Starting random walk\n";

        while (validator.num_actions_applied > (int)plan.size()) {
            validator.num_actions_applied --;
        }
        if (validator.num_actions_applied == 0) {
            cout << "Applying from initial state for " << validator.problem_file << "\n";
        }
        State starting_state = traverse(validator.num_actions_applied);
        for (int j = 0; j < validator.num_walks; j++)
            recursive_random_walk(
                visited_states, starting_state, validator.depth, validator.only_add_leaves);

        cout << "End random walk\n";
    }
    else {

        if (plan.size() == 0) {
            throw std::runtime_error("Cannot follow the solution because there is no solution");
        }
        // Start from 1 to not include the initial state
        State curr = state_registry.get_initial_state();
        for (int i = 1; i < plan.size(); i++) {
            OperatorProxy op = this->task_proxy.get_operators()[plan[i]];
            curr = state_registry.get_successor_state(curr, op);
            visited_states.insert(curr.get_id());
        }
    }

    return visited_states;
}

SearchStatus EnforcedHillClimbingSearch::step() {
    last_num_expanded = statistics.get_expanded();
    search_progress.check_progress(current_eval_context);

    if (check_goal_and_set_plan(current_eval_context.get_state())) {
        random_walk_and_write();
        return SOLVED;
    }

    expand(current_eval_context);
    return ehc();
}

SearchStatus EnforcedHillClimbingSearch::ehc() {
    while (!open_list->empty()) {
        EdgeOpenListEntry entry = open_list->remove_min();
        StateID parent_state_id = entry.first;
        OperatorID last_op_id = entry.second;
        OperatorProxy last_op = task_proxy.get_operators()[last_op_id];

        State parent_state = state_registry.lookup_state(parent_state_id);
        SearchNode parent_node = search_space.get_node(parent_state);

        // d: distance from initial node in this EHC phase
        int d = parent_node.get_g() - current_phase_start_g +
                get_adjusted_cost(last_op);

        if (parent_node.get_real_g() + last_op.get_cost() >= bound)
            continue;

        State state = state_registry.get_successor_state(parent_state, last_op);
        statistics.inc_generated();

        SearchNode node = search_space.get_node(state);

        if (node.is_new()) {
            EvaluationContext eval_context(state, &statistics);
            reach_state(parent_state, last_op_id, state);
            statistics.inc_evaluated_states();

            if (eval_context.is_evaluator_value_infinite(evaluator.get())) {
                node.mark_as_dead_end();
                statistics.inc_dead_ends();
                continue;
            }

            int h = eval_context.get_evaluator_value(evaluator.get());
            node.open_new_node(
                parent_node, last_op, get_adjusted_cost(last_op));

            if (h < current_eval_context.get_evaluator_value(evaluator.get())) {
                ++num_ehc_phases;
                if (d_counts.count(d) == 0) {
                    d_counts[d] = make_pair(0, 0);
                }
                pair<int, int> &d_pair = d_counts[d];
                d_pair.first += 1;
                d_pair.second += statistics.get_expanded() - last_num_expanded;

                current_eval_context = move(eval_context);
                open_list->clear();
                current_phase_start_g = node.get_g();
                return IN_PROGRESS;
            } else {
                expand(eval_context);
            }
        }
    }
    log << "No solution - FAILED" << endl;
    cout << "Begin random walk with number of applied actions is 0";
    this->validator.num_actions_applied = 0;
    random_walk_and_write();
    return FAILED;
}

void EnforcedHillClimbingSearch::print_statistics() const {
    statistics.print_detailed_statistics();

    log << "EHC phases: " << num_ehc_phases << endl;
    assert(num_ehc_phases != 0);
    log << "Average expansions per EHC phase: "
        << static_cast<double>(statistics.get_expanded()) / num_ehc_phases
        << endl;

    for (auto count : d_counts) {
        int depth = count.first;
        int phases = count.second.first;
        assert(phases != 0);
        int total_expansions = count.second.second;
        log << "EHC phases of depth " << depth << ": " << phases
            << " - Avg. Expansions: "
            << static_cast<double>(total_expansions) / phases << endl;
    }
}

class EnforcedHillClimbingSearchFeature
    : public plugins::TypedFeature<
          SearchAlgorithm, EnforcedHillClimbingSearch> {
public:
    EnforcedHillClimbingSearchFeature() : TypedFeature("ehc") {
        document_title("Lazy enforced hill-climbing");
        document_synopsis("");

        add_option<shared_ptr<Evaluator>>("h", "heuristic");
        add_option<PreferredUsage>(
            "preferred_usage", "preferred operator usage",
            "prune_by_preferred");
        add_list_option<shared_ptr<Evaluator>>(
            "preferred", "use preferred operators of these evaluators", "[]");
        add_search_algorithm_options_to_feature(*this, "ehc");
        add_option<std::string>("domain_file");
        add_option<std::string>("problem_file");
        add_option<int>("num_actions");
        add_option<int>("seed", "Random Seed", "0");
        add_option<int>("depth", "Depth of the Walk", "4");
        add_option<int>("num_walks", "Number of random walks", "3");
        add_option<std::string>("output_dir", "Directory to write new instances to");
        add_option<int>("num_instances", "Number of new instances to generate per task", "2");
        add_option<bool>("only_add_leaves", "whether to only add leaves node", "true");
        add_option<bool>("use_ehc_solution", "whether to follow the path of ehc algorithm", "false");
        add_option<std::string>("python_program");
    }

    virtual shared_ptr<EnforcedHillClimbingSearch> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<EnforcedHillClimbingSearch>(
            opts.get<shared_ptr<Evaluator>>("h"),
            opts.get<PreferredUsage>("preferred_usage"),
            opts.get_list<shared_ptr<Evaluator>>("preferred"),
            get_search_algorithm_arguments_from_options(opts), opts);
    }
};

static plugins::FeaturePlugin<EnforcedHillClimbingSearchFeature> _plugin;

static plugins::TypedEnumPlugin<PreferredUsage> _enum_plugin(
    {{"prune_by_preferred",
      "prune successors achieved by non-preferred operators"},
     {"rank_preferred_first",
      "first insert successors achieved by preferred operators, "
      "then those by non-preferred operators"}});
}
