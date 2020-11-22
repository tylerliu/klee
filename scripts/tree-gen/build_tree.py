#!/usr/bin/env python
# vim: set ts=2 sts=2 sw=2 et si tw=80:

import sys
import string
import os
import math
import operator
import subprocess
import delegator

# https://anytree.readthedocs.io/en/latest/index.html
from anytree import NodeMixin, RenderTree, PostOrderIter, LevelOrderIter
from anytree.search import find, findall_by_attr
from anytree.exporter import DotExporter

# For reasoning about formulae
from sympy import *
from sympy.parsing.sympy_parser import parse_expr

sys.setrecursionlimit(1000000)

tags_file = sys.argv[1]
perf_file = sys.argv[2]
formula_file = sys.argv[3]
perf_metric = sys.argv[4]
tree_type = sys.argv[5]
tree_file = sys.argv[6]
constraint_file = sys.argv[7]
max_perf = int(sys.argv[8])
min_perf = int(sys.argv[9])
constraint_node = sys.argv[10]

expected_perf = (max_perf+min_perf)/2
perf_resolution = (max_perf-min_perf)/2


class Constraint:

    def __init__(self, subject=None, sind=None):
        self.subject = subject  # Main clause of constraint
        self.sind = sind  # Used to indicate if first or second child follows true branch


class MyNode(Constraint):

    def __init__(self, name, id, depth):
        self.name = name
        self.id = id
        self.depth = depth  # Depth of root is -1.
        self.tags = []
        self.max_perf = -1
        self.min_perf = -1
        self.formula = set()
        self.sub_tests = []
        self.constraints = Constraint()
        self.is_true = -1
        self.merge_res = float('inf')
        self.merge_type = ""

    # TODO:Figure out why this is necessary.Code fails without it
    # Think it is required to override similar field in NodeMixin. Not sure.

    @property
    def depth(self):
        return self._depth

    @depth.setter
    def depth(self, value):
        self._depth = value


class TreeNode(MyNode, NodeMixin):
    def __init__(self, name, id, depth, parent=None, children=None):
        MyNode.__init__(self, name, id, depth)
        self.parent = parent
        if children:
            self.children = children


traces_perf = {}
traces_tags = {}
traces_perf_formula = {}
leaf_tags = list()
all_tag_prefixes = list()
perf_var = {}
perf_formula_var = {}
merged_tuples = list()
merge_res = 0
prefix_match_lengths = [[]]
prefix_branch_constraints = [[[]]]
constraint_thresholds = {}
loop_pcv_violations = set()
branch_pcv_violations = {}
cliff_res = set()

tree_root = TreeNode("ROOT", -1, -1)

# Setup for formula interpretation
e, c, t = symbols("e c t")  # Loop PCVs
common_vals = [(e, 0), (t, 1), (c, 0)]
extreme_vals = [(e, 65536), (t, 65536), (c, 65535)]
# Hack
loop_pcv_root_cause = "e,c,t <= 65536"


def main():
    global tree_root

    setup()
    build_basic_tree(tree_file, tree_root)

    if(tree_type == "neg-tree"):
        process_neg_tree()
    else:
        process_res_tree()

    if(constraint_node != "none"):
        debug_constraints(constraint_node)


def setup():
    get_traces_perf()
    get_traces_tags()


def build_basic_tree(tree_file, tree_root):
    build_tree(tree_file)
    assign_tree_constraints(tree_root)
    assign_tree_tags_and_perf(tree_root)


def process_res_tree():
    global tree_root
    global cliff_res
    global perf_resolution
    find_merge_resolutions(tree_root)
    cliff_res.add(0)
    while(len(cliff_res)):
        res = min(cliff_res)
        perf_resolution = res
        merge_nodes_at_res(tree_root, res)
        cliff_res.remove(res)
        # Need to do this repeatedly, since nodes change
        find_merge_resolutions(tree_root)
        cliff_res = get_cliff_points(tree_root)
        if(len(cliff_res) == 0 or min(cliff_res) > res):
            pretty_print_tree(tree_root)
            print_tree_image(tree_root)


def process_neg_tree():
    global tree_root
    deleted_nodes = list()
    deleted_nodes = remove_sat_nodes(tree_root)

    # Re-assigning formulae since certain nodes might have been deleted
    for node in list(LevelOrderIter(tree_root)):
        for sub_node in node.sub_tests:
            if(sub_node in traces_perf_formula and sub_node not in deleted_nodes):
                node.formula.add(traces_perf_formula[sub_node])

    tree_root = remove_spurious_branching(tree_root)
    tree_root = coalesce_constraints(tree_root)
    pretty_print_tree(tree_root)
    print_tree_image(tree_root)


def print_tree_image(root):
    if(tree_type == "neg-tree"):
        tree_file_name = tree_type+".dot"
    else:
        tree_file_name = tree_type+"-%d.dot" % (perf_resolution)
    DotExporter(root,
                nodenamefunc=node_identifier_fn,
                nodeattrfunc=node_colour_fn).to_dotfile(tree_file_name)


