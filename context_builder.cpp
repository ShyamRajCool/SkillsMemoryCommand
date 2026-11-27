// src/memory/context_builder.cpp — YOU IMPLEMENT. Spec §4.4.

#include "memory/context_builder.h"
#include "util/min_heap.h"

ContextPlan select_context(const std::vector<ContextItem>& items,
                           std::size_t budget, std::size_t keep_recent) {
    // TODO: total the tokens; if they fit, keep everything. Otherwise push
    // every unprotected item onto a MinHeap keyed on (drop_rank(role), index)
    // and pop/drop until the total fits or the heap is empty.
    //
    // Clues:
    //  * Heap entries:  MinHeap<std::pair<int, std::size_t>> heap;
    //    std::pair compares .first, then .second, which is exactly
    //    "lowest rank first, then oldest first".
    //  * "The last keep_recent items" start at index items.size() - keep_recent,
    //    but these are unsigned: if keep_recent > items.size() that
    //    subtraction wraps around to a huge number. Handle that case first.
    //  * A vector<bool> keep(items.size(), true) makes the final pass easy:
    //    collect the indices still marked true, in ascending order.
    //  * total_tokens is the sum of the KEPT items; over_budget is
    //    total_tokens > budget.


    std::size_t total_items = items.size();

    std::size_t total = 0;

    for (const ContextItem& item: items){
        total+= item.tokens; //Total tokens is obtained to be compared with later
    }

    std::vector<bool> keep_context(total_items, true); //holder

    if (total > budget){
        std::size_t first = 0;
        if (keep_recent < total_items){ //valid keep recent checked
            first = total_items - keep_recent;
        }

        MinHeap<std::pair<int, std::size_t>> heap;
        for (std::size_t i = 0; i < total_items; i++) {
            bool system = items[i].role == Role::System;
            bool recent = i >= first;
            bool protected_id =  system || items[i].pinned || recent; //unprotected item checker
            if (!protected_id) {
                heap.push(std::make_pair(drop_rank(items[i].role), i)); //make pair needed for unprotected item
            }
        }



        bool heap_check = false;
        while (total > budget && !heap_check) {//pop until fits or empty
            if (heap.empty()){
                heap_check = true;
            }
            else{
                std::size_t i = heap.pop().second;
                keep_context[i] = false;
                total = total - items[i].tokens;
            }
        }
    }

    ContextPlan output_plan;

    for (std::size_t i = 0; i < total_items; i++){
        if (keep_context[i] == true){
            output_plan.keep.push_back(i);
        }
    }

    output_plan.total_tokens = total;
    output_plan.over_budget = total > budget;

    return output_plan;
}
