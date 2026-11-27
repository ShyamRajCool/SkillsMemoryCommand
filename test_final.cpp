// tests/final/test_final.cpp — YOUR TEST SUITE (spec §8).
//
// You need at least 20 tests. The 8 examples below are given and count:
//   Memory  (4): heap, lru, bst, ctx
//   Policy  (2): pol x2
//   Skills  (2): graph, index
// Write at least 12 more of your own, covering the list in spec §8.
// Name tests <area>_<what> with the areas heap, lru, bst, ctx, pol, graph,
// index, so the summary groups them.
//
//   ./build/test_final              run everything
//   ./build/test_final lru_         only tests whose names start with lru_
//   ./build/test_final =lru_evicts_least_recently_used    one test, exactly
//   ./build/test_final --list       list test names
//
// If a sanitizer error (ASan/UBSan) aborts the run, the tests after it do
// not run: rerun the failing test alone with =its_name and read the FIRST
// stack frame that is in your own file.
//
// CHECK_EQ(a, b) prints both values when it fails (vectors too), so prefer
// it over CHECK(a == b).
//
// Helpers you can use (tests/test_helpers.h): TempDir, write_text,
// write_skill, FixedPolicy, ScriptedConfirmer, LinesInput, StringOutput,
// parse_ok, args1. ManualClock is in policy/policy.h.

#include "../mini_test.h"
#include "../test_helpers.h"

#include "memory/context_builder.h"
#include "memory/memory_index.h"
#include "memory/memory_store.h"
#include "policy/policies.h"
#include "policy/policy_chain.h"
#include "skills/dep_graph.h"
#include "skills/skill_catalog.h"
#include "skills/skill_index.h"
#include "util/lru_cache.h"
#include "util/min_heap.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>

// =========================================================================
// Memory — 4 examples
// =========================================================================

// Example 1 (heap): items come back smallest first, duplicates included.
TEST(heap_pops_in_order) {
    MinHeap<int> h;
    for (int x : {5, 1, 4, 1, 3}) h.push(x);
    CHECK_EQ(h.size(), std::size_t{5});
    CHECK_EQ(h.top(), 1);
    std::vector<int> got;
    while (!h.empty()) got.push_back(h.pop());
    CHECK_EQ(got, (std::vector<int>{1, 1, 3, 4, 5}));
}

// Example 2 (lru): the least recently USED entry is evicted, and get()
// counts as a use.
TEST(lru_evicts_least_recently_used) {
    LruCache<std::string, int> c(2);
    c.put("a", 1);
    c.put("b", 2);
    REQUIRE(c.get("a") != nullptr);   // "a" is now the most recent
    c.put("c", 3);                    // full: "b" must go
    CHECK(c.contains("a"));
    CHECK(!c.contains("b"));
    CHECK_EQ(c.keys_mru_to_lru(), (std::vector<std::string>{"c", "a"}));
    CHECK_EQ(c.stats().evictions, std::size_t{1});
}

// Example 3 (bst): removing a node with two children keeps the tree sorted,
// keeps every other record, and lowers size() by exactly one.
TEST(bst_remove_two_children) {
    MemoryIndex t;
    for (std::uint64_t id : {50, 30, 70, 60, 80}) REQUIRE(t.insert(MemoryRecord{id, "m" + std::to_string(id)}));
    REQUIRE(t.remove(70));
    std::vector<std::uint64_t> ids;
    t.for_each_in_order([&](const MemoryRecord& r) { ids.push_back(r.id); });
    CHECK_EQ(ids, (std::vector<std::uint64_t>{30, 50, 60, 80}));
    CHECK(t.find(70) == nullptr);
    REQUIRE(t.find(80) != nullptr);
    CHECK_EQ(t.find(80)->text, std::string("m80"));
    CHECK_EQ(t.size(), std::size_t{4});
}

// Example 4 (ctx): the worked example in spec §4.4.
TEST(ctx_spec_example) {
    std::vector<ContextItem> items{
        {Role::System, 10, false},     // 0  protected
        {Role::User, 10, false},       // 1
        {Role::Assistant, 10, false},  // 2  dropped 3rd (oldest assistant)
        {Role::Tool, 10, false},       // 3  dropped 1st
        {Role::Assistant, 10, false},  // 4
        {Role::Tool, 10, false},       // 5  dropped 2nd
        {Role::User, 10, false},       // 6  recent: protected
        {Role::Assistant, 10, false}}; // 7  recent: protected
    ContextPlan p = select_context(items, 50, 2);
    CHECK_EQ(p.keep, (std::vector<std::size_t>{0, 1, 4, 6, 7}));
    CHECK_EQ(p.total_tokens, std::size_t{50});
    CHECK(!p.over_budget);
}