def debug_constraints(node_name):
    global tree_root
    node = find(tree_root, lambda node: node.name == node_name)
    pretty_print_constraints(node)


def merge_nodes_at_res(root, res):
    # TODO: Lots of code duplication in below functions. Need to refactor.
    merge_nodes_simple(root, res)
    # Need to do this repeatedly, since nodes change
    find_merge_resolutions(root)
    merge_nodes_spurious(root, res)
    # Need to do this repeatedly, since nodes change
    find_merge_resolutions(root)
    merge_nodes_cc(root, res)


def merge_nodes_simple(root, res):
    for node in list(LevelOrderIter(root))[:]:
        if((not node == root) and (node.parent != None) and (not node.is_leaf)):
            if(node.merge_res <= res and node.merge_type == "simple"):
                children = list(node.children)
                for child in children:
                    child.parent = None
                node.constraints = Constraint()  # Because it is now a leaf
                node.merge_res = float('inf')  # Because it is now a leaf


def merge_nodes_spurious(root, res):
    for node in list(LevelOrderIter(root))[:]:
        if((not node == root) and (node.parent != None) and (not node.is_leaf)):
            if(node.merge_res <= res and node.merge_type == "spurious"):
                children = list(node.children)
                assert(get_merge_res_wrapper(children[0], children[1]))
                for pair in merged_tuples:
                    # Sanity check that the tuple always has a node from child zero and then child one.
                    # Mostly redundant, but left here in any case
                    assert(len(findall_by_attr(children[0], pair[0], name='name')) == 1
                           and len(findall_by_attr(children[1], pair[1], name='name')) == 1)
                    final = findall_by_attr(
                        children[0], pair[0], name='name')[0]
                    merged_in = findall_by_attr(
                        children[1], pair[1], name='name')[0]
                    merge_nodes(final, merged_in)
                children[1].parent = None
                children[0].parent = node.parent
                children[0].is_true = node.is_true
                node.parent = None


def merge_nodes_cc(root, res):
    for node in list(LevelOrderIter(root))[:]:
        if((not node == root) and (node.parent != None) and (not node.is_leaf)):
            if(node.merge_res <= res and node.merge_type == "cc"):
                children = list(node.children)
                # This is the dad, since we are going in LevelOrder Iter
                dad = node
                grandad = node.parent
                assert(len(dad.siblings) == 1)
                uncle = list(dad.siblings)[0]
                nephew = None
                neice = None
                if(compare_node_constraints(children[0], uncle)):
                    nephew = children[0]
                    if(len(children) > 1):
                        neice = children[1]
                else:
                    nephew = children[1]
                    if(len(children) > 1):
                        neice = children[0]

                assert(get_merge_res_wrapper(uncle, nephew))
                for pair in merged_tuples:
                    # Sanity check that the tuple always has a node from uncle and then nephew.
                    # Mostly redundant, but left here in any case
                    assert(len(findall_by_attr(uncle, pair[0], name='name')) == 1
                           and len(findall_by_attr(nephew, pair[1], name='name')) == 1)
                    final = findall_by_attr(
                        uncle, pair[0], name='name')[0]
                    merged_in = findall_by_attr(
                        nephew, pair[1], name='name')[0]
                    merge_nodes(final, merged_in)

                # Modify the constraints in the current node
                curr_subj = grandad.constraints.subject
                merged_in_subj = dad.constraints.subject
                uncle_ind = list(grandad.children).index(uncle)
                if(uncle.is_true):
                    conjunction = "Or"
                    new_sind = uncle_ind
                else:
                    conjunction = "And"
                    new_sind = (uncle_ind+1) % 2

                if((uncle.is_true + nephew.is_true) % 2 == 1):
                    if(neice != None):
                        neice.is_true = (neice.is_true+1) % 2
                    merged_in_subj = "(Eq false " + merged_in_subj

                final_subj = "(" + conjunction + " w32 " + \
                    curr_subj + " " + merged_in_subj + ")"
                grandad.constraints = Constraint(final_subj, new_sind)

                # Fix the branching
                dad.parent = None
                nephew.parent = None
                if(neice != None):
                    neice.parent = grandad


def coalesce_within_resolution(root):
    for node in list(PostOrderIter(root)):
        if(not node.is_leaf and can_merge_perf(node.max_perf, node.min_perf)):
            for child in list(node.children):
                child.parent = None
            node.constraints = Constraint()  # Because it is now a leaf


def get_cliff_points(root):
    cliff_points = set()
    if(not list(root.children)[0].is_leaf):
        for node in list(PostOrderIter(root)):
            if(not node.is_leaf):
                cliff_points.add(node.merge_res)
    return cliff_points


