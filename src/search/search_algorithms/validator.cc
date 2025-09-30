//
// Created by Duong Nguyen on 24.09.25.
//
#include "validator.h"

#include "plan_parser.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"

using namespace std;
using utils::ExitCode;

namespace validator {
 Validator::Validator(const plugins::Options &opts): SearchAlgorithm(opts),
    plan_file(opts.get<string>("plan_file")){
 }

void Validator::print_statistics() const {
    std::cout << "In the validator already";
 }

SearchStatus Validator::step() {
     PlanParser parser(this->plan_file);
     ParsedPlan plan = parser.parse();
     plan.print_plan();
     State curr_state = this->state_registry.get_initial_state();
     OperatorsProxy all_ops = this->task_proxy.get_operators();
     for (auto & each_op : plan.actions) {
         OperatorProxy op = task_properties::find_operator(each_op.to_string(), all_ops);
         if (task_properties::is_applicable(op, curr_state)) {
             curr_state = this->state_registry.get_successor_state(curr_state, op);
         }
         else {
             cout << "Operator is not applicable: " <<each_op.to_string() << endl;
         }
     }
     bool is_goal = task_properties::is_goal_state(this->task_proxy, curr_state);
     if (is_goal) {
         cout << "The plan is valid!!\n";
     }
     else {
         cout << "The plan is not valid\n";
     }
     return SearchStatus::SOLVED;
 }


class ValidatorFeature : public plugins::TypedFeature<SearchAlgorithm, Validator> {
     public:
     ValidatorFeature(): TypedFeature("validator") {
         add_option<std::string>("plan_file", "Path to the"
                                              "plan file");
         add_option<utils::Verbosity>("verbosity", "Verbosity level");
         add_option<OperatorCost>("cost_type", "Cost type");
         add_option<double>("max_time", "Max time");
         add_option<int>("bound", "Bound");
     }

     virtual shared_ptr<Validator> create_component(const plugins::Options &opts) const override {
         return plugins::make_shared_from_arg_tuples<Validator>(opts);
     }
 };
static plugins::FeaturePlugin<ValidatorFeature> _plugin;

}