// =========================================================================
// Policy — 2 examples
// =========================================================================

// Example 5 (pol): the sliding window. A request exactly `window` seconds
// after an allowed one no longer counts it.
TEST(pol_rate_limit_window_boundary) {
    ManualClock clock(100);
    RateLimitPolicy p(2, 10.0, clock);   // at most 2 per 10 s
    CommandRequest req;
    req.program = "ls";
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 100
    clock.advance(1);
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 101
    clock.advance(1);
    CHECK(p.evaluate(req).kind == Decision::Kind::Deny);    // t = 102: two in the window
    clock.advance(8);
    CHECK(p.evaluate(req).kind == Decision::Kind::Allow);   // t = 110: 100 has left (110 - 100 = 10)
    CHECK(p.tracked() <= 2u);
}

// Example 6 (pol): first Deny wins and later policies are not asked; with no
// Deny, the FIRST Ask is returned unchanged.
TEST(pol_chain_first_deny_stops_first_ask_wins) {
    auto ask1 = std::make_unique<FixedPolicy>(Decision::Kind::Ask, "ask1");
    auto ask2 = std::make_unique<FixedPolicy>(Decision::Kind::Ask, "ask2");
    PolicyChain chain;
    REQUIRE(chain.add(std::make_unique<FixedPolicy>(Decision::Kind::Allow, "ok")));
    REQUIRE(chain.add(std::move(ask1)));
    REQUIRE(chain.add(std::move(ask2)));
    CommandRequest req;
    req.program = "git";
    Decision d = chain.evaluate(req);
    CHECK(d.kind == Decision::Kind::Ask);
    CHECK_EQ(d.policy, std::string("ask1"));

    auto deny = std::make_unique<FixedPolicy>(Decision::Kind::Deny, "no");
    auto after = std::make_unique<FixedPolicy>(Decision::Kind::Allow, "after");
    FixedPolicy* after_ptr = after.get();   // still valid: the chain owns it now
    REQUIRE(chain.add(std::move(deny)));
    REQUIRE(chain.add(std::move(after)));
    d = chain.evaluate(req);
    CHECK(d.kind == Decision::Kind::Deny);
    CHECK_EQ(d.policy, std::string("no"));
    CHECK_EQ(after_ptr->calls, 0);     // never evaluated: the chain stopped at the Deny
}

// =========================================================================
// Skills — 2 examples
// =========================================================================

// Example 7 (graph): post-order load order, and a cycle message that lists
// only the nodes still on the DFS path (b is already finished).
TEST(graph_order_and_cycle_message) {
    DepGraph g;
    for (const char* n : {"a", "b", "c", "d"}) g.add_node(n);
    g.add_edge("a", "b");   // a requires b
    g.add_edge("a", "c");
    g.add_edge("b", "d");
    g.add_edge("c", "d");   // diamond: d is reached twice, but it is not a cycle
    std::vector<std::string> order;
    std::string error;
    REQUIRE(g.order_from("a", &order, &error));
    CHECK_EQ(order, (std::vector<std::string>{"d", "b", "c", "a"}));

    g.add_edge("c", "a");   // now a -> c -> a is a cycle
    CHECK(!g.order_from("a", &order, &error));
    CHECK_EQ(error, std::string("cycle: a -> c -> a"));
}

// Example 8 (index): rank by number of distinct query words; ties go to the
// smaller doc id.
TEST(index_ranking_and_ties) {
    SkillIndex ix;
    ix.add(0, "format numbers");
    ix.add(1, "csv report with table and numbers");
    ix.add(2, "table format");
    ix.add(3, "unrelated");
    CHECK_EQ(ix.search("csv table numbers", 10), (std::vector<std::size_t>{1, 0, 2}));
    CHECK_EQ(ix.search("format table", 1), (std::vector<std::size_t>{2}));
    REQUIRE(ix.postings("numbers") != nullptr);
    CHECK_EQ(*ix.postings("numbers"), (std::vector<std::size_t>{0, 1}));
}