def remove_sat_nodes(root):
    global loop_pcv_violations
    deleted_nodes = []
    # Finding violations due to loop PCVs:
    for node in list(PostOrderIter(root)):
        if(node.is_leaf):
            perf = parse_expr(
                traces_perf_formula[node.name]).subs(extreme_vals)
            if(not perf_within_envelope(perf) and ("*" in traces_perf_formula[node.name])):
                loop_pcv_violations.add(
                    traces_perf_formula[node.name])
    loop_pcv_violations = merge_formulae(loop_pcv_violations)

    # Finding nodes that fall within the envelope and deleting them
    for node in list(LevelOrderIter(root))[:]:
        if((not node.is_leaf and perf_within_envelope(node.max_perf) and perf_within_envelope(node.min_perf)) or (node.is_leaf and perf_within_envelope(node.max_perf))):
            node.parent = None
            deleted_nodes.append(node.name)

    # Now we simplify the remaining nodes by getting rid of subtrees entirely
    for node in list(LevelOrderIter(root))[:]:
        if((not node.is_leaf) and (not bool(set(node.sub_tests) & set(deleted_nodes)))):
            node.constraints = Constraint()
            for child in list(node.children):
                child.parent = None

    return deleted_nodes


def basic_cleanup_tree(root):
    # Removes nodes with no siblings.
    for node in list(PostOrderIter(root))[:]:
        if(node.parent == None):  # Some nodes get detached during this process
            continue
        while(len(node.siblings) == 0):
            # We do not want to coalesce the root or the first node
            if(node.parent == root or node == root):
                break
            else:
                curr_parent = node.parent
                new_parent = curr_parent.parent
                node.parent = new_parent
                curr_parent.parent = None


def pretty_print_neg_tree(root):
    global branch_pcv_violations
    global loop_pcv_violations
    # Now we put together branch PCV violations in a user-friendly format
    file_name = "neg-tree-%d.txt" % (perf_resolution)
    for node in PostOrderIter(tree_root):
        if(node.is_leaf):
            node.formula = merge_formulae(node.formula)
            node.depth = get_node_depth(node)  # Reusing depth
            for f in node.formula:
                present = 0
                for formula, nodes in branch_pcv_violations.items():
                    if(are_similar_formulae({formula, f})):
                        if(node.depth in nodes):
                            nodes[node.depth].add(node.name)
                        else:
                            nodes[node.depth] = {node.name}
                        present = 1
                        break
                if(not present):
                    temp = {}
                    temp[node.depth] = {node.name}
                    branch_pcv_violations[f] = temp

    for formula, node_list in branch_pcv_violations.items():
        f = open(file_name, "a+")
        f.write("\n*** Violating Formula(e) *** \n %s\n" % (formula))
        f.close()
        trees_for_formula = {}
        for depth, nodes in node_list.items():
            paths = []
            for node in nodes:
                node_path = get_node_path(node, root)
                paths.append((node, node_path))

            depth_root = build_path_tree(paths)
            trees_for_formula[depth] = depth_root
            print_tree(depth_root, file_name)

    # Printing loop PCV violations
    if(len(loop_pcv_violations)):
        f = open(file_name, "a+")
        f.write("\n*** Violating Formula(e) *** \n\n")
        for formula in loop_pcv_violations:
            f.write(formula+"\n")
        f.write("Cause(s) of violation\n%s" % (loop_pcv_root_cause))
        f.close()


def pretty_print_tree(root):
    if(tree_type == "neg-tree"):
        file_name = "%s.txt" % (tree_type)
    else:
        file_name = "%s-%d.txt" % (tree_type, perf_resolution)
    nodes = list(node.name for node in PostOrderIter(
        tree_root) if node.is_leaf)
    paths = list()
    # for node in nodes:
    #     node_path = get_node_path(node, root)
    #     paths.append((node, node_path))
    # depth_root = build_path_tree(paths)
    print_tree(root, file_name)
    if(tree_type == "neg-tree"):
        # Printing loop PCV violations
        if(len(loop_pcv_violations)):
            loop_violations_file = "%s-loop" % (tree_type)
            f = open(loop_violations_file, "w")
            for formula in loop_pcv_violations:
                f.write(formula+"\n")
            f.write("Cause(s) of violation\n%s" % (loop_pcv_root_cause))
            f.close()


def build_path_tree(paths_list):
    # Setup
    root_node = TreeNode("depth_root", -1, -1)
    node_ctr = 0
    temp = TreeNode("", -1, -1)
    temp.name = "Node%d" % (node_ctr)
    temp.parent = root_node
    temp.constraints = Constraint("", -1)
    temp.is_true = 1
    node_ctr = node_ctr + 1

    for path_tuple in paths_list:
        path = path_tuple[1]
        sub_root = root_node
        prev_is_true = 1
        for c in path:
            children = list(sub_root.children)
            next_node = [x for x in children if (
                x.is_true == prev_is_true)]
            assert(len(next_node) == 1)
            sub_root = next_node[0]
            if(not(sub_root.constraints.subject ==
                   "" or sub_root.constraints.subject == c.subject)):
                print(sub_root.constraints.subject)
                print(c.subject)
            sub_root.constraints.subject = c.subject
            children = list(sub_root.children)
            next_node = [x for x in children if (
                x.is_true == c.sind)]
            assert(len(next_node) <= 1)
            if(not len(next_node)):
                temp = TreeNode("", -1, -1)
                temp.name = "Node%d" % (node_ctr)
                temp.parent = sub_root
                temp.constraints = Constraint("", -1)
                temp.is_true = c.sind
                node_ctr = node_ctr + 1

            prev_is_true = c.sind
        # Last node has the same name as the original node
        temp.name = path_tuple[0]
        reflection_node = findall_by_attr(tree_root, temp.name, name='name')
        assert(len(reflection_node) == 1 and "Node not found")
        reflection_node = reflection_node[0]
        temp.max_perf = reflection_node.max_perf
        temp.min_perf = reflection_node.min_perf

    for node in list(PostOrderIter(root_node)):
        if(not node.is_leaf):
            node.max_perf, node.min_perf = get_perf_variability(node)

    root_node = remove_spurious_branching(root_node)
    root_node = coalesce_constraints(root_node)

    return root_node


