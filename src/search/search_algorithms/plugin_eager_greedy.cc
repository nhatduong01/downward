#include "eager_search.h"
#include "search_common.h"

#include "../plugins/plugin.h"

using namespace std;

namespace plugin_eager_greedy {
class EagerGreedySearchFeature
    : public plugins::TypedFeature<SearchAlgorithm, eager_search::EagerSearch> {
public:
    EagerGreedySearchFeature() : TypedFeature("eager_greedy") {
        document_title("Greedy search (eager)");
        document_synopsis("");

        add_list_option<shared_ptr<Evaluator>>("evals", "evaluators");
        add_list_option<shared_ptr<Evaluator>>(
            "preferred", "use preferred operators of these evaluators", "[]");
        add_option<int>(
            "boost", "boost value for preferred operator open lists", "0");
        eager_search::add_eager_search_options_to_feature(
            *this, "eager_greedy");

        document_note(
            "Open list",
            "In most cases, eager greedy best first search uses "
            "an alternation open list with one queue for each evaluator. "
            "If preferred operator evaluators are used, it adds an extra queue "
            "for each of these evaluators that includes only the nodes that "
            "are generated with a preferred operator. "
            "If only one evaluator and no preferred operator evaluator is used, "
            "the search does not use an alternation open list but a "
            "standard open list with only one queue.");
        document_note("Closed nodes", "Closed node are not re-opened");
        document_note(
            "Equivalent statements using general eager search",
            "\n```\n--search \"let(h2, eval2, eager_greedy([eval1, h2], preferred=[h2], boost=100))\"\n```\n"
            "is equivalent to\n"
            "```\n--search \"let(h1, eval1, let(h2, eval2,\n"
            "              eager(alt([single(h1), single(h1, pref_only=true), \n"
            "                         single(h2), single(h2, pref_only=true)], boost=100),\n"
            "                    preferred=[h2])))\"\n```\n"
            "------------------------------------------------------------\n"
            "```\n--search \"eager_greedy([eval1, eval2])\"\n```\n"
            "is equivalent to\n"
            "```\n--search \"eager(alt([single(eval1), single(eval2)]))\"\n```\n"
            "------------------------------------------------------------\n"
            "```\n--search \"let(h1, eval1, eager_greedy([h1], preferred=[h1]))\"\n```\n"
            "is equivalent to\n"
            "```\n--search \"let(h1, eval1, eager(alt([single(h1), single(h1, pref_only=true)]),\n"
            "                               preferred=[h1]))\"\n```\n"
            "------------------------------------------------------------\n"
            "```\n--search \"eager_greedy([eval1])\"\n```\n"
            "is equivalent to\n"
            "```\n--search \"eager(single(eval1))\"\n```\n",
            true);
        add_option<std::string>("domain_file");
        add_option<std::string>("problem_file");
        add_option<int>("num_actions");
        add_option<int>("seed", "Random Seed", "0");
        add_option<int>("depth", "Depth of the Walk", "4");
        add_option<int>("num_walks", "Number of random walks", "3");
        add_option<std::string>("output_dir", "Directory to write new instances to");
        add_option<int>("num_instances", "Number of new instances to generate per task", "2");
        add_option<bool>("only_add_leaves", "whether to only add leaves node", "true");
        add_option<bool>("use_solution", "whether to follow the path of the algorithm", "false");
        add_option<std::string>("python_program");
    }

    virtual shared_ptr<eager_search::EagerSearch> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<eager_search::EagerSearch>(
            search_common::create_greedy_open_list_factory(
                opts.get_list<shared_ptr<Evaluator>>("evals"),
                opts.get_list<shared_ptr<Evaluator>>("preferred"),
                opts.get<int>("boost")),
            false, nullptr, opts.get_list<shared_ptr<Evaluator>>("preferred"),
            eager_search::get_eager_search_arguments_from_options(opts), opts);
    }
};

static plugins::FeaturePlugin<EagerGreedySearchFeature> _plugin;
}
