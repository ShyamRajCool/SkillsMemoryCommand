// src/memory/memory_index.cpp — YOU IMPLEMENT. Spec §4.3.

#include "memory/memory_index.h"



void MemoryIndex::destroy(Node* n) { //recursive delete
    if (n != nullptr) {
        destroy(n->left);
        destroy(n->right);
        delete n;
    }
    else{
        return;
    }

}

std::size_t MemoryIndex::height_of(const Node* n) { //height formula followed
    std::size_t bottom_layer = 0;
    if (n != nullptr) {
        std::size_t l_height = height_of(n->left);
        std::size_t r_height = height_of(n->right);
        if (l_height > r_height){
            return l_height + 1;
        }
        else{
            return r_height + 1;
        }
    }
    else{
        return bottom_layer;
    }
}

void MemoryIndex::in_order(const Node* n, const std::function<void(const MemoryRecord&)>& fn) { //in order traversal followed
                                                                                                //left -> root -> right
    if (n != nullptr) {
        in_order(n->left, fn);
        fn(n->rec);
        in_order(n->right, fn);
    }
    else{
        return;
    }

}

bool MemoryIndex::Case1(Node* parent, Node* node){ //leaf node is removed
    if (node->left == nullptr && node -> right == nullptr){
        if (parent == nullptr){
            root_ = nullptr; 
        }
        else if(parent -> left == node){
            parent -> left = nullptr; //parent node left null

        }
        else{
            parent -> right = nullptr; //parent node right null
        }
        return true;
    }
    else{
        return false;
    }
}

bool MemoryIndex::Case2(Node* parent, Node* node){ //internal node with a single child

    if (node-> left != nullptr && node-> right == nullptr){ //node has left child
        if (parent == nullptr){ //node child new root
            root_ = node -> left;
        }
        else if (parent -> left == node){//child on left
            parent -> left = node -> left;
        }
        else{//child on right
            parent -> right = node -> left;
        }
        return true;
    }
    else if (node -> left == nullptr && node -> right != nullptr){ //node has right child
        if (parent == nullptr){//node child root
            root_ = node -> right;
        }
        else if (parent -> left == node){//node on left
            parent -> left = node -> right;
        }
        else{//node on right
            parent -> right = node -> right;
        }
        return true;
    }
    return false;
}

void MemoryIndex::Case3(Node* parent, Node* node){
    Node* succ = node -> right;
    Node* prev = node;
    
    while (succ -> left != nullptr){ //leftmost node is searched for
        prev = succ;
        succ = succ -> left;
    }

    if (!Case1(prev,succ)){
        Case2(prev,succ);
    }

    succ -> left = node -> left;
    succ -> right = node -> right; //node with successor, left, right, and parent are updated.
    if (parent == nullptr){
        root_ = succ;
    }
    else if (parent -> left == node){
        parent -> left = succ;
    }
    else{
        parent -> right = succ;
    }
}

MemoryIndex::~MemoryIndex() {
    // TODO: free every node (post-order: both children before the node).
    destroy(root_); //it is just recursion
}

bool MemoryIndex::insert(MemoryRecord rec) {
    // TODO: walk down from root_ (smaller ids left, larger right). Equal id:
    // return false and change nothing. Otherwise attach a new Node where the
    // walk fell off the tree, ++count_, return true.
    // Clue: memory ids arrive as 1, 2, 3, ..., so the tree is one long right
    // spine. A loop handles 5,000 nodes easily; so does recursion.

    if (root_ == nullptr) { 
        root_ = new Node{std::move(rec), nullptr, nullptr}; //insert root
        count_ = count_ + 1;
        return true;
    }
    else{
        Node* curr = root_;
        bool sentinel_loop = true;
        while (sentinel_loop) {
            if (rec.id == curr->rec.id) {
                return false;                                
            }
            if (rec.id < curr->rec.id) {
                if (curr->left == nullptr) { //searching must be stopped as no more and can be placed
                    curr->left = new Node{std::move(rec), nullptr, nullptr};
                    sentinel_loop = false;
                }
                else{
                    curr = curr->left;
                }
            }
            else {
                if (curr->right == nullptr) { //searching must be stopped as no more and can be placed
                    curr->right = new Node{std::move(rec), nullptr, nullptr};
                    sentinel_loop = false;
                }
                else{
                    curr = curr->right;
                }
            }
        }
        count_ = count_ + 1;
        return true;
    }
}

const MemoryRecord* MemoryIndex::find(std::uint64_t id) const {
    const Node* node = root_;
    while (node != nullptr) {
        if (node->rec.id == id) {
            return &node->rec;
        }
        if (id < node->rec.id) {
            node = node->left;
        }
        else {
            node = node->right;
        }
    }
    return nullptr; //basically false
}

bool MemoryIndex::remove(std::uint64_t id) {
    // TODO: leaf, one child, two children (use the in-order successor or
    // predecessor). Spec Appendix C draws all three.
    // Clue: the successor (leftmost node of the right subtree) has no left
    // child but MAY have a right child: that child takes its place.
    // Clue: --count_ exactly once per successful remove. Counting inside
    // the helper AND here double-counts the two-children case.
    // Clue: when the removed node is the root, root_ itself must change.
    Node* node = root_;
    Node* parent = nullptr;      
    bool removeCase1;
    bool removeCase2;

    while (node != nullptr) {
        if (node->rec.id == id) { //if found, then appropriate case must be used to safely join together BST after removal
            removeCase1 = Case1(parent,node);
            if (removeCase1 == false){
                removeCase2 = Case2(parent,node);
                if (removeCase2 == false){
                    Case3(parent,node);
                }
            }
            delete node;
            count_ = count_ - 1;              
            return true;
        } 
        else if (id < node->rec.id) { //similar to BST find
            parent = node;
            node = node->left;
        }
        else { //similar to BST find
            parent = node;
            node = node->right;
        }
    }
    return false;   

}

std::size_t MemoryIndex::size() const noexcept { return count_; }

std::size_t MemoryIndex::height() const {
    // TODO: empty tree = 0, one node = 1, otherwise 1 + the taller child.
    return height_of(root_);
}

void MemoryIndex::for_each_in_order(const std::function<void(const MemoryRecord&)>& fn) const {
    // TODO: left subtree, then fn(node->rec), then right subtree.

    in_order(root_,fn);
}

void MemoryIndex::clear() {
    // TODO
    // Clue: after freeing the nodes, set root_ = nullptr and count_ = 0, or
    // the destructor frees them a second time.
    destroy(root_);
    root_ = nullptr;
    count_ = 0;
}