def get_node_path(node_name, root):
    node = findall_by_attr(root, node_name, name='name')
    assert(len(node) == 1 and "Node not found")
    node = node[0]
    constraint_path = list()
    if(node == root or node.parent == root):
        constraint_path.insert(0, Constraint("1", 1))
        return constraint_path

    while(1):
        parent = node.parent
        if(parent == root):
            break
        constraint_path.insert(0, Constraint(
            parent.constraints.subject, node.is_true))
        node = parent

    return constraint_path


def print_neg_tree(root, op_file):
    with open("op_file", "a+") as op:
        leaves = [node for node in list(LevelOrderIter(root)) if node.is_leaf]
        for leaf in leaves:
            s = ""
            leaf_path = get_node_path(leaf.name, root)
            s = "Cause(s) of violation:" + "\n"
            for c in leaf_path:
                if(c.sind):
                    prefix = "(Eq true "
                else:
                    prefix = "(Eq false "
                s = s + prefix + c.subject + ") **AND** "
            s = s[0: (len(s)-len(" **AND** "))]  # Remove last and
            op.write(s+"\n")


def print_res_tree(root, op_file):
    with open(op_file, "a+") as op:
        leaves = [node for node in list(LevelOrderIter(root)) if node.is_leaf]
        for leaf in leaves:
            s = ""
            leaf_path = get_node_path(leaf.name, root)
            for c in leaf_path:
                if(c.sind):
                    prefix = "(Eq true "
                else:
                    prefix = "(Eq false "
                s = s + prefix + c.subject + ") **AND** "
            s = s[0: (len(s)-len(" **AND** "))]  # Remove last and
            s = s+"," + "%d" % (int((leaf.max_perf + leaf.min_perf)/2))
            op.write(s+"\n")


def print_tree(root, op_file):
    if(tree_type == "neg_tree"):
        print_neg_tree(root, op_file)
    else:
        print_res_tree(root, op_file)


def get_node_depth(node):
    global tree_root
    depth = -1
    if(node == tree_root):
        return -1

    while(1):
        parent = node.parent
        assert(parent != None)
        depth = depth+1
        if(parent == tree_root):
            break
        node = parent
    return depth


def coalesce_constraints(root):
    for node in PostOrderIter(root):
        children = list(node.children)
        if(not node.is_leaf and not node.is_root and len(children) == 2):
            grandchildren_1 = list(children[0].children)
            grandchildren_2 = list(children[1].children)
            # Now constraint coalescing is possible
            # Have to have atleast 3 grandchildren
            if(len(grandchildren_1) + len(grandchildren_2) > 2):
                uncle = None
                dad = None
                nephew = None
                neice = None

                # If merging works, uncle == nephew. Hence, dad, nephew are lost and neice gets promoted.

                if(compare_trees_wrapper(children[0], grandchildren_2[0])):
                    uncle = children[0]
                    dad = children[1]
                    nephew = grandchildren_2[0]
                    if(len(grandchildren_2) > 1):
                        neice = grandchildren_2[1]
                elif(compare_trees_wrapper(children[1], grandchildren_1[0])):
                    uncle = children[1]
                    dad = children[0]
                    nephew = grandchildren_1[0]
                    if(len(grandchildren_1) > 1):
                        neice = grandchildren_1[1]
                elif(len(grandchildren_2) > 1 and compare_trees_wrapper(children[0], grandchildren_2[1])):
                    uncle = children[0]
                    dad = children[1]
                    nephew = grandchildren_2[1]
                    neice = grandchildren_2[0]

                elif(len(grandchildren_1) > 1 and compare_trees_wrapper(children[1], grandchildren_1[1])):
                    uncle = children[1]
                    dad = children[0]
                    nephew = grandchildren_1[1]
                    neice = grandchildren_1[0]

                if(uncle != None):  # Merging the two nodes
                    for pair in merged_tuples:
                        # Sanity check that the tuple always has a node from uncle and then nephew.
                        # Mostly redundant, but left here in any case
                        assert(len(findall_by_attr(uncle, pair[0], name='name')) == 1
                               and len(findall_by_attr(nephew, pair[1], name='name')) == 1)
                        final = findall_by_attr(
                            uncle, pair[0], name='name')[0]
                        merged_in = findall_by_attr(
                            nephew, pair[1], name='name')[0]
                        merge_nodes(final, merged_in)

                    # Modify the constraints in the current node
                    curr_subj = node.constraints.subject
                    merged_in_subj = dad.constraints.subject
                    uncle_ind = children.index(uncle)
                    if(uncle.is_true):
                        conjunction = "Or"
                        new_sind = uncle_ind
                    else:
                        conjunction = "And"
                        new_sind = (uncle_ind+1) % 2

                    if((uncle.is_true + nephew.is_true) % 2 == 1):
                        if(neice != None):
                            neice.is_true = (neice.is_true+1) % 2
                        merged_in_subj = "(Eq false " + merged_in_subj

                    final_subj = "(" + conjunction + " w32 " + \
                        curr_subj + " " + merged_in_subj + ")"
                    node.constraints = Constraint(final_subj, new_sind)

                    # Fix the branching
                    dad.parent = None
                    nephew.parent = None
                    if(neice != None):
                        neice.parent = node
    return root