// =========================================================================
// Your tests (at least 12 more). Spec §8 lists what they must cover.
// =========================================================================

//test 9
TEST(heap_empty) {
    MinHeap<int> heap;

    bool top_heap = false;
    try { 
        (void)heap.top(); 
    } 
    catch (const std::out_of_range&) {
        top_heap = true; 
    }

    CHECK(top_heap);

    bool pop_heap= false;
    try {
        (void)heap.pop(); 
    } 
    catch (const std::out_of_range&) { 
        pop_heap = true; 
    }
    CHECK(pop_heap);

    heap.push(67);
    CHECK_EQ(heap.pop(), 67);

    bool pop_done = false;
    try { (void)heap.pop(); 
    } 
    catch (const std::out_of_range&) { 
        pop_done = true; 
    }

    CHECK(pop_done);
}


//test 10
TEST(heap_topk) {
    std::vector<int> nums{2, 212, 5, 306, 309, 310, 564};
    
    CHECK_EQ(top_k(nums, 0), (std::vector<int>{}));
    CHECK_EQ(top_k(std::vector<int>{}, 3), (std::vector<int>{}));
    CHECK_EQ(top_k(nums, 2), (std::vector<int>{564, 310}));
    CHECK_EQ(top_k(nums, 10), (std::vector<int>{564, 310, 309, 306, 212, 5, 2}));
}

//test 11
TEST(heap_random) {
    constexpr std::size_t k1 = 100000;
    constexpr std::size_t k2 = 10;

    std::mt19937 rng(52);
    std::uniform_int_distribution<int> distribution(-1000000, 1000000);
    std::vector<int> data_vector(k1);
    for (int& x : data_vector) x = distribution(rng);

    auto start_1 = std::chrono::steady_clock::now();
    std::vector<int> actual = top_k(data_vector, k2);
    auto end_1 = std::chrono::steady_clock::now();

    std::vector<int> sorted = data_vector;
    auto start_2 = std::chrono::steady_clock::now();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>{});
    auto end_2 = std::chrono::steady_clock::now();

    std::vector<int> theoretical(sorted.begin(), sorted.begin() + k2);
    CHECK_EQ(actual, theoretical);

    double topk_ms = std::chrono::duration<double, std::milli>(end_1 - start_1).count();
    double sort_ms = std::chrono::duration<double, std::milli>(end_2- start_2).count();
    std::cout << "For n = " << k1 << " and k = " << k2 << " the top_k is " << topk_ms << " and the sort is " << sort_ms << ", which are both measured in milliseconds" << std::endl;
}

//test 12
TEST(lru_update) {
    LruCache<std::string, int> cache_sample_1(2);

    cache_sample_1.put("apple", 1);
    cache_sample_1.put("banana", 2);
    cache_sample_1.put("banana", 3);   

    CHECK_EQ(cache_sample_1.size(), std::size_t{2});
    CHECK_EQ(cache_sample_1.stats().evictions, std::size_t{0});

    REQUIRE(cache_sample_1.get("banana") != nullptr);   
    CHECK_EQ(*cache_sample_1.get("banana"), 3);      
    CHECK(cache_sample_1.get("67") == nullptr); 

    cache_sample_1.put("krillion", 4); 

    CHECK_EQ(cache_sample_1.keys_mru_to_lru(), (std::vector<std::string>{"krillion", "banana"}));
    CHECK_EQ(cache_sample_1.stats().hits, std::size_t{2});
    CHECK_EQ(cache_sample_1.stats().misses, std::size_t{1});
    CHECK_EQ(cache_sample_1.stats().evictions, std::size_t{1});
}

