//
// Created by Duong Nguyen on 24.09.25.
//
#include "validator.h"

#include "plan_parser.h"

#include "../plugins/plugin.h"

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