def remove_spurious_branching(root):
    # Removes spurious, perf-unrelated branching
    for node in list(LevelOrderIter(root))[:]:
        children = list(node.children)
        if(node.parent != None and len(children) == 2 and (not children[0].is_leaf) and compare_trees_wrapper(children[0], children[1])):
            for pair in merged_tuples:
                # Sanity check that the tuple always has a node from child zero and then child one.
                # Mostly redundant, but left here in any case
                assert(len(findall_by_attr(children[0], pair[0], name='name')) == 1
                       and len(findall_by_attr(children[1], pair[1], name='name')) == 1)
                final = findall_by_attr(
                    children[0], pair[0], name='name')[0]
                merged_in = findall_by_attr(
                    children[1], pair[1], name='name')[0]
                merge_nodes(final, merged_in)
            children[1].parent = None
            children[0].parent = node.parent
            children[0].is_true = node.is_true
            node.parent = None
    return root


def find_merge_resolutions(root):
    clear_merge_resolutions(root)
    for node in list(LevelOrderIter(root)):
        assign_simple_res(node)
        assign_spurious_branching_res(node)
        assign_constraint_coalescing_res(node)


def clear_merge_resolutions(root):
    for node in list(LevelOrderIter(root)):
        node.merge_res = float('inf')
        node.merge_type = ""


def assign_simple_res(node):
    if(not node.is_leaf):
        res = (node.max_perf - node.min_perf) + 1
        if(node.merge_res > res):
            node.merge_res = res
            node.merge_type = "simple"


def assign_spurious_branching_res(node):
    children = list(node.children)
    if(node.parent != None and len(children) == 2 and (not children[0].is_leaf)):
        res = get_merge_res_wrapper(children[0], children[1])
        if(res > 0 and merge_res < node.merge_res):
            node.merge_res = merge_res
            node.merge_type = "spurious"


# TODO: Duplicated code from coalesce_constraints
def assign_constraint_coalescing_res(node):
    children = list(node.children)
    if(not node.is_leaf and not node.is_root and len(children) == 2):
        grandchildren_1 = list(children[0].children)
        grandchildren_2 = list(children[1].children)
        # Now constraint coalescing is possible
        # Have to have atleast 3 grandchildren
        if(len(grandchildren_1) + len(grandchildren_2) > 2):
            uncle = None
            dad = None
            nephew = None
            neice = None

            # If merging works, uncle == nephew. Hence, dad, nephew are lost and neice gets promoted.

            if(get_merge_res_wrapper(children[0], grandchildren_2[0])):
                uncle = children[0]
                dad = children[1]
                nephew = grandchildren_2[0]
                if(len(grandchildren_2) > 1):
                    neice = grandchildren_2[1]
            elif(get_merge_res_wrapper(children[1], grandchildren_1[0])):
                uncle = children[1]
                dad = children[0]
                nephew = grandchildren_1[0]
                if(len(grandchildren_1) > 1):
                    neice = grandchildren_1[1]
            elif(len(grandchildren_2) > 1 and get_merge_res_wrapper(children[0], grandchildren_2[1])):
                uncle = children[0]
                dad = children[1]
                nephew = grandchildren_2[1]
                neice = grandchildren_2[0]

            elif(len(grandchildren_1) > 1 and get_merge_res_wrapper(children[1], grandchildren_1[1])):
                uncle = children[1]
                dad = children[0]
                nephew = grandchildren_1[1]
                neice = grandchildren_1[0]

            # Trying to assign the merge_threshold for dad and nephew, since these get thrown away.
            if(uncle != None):
                if(merge_res < dad.merge_res):
                    dad.merge_res = merge_res
                    dad.merge_type = "cc"
                if(merge_res < nephew.merge_res):
                    nephew.merge_res = merge_res
                    nephew.merge_type = "cc"


def merge_formulae(formulae):  # Assumes a set(no duplicates)
    final_formulae = set()

    for formula in formulae:
        present = 0
        for formula_family in final_formulae:
            if(are_similar_formulae({formula_family, formula})):
                present = 1
                break
        if(not present):
            final_formulae.add(formula)

    return final_formulae