//test 13
TEST(lru_erase_and_empty_and_single) {
    LruCache<int, int> cache_sample_2(4);
    cache_sample_2.put(1, 109);
    cache_sample_2.put(2, 200);
    cache_sample_2.put(3, 301);  

    CHECK(cache_sample_2.erase(2));                                        
    CHECK_EQ(cache_sample_2.keys_mru_to_lru(), (std::vector<int>{3,1}));

    cache_sample_2.put(4, 492);

    CHECK(cache_sample_2.erase(4));                                        
    CHECK_EQ(cache_sample_2.keys_mru_to_lru(), (std::vector<int>{3, 1}));

    CHECK(cache_sample_2.erase(1));                                        
    CHECK_EQ(cache_sample_2.keys_mru_to_lru(), (std::vector<int>{3}));
    CHECK(!cache_sample_2.erase(99));                                     

    LruCache<int, int> empty(0);                               
    empty.put(1, 109);
    CHECK_EQ(empty.size(), std::size_t{0});

    LruCache<int, int> single(1);                              
    single.put(1, 93);
    single.put(2, 1);
    CHECK(!single.contains(1));
    CHECK(single.contains(2));
}

TEST(bst_remove_reject){
    MemoryIndex tree;

    std::vector<std::uint64_t> nums{212, 5, 306, 309, 310, 564};
    for (std::uint64_t num : nums) {
        REQUIRE(tree.insert(MemoryRecord{num, "Entry "}));
    }

    CHECK(!tree.remove(511));
    REQUIRE(tree.remove(564));
    REQUIRE(tree.remove(309));
    REQUIRE(tree.remove(212));

    CHECK_EQ(tree.size(), std::size_t{3});
}



TEST(bst_dup_hand_clear){
    MemoryIndex tree;
    CHECK_EQ(tree.height(), std::size_t{0});

    REQUIRE(tree.insert(MemoryRecord{250,"newEntry"}));
    CHECK_EQ(tree.height(), std::size_t{1});
    CHECK(!tree.insert(MemoryRecord{250,"Same"}));
    CHECK_EQ(tree.size(), std::size_t{1});

    std::vector<std::uint64_t> nums{2, 212, 5, 306, 309, 310, 564};
    for (std::uint64_t num: nums){   
        REQUIRE(tree.insert(MemoryRecord{num,"Entry "}));
    }

    CHECK_EQ(tree.height(),std::size_t{5});
    CHECK(tree.find(511) == nullptr);

    tree.clear();
    CHECK_EQ(tree.size(), std::size_t{0});
    CHECK_EQ(tree.height(), std::size_t{0});


}

TEST(bst_1000_shuffle_seq){
    MemoryIndex tree;
    for(std::size_t id = 1; id <= 1000; id++){
        REQUIRE(tree.insert(MemoryRecord{id, "New"}));
    }

    CHECK_EQ(tree.height(),std::size_t{1000});

    std::vector<int> idNums(1000);
    for (std::size_t i = 1; i <= 1000; i++){
        idNums[i-1] = i;
    }

    std::mt19937 rng(511);
    std::shuffle(idNums.begin(),idNums.end(), rng);

    MemoryIndex tree_2;

    for (std::size_t id: idNums){
        REQUIRE(tree_2.insert(MemoryRecord{id, "NewEd"}));
    }

    CHECK(tree_2.height() < tree.height());

    std::cout << "Sequential height is " << tree.height() << " and shuffled height is " << tree_2.height() << std::endl;
}

TEST(ctx_overbudget){
    std::vector<ContextItem> c_items{{Role::System,100,false},{Role::Tool,50,false},{Role::User,67,true},{Role::User,12,false},{Role::Tool,100,true}};

    ContextPlan plan = select_context(c_items,200,2);
    CHECK_EQ(plan.keep, (std::vector<std::size_t>{0,2,3,4}));
    CHECK_EQ(plan.total_tokens,std::size_t{279});
    CHECK(plan.over_budget);

}

TEST(ctx_within_budget){
    std::vector<ContextItem> c_items{{Role::System,100,false},{Role::System,50,true},{Role::User,67,true},{Role::User,12,false},{Role::Tool,100,true}};
    ContextPlan plan = select_context(c_items,350,0);
    CHECK_EQ(plan.keep,(std::vector<std::size_t>{0,1,2,3,4}));
    CHECK_EQ(plan.total_tokens,std::size_t{329});
    CHECK(!plan.over_budget);

    ContextPlan plan_2 = select_context(c_items,10,10);
    CHECK_EQ(plan_2.keep, (std::vector<std::size_t>{0,1,2,3,4}));
    CHECK_EQ(plan_2.total_tokens, std::size_t{329});
    CHECK(plan_2.over_budget);

}


int main(int argc, char** argv) { return mt::run_all(argc, argv); }