def merge_nodes(final, merged_in):
    # TODO: Need to add/merge formulae
    final.sub_tests = final.sub_tests + merged_in.sub_tests
    final.formula.update(merged_in.formula)
    final.max_perf = max(final.max_perf, merged_in.max_perf)
    final.min_perf = min(final.min_perf, merged_in.min_perf)
    if(final.merge_res < merged_in.merge_res):
        final.merge_res = merged_in.merge_res
        final.merge_type = merged_in.merge_type


def get_merging_res(perf1, perf2):
    return (abs(perf2-perf1)) + 1


def can_merge_perf(perf1, perf2):
    return abs(perf2-perf1) < perf_resolution


def perf_within_envelope(perf):
    return (perf >= expected_perf-perf_resolution and perf <= expected_perf+perf_resolution)


def pretty_print_constraints(node):
    if(node == None):
        print("Node %s not found" % (constraint_node))
        return
    assert(len(node.constraints.subject) > 0)

    print("Constraints for node %s" % (node.name))
    print("Subject is %s" % (node.constraints.subject))
    for child in list(node.children):
        if(child.is_true):
            print("True branch to Node %s" % (child.name))
        else:
            print("False branch to Node %s" % (child.name))


def compare_node_constraints(node1, node2):
    return (node1.constraints.subject == node2.constraints.subject)


# TODO:Duplicated code from compare_trees. Must refactor

def get_merge_res_wrapper(node1, node2):
    global merge_res
    global merged_tuples
    merge_res = 0
    merged_tuples.clear()
    x = get_merge_res(node1, node2)
    if(not x):  # Is this needed?
        merged_tuples.clear()
    return x


def get_merge_res(node1, node2):
    global merge_res
    global merged_tuples
    merged_tuples.append((node1.name, node2.name))
    if((not compare_node_constraints(node1, node2)) or (len(node1.children) != len(node2.children))):
        return 0  # These trees can never be merged
    elif(node1.is_leaf):
        # Node2 is also a leaf node because of above check
        if(tree_type == "neg-tree"):
            assert(0 and "Not to be called from neg-tree")
        else:
            local_merge_res = get_merging_res(
                max(node1.max_perf, node2.max_perf), min(node1.min_perf, node2.min_perf))
            if(local_merge_res > merge_res):
                merge_res = local_merge_res
            return 1
    else:  # Can have one child because coalescing is done later
        if(len(node1.children) == 1):
            if(not (node1.children[0].is_true == node2.children[0].is_true)):
                return 0
            return get_merge_res(node1.children[0], node2.children[0])
        else:
            # Both have two children
            return(get_merge_res(node1.children[node1.constraints.sind], node2.children[node2.constraints.sind]) and get_merge_res(node1.children[(node1.constraints.sind + 1) % 2], node2.children[(node2.constraints.sind + 1) % 2]))


def compare_trees_wrapper(node1, node2):
    merged_tuples.clear()
    x = compare_trees(node1, node2)
    if(not x):
        merged_tuples.clear()
    return x


def compare_trees(node1, node2):
    merged_tuples.append((node1.name, node2.name))
    # Checks if the sub-trees below node1 and node2 are identical in terms of constraints
    if((not compare_node_constraints(node1, node2)) or (len(node1.children) != len(node2.children))):
        return 0
    elif(node1.is_leaf):
        # Node2 is also a leaf node because of above check
        if(tree_type == "neg-tree"):
            return 1
        else:
            return can_merge_perf(max(node1.max_perf, node2.max_perf), min(
                node1.min_perf, node2.min_perf))
    else:  # Can have one child because coalescing is done later
        if(len(node1.children) == 1):
            if(not (node1.children[0].is_true == node2.children[0].is_true)):
                return 0
            return compare_trees(node1.children[0], node2.children[0])
        else:
            # Both have two children
            return(compare_trees(node1.children[node1.constraints.sind], node2.children[node2.constraints.sind]) and compare_trees(node1.children[(node1.constraints.sind + 1) % 2], node2.children[(node2.constraints.sind + 1) % 2]))


def are_similar_formulae(formula_var):
    # Checks if a set of formulae only differ in terms of the constant
    if(len(formula_var) == 0):
        return False

    pcvs_used = {}
    for formula in formula_var:
        pcvs_used[formula] = list()
        formula_terms = formula.split("+")
        for term in formula_terms:
            term = term.strip()
            if("*" in term):
                pcvs = term.replace("*", "")
            else:
                pcvs = ""
            pcvs_used[formula].append(pcvs)
    expected_pcv_set = set(pcvs_used[list(formula_var)[0]])
    return all(set(pcv_list) == expected_pcv_set for pcv_list in pcvs_used.values())


def assign_tree_constraints(root):
    # Assumes a binary tree as input.
    for node in list(PostOrderIter(root)):
        if((not node.is_leaf) and (not node == root)):
            node.constraints = assign_node_constraints(node)
            node.children[node.constraints.sind].is_true = 1
            node.children[(node.constraints.sind+1) % 2].is_true = 0


def assign_node_constraints(node):
    assert(node != None and "node not present")
    assert(len(node.children) == 2 and "Leaf node provided")
    children = list(node.children)
    assert(len(children[0].sub_tests) and len(children[1].sub_tests))
    test1 = int(children[0].sub_tests[0].replace("test", ""))
    test2 = int(children[1].sub_tests[0].replace("test", ""))
    constraints = prefix_branch_constraints[test1][test2]
    assert(len(constraints) == 2)
    # In the above matrix, the first constraint in the list always points to the test with the smaller numeric value.
    # That is a property of how the constraints are dumped from KLEE
    if(test1 > test2):  # Need to re-order the list.
        constraints = [constraints[1], constraints[0]]
    # Now we can be sure that the constraints are ordered by the children
    if(len(constraints[0]) > len(constraints[1])):
        short_constraint = constraints[1]
        long_constraint = constraints[0]
        short_ind = 1
    else:
        short_constraint = constraints[0]
        long_constraint = constraints[1]
        short_ind = 0

    short_constraint = short_constraint.rstrip()
    long_constraint = long_constraint.replace('\n', '')
    short_constraint = short_constraint.replace('\n', '')
    assert(long_constraint[0:long_constraint.find(
        short_constraint)] == "(Eq false")

    subject = short_constraint
    return Constraint(subject, short_ind)


def assign_tree_tags_and_perf(root):
    for node in list(PostOrderIter(root)):
        if(node.is_leaf):
            node.max_perf = traces_perf[node.name]
            node.min_perf = node.max_perf
            node.formula.add(traces_perf_formula[node.name])
            if(node.name in traces_tags):
                node.tags = traces_tags[node.name]

        else:
            node.max_perf, node.min_perf = get_perf_variability(node)
            assign_node_tags(node)


def assign_node_tags(node):
    # Returns tags for an intermediate node in the tree.
    # A node has a particular tag iff all its descendants have that tag
    children = list(node.children)
    assert(len(children) > 0)
    children_tags = list(child.tags for child in children)
    node.tags = os.path.commonprefix(children_tags)


def get_perf_variability(node):
    # Returns perf variability for an intermediate node in the tree.
    children = list(node.children)
    assert(len(children) <= 2 and len(children) > 0)
    max_perf = max(list(node.max_perf for node in children))
    min_perf = min(list(node.min_perf for node in children))
    return max_perf, min_perf


def node_colour_fn(node):
    if(node.name == "branch003648"):
        colour = "fillcolor = red,fontcolor = white"
    elif(node.is_leaf):
        colour = "fillcolor = blue,fontcolor = white"
    else:
        colour = "fillcolor = white,fontcolor = black"
    return colour+",style = filled"


def node_identifier_fn(node):
    if(node.is_leaf):
        identifier = 'Name: %s\n' % (node.name)
        # tests = str(node.sub_tests)[1:-1]
        # tests = tests.replace(", ", "\n")
        # identifier += '%s' % (tests)

        identifier += '\nPerf = %s' % ((node.max_perf + node.min_perf)/2)
        # identifier += '\nFormula = %s' % (repr(node.formula))

    else:
        if(tree_type == "res-tree"):
            identifier = '%s:%s:%s\n Merge_res = %d' % (
                node.name, node.id, node.depth, node.merge_res)
        else:
            identifier = '%s:%s:%s' % (
                node.name, node.id, node.depth)

    tag = str(node.tags)[1:-1]  # Removes the square brackets around the list
    tag = tag.replace(", ", "\n")
    identifier += "\n%s" % (tag)

    return identifier


def find_lpm_node(root, node_id, lpm, leaf_id):
    if(lpm == 0):  # Small hack, since it doesn't fit in well with the while loop
        return root

    node_name = "test"+f"{node_id:06d}"
    leaf_node_name = "test"+f"{leaf_id:06d}"
    while(lpm >= 0):
        lpm = lpm - 1
        children = list(root.children)
        root.sub_tests.append(leaf_node_name)
        # print("Looking for %s in subtree with root %s" %
        #       (node_name, root.name))
        root = next((x for x in children if node_name in x.sub_tests), None)
        assert(root != None)

    root.sub_tests.append(leaf_node_name)  # Needs to be done one last time

    return root


def del_node(node):
    while(1):
        if(len(node.siblings) > 0):
            node.parent = None
            break
        else:
            parent = node.parent
            node.parent = None
            node = parent


def build_tree(tree_file):
    global tree_root
    global prefix_match_lengths
    global prefix_branch_constraints
    # stupid code to get length of file. Makes everything much easier
    ctr = 0
    with open(tree_file, 'r') as f:
        for line in f:
            ctr = ctr + 1

    num_lines = int(math.sqrt(2*ctr))
    prefix_match_lengths = [
        [-1 for i in range(num_lines)] for i in range(num_lines)]
    prefix_branch_constraints = [
        [list() for i in range(num_lines)] for i in range(num_lines)]
    with open(tree_file, 'r') as f:
        for line in f:
            text = line.rstrip()
            text = text.split('|')
            assert(len(text) == 3)
            text = [int(x) for x in text]
            prefix_match_lengths[text[0]][text[1]] = text[2]
            prefix_match_lengths[text[1]][text[0]] = text[2]

    with open(constraint_file, 'r') as f:
        acc = ""
        for line in f:
            text = line.strip()+"\n"
            if("|" in line):
                if(acc != ""):
                    assert(
                        len(prefix_branch_constraints[index1][index2]) < 2)
                    prefix_branch_constraints[index1][index2].append(acc)
                    prefix_branch_constraints[index2][index1].append(acc)
                    if(index1 >= index2):
                        assert(
                            len(prefix_branch_constraints[index1][index2]) == 2)
                text = text.split("|")
                assert(len(text) == 3)
                index1 = int(text[0])
                index2 = int(text[1])
                acc = text[2]
            else:
                acc = acc+text
        # Last line has still not been assigned!
        assert(acc != "")
        assert(len(prefix_branch_constraints[index1][index2]) < 2)
        prefix_branch_constraints[index1][index2].append(acc)
        prefix_branch_constraints[index2][index1].append(acc)

    id_ctr = 0
    for i in range(num_lines):
        if(i != 0):
            lpm_index, lpm = max(
                enumerate(prefix_match_lengths[i][0:i]), key=operator.itemgetter(1))
        else:
            lpm = 0
            lpm_index = 0

        subtree_root = tree_root
        subtree_root = find_lpm_node(subtree_root, lpm_index, lpm, i)
        rem_len = prefix_match_lengths[i][i] - lpm
        leaf_node_name = "test"+f"{i:06d}"
        # print("inserting node %s" % (leaf_node_name))
        depth = lpm
        while(rem_len >= 0):
            id = len(subtree_root.children)
            assert(id <= 1)
            if(rem_len == 0):
                node_name = leaf_node_name
            else:
                node_name = "branch"+f"{int(id_ctr):06d}"
                id_ctr = id_ctr + 1
            node = TreeNode(node_name, id, depth, parent=subtree_root)
            subtree_root = node
            subtree_root.sub_tests.append(leaf_node_name)
            depth = depth + 1
            rem_len = rem_len - 1

    # Extra hack, to retain simplifying assumptions
    for i in range(num_lines):
        leaf_node_name = "test"+f"{i:06d}"
        if(leaf_node_name not in traces_perf):
            leaf_node = find(
                tree_root, lambda node: node.name == leaf_node_name)
            del_node(leaf_node)

    basic_cleanup_tree(tree_root)


def get_traces_perf():
    global traces_perf
    global traces_perf_formula

    # We assume that these values are
    with open(formula_file, 'r') as f:
        for line in f:
            text = line.rstrip()
            test_id = text[0:
                           find_nth(text, ",", 1)]
            metric = text[(find_nth(text, ",", 1)+1):
                          find_nth(text, ",", 2)]
            perf = str(text[(find_nth(text, ",", 2)+1):])

            if (metric == perf_metric):
                traces_perf_formula[test_id] = perf
                expr = parse_expr(perf)
                traces_perf[test_id] = expr.subs(common_vals)

    # Done separately, because not sure if lines match (They most probably do)

    with open(perf_file, 'r') as f:
        for line in f:
            text = line.rstrip()
            test_id = text[0:
                           find_nth(text, ",", 1)]
            assert(test_id in traces_perf_formula and "Missing perf formula")


def get_traces_tags():
    global traces_tags
    global leaf_tags
    global all_tag_prefixes
    with open(tags_file, 'r') as f:
        lines = f.readlines()  # Small file, so OK.
        # Hack to access next element too
        lines_hack = lines[1:]
        lines_hack.append(None)
        for line, next_line in zip(lines, lines_hack):
            text = line.rstrip()
            test_id = text[0:
                           find_nth(text, ",", 1)]
            tag = text[(find_nth(text, ",", 2)+1):]

            # Assign all tags for current test in a list
            if test_id in traces_tags:
                traces_tags[test_id].append(tag)
            else:
                list_tags = []
                list_tags.append(tag)
                traces_tags[test_id] = list_tags

            # Getting unique tags
            if(next_line != None):
                next_text = next_line.rstrip()
                next_test_id = next_text[0:
                                         find_nth(next_text, ",", 1)]
            else:
                next_test_id = ""

            if(test_id != next_test_id):  # Reached the leaf tag
                if(traces_tags[test_id] not in leaf_tags):
                    leaf_tags.append(traces_tags[test_id])
                    for x in range(1, len(traces_tags[test_id])+1):
                        if(traces_tags[test_id][0:x] not in all_tag_prefixes):
                            all_tag_prefixes.append(traces_tags[test_id][0:x])
    all_tag_prefixes = list(all_tag_prefixes)
    all_tag_prefixes.sort(key=lambda x: len(x))


def find_nth(haystack, needle, n):
    start = haystack.find(needle)
    while start >= 0 and n > 1:
        start = haystack.find(needle, start+len(needle))
        n -= 1
    return start


main()
