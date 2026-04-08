import json
import argparse
from collections import defaultdict

class CallNode:
    def __init__(self, name):
        self.name = name
        self.weight = 0
        self.children = defaultdict(lambda: CallNode("tmp"))

    def add_trace(self, stack, weight):
        self.weight += weight
        if not stack:
            return
        
        child_name = stack[0]
        if self.children[child_name].name == "tmp":
             self.children[child_name].name = child_name
             
        self.children[child_name].add_trace(stack[1:], weight)

# Using current_level=-1 so the target_level exactly matches your semicolon count
def find_prune_target(node, target_name, target_level, current_level=-1):
    """Recursively searches for the node to use as the new root."""
    if node.name == target_name and current_level == target_level:
        return node
    
    for child in node.children.values():
        result = find_prune_target(child, target_name, target_level, current_level + 1)
        if result:
            return result
    return None

def build_json_dict(node, total_run_weight):
    """Recursively builds the dictionary with runtime percentages."""
    percentage = (node.weight / total_run_weight) * 100 if total_run_weight > 0 else 0
    
    result = {
        "function": node.name,
        "runtime_percentage": f"{percentage:.2f}%",
    }
    
    if node.children:
        # Sort children by weight (heaviest first)
        sorted_children = sorted(node.children.values(), key=lambda x: x.weight, reverse=True)
        result["children"] = [build_json_dict(c, total_run_weight) for c in sorted_children]
        
    return result

def main():
    # Set up command line argument parsing
    parser = argparse.ArgumentParser(description="Prune and export folded stacks to JSON.")
    parser.add_argument("filename", help="Path to the folded_stacks.txt file")
    parser.add_argument("--func", required=True, help="The exact name of the target function to prune at")
    parser.add_argument("--level", type=int, required=True, help="The depth level of the target function")
    parser.add_argument("--out", default="pruned_trace.json", help="Output JSON filename (default: pruned_trace.json)")
    
    args = parser.parse_args()

    # 1. Read the folded stacks from the file
    try:
        with open(args.filename, 'r') as f:
            folded_lines = f.readlines()
    except FileNotFoundError:
        print(f"Error: Could not find '{args.filename}'. Please ensure the path is correct.")
        return

    # 2. Build the full tree
    root = CallNode("ROOT")
    for line in folded_lines:
        line = line.strip()
        if not line: continue
        
        try:
            stack_str, weight_str = line.rsplit(' ', 1)
            stack = stack_str.split(';')
            weight = float(weight_str)
            root.add_trace(stack, weight)
        except ValueError:
            print(f"Warning: Skipping malformed line: {line}")
            continue

    total_run_time = root.weight

    # 3. Find the target node
    pruned_root = find_prune_target(root, args.func, args.level)

    if not pruned_root:
        print(f"Error: Could not find function '{args.func}' at level {args.level}")
        return

    # 4. Generate the final JSON dictionary
    final_json = build_json_dict(pruned_root, total_run_time)

    # 5. Output to terminal and save to file
    json_output = json.dumps(final_json, indent=2)
    print(json_output)
    
    with open(args.out, "w") as f:
        f.write(json_output)
    print(f"\nSuccess: Exported JSON to '{args.out}'")

if __name__ == "__main__":
    main()

# python export_tree.py /tmp/folded_stacks.txt --func "pybind11::cpp_function::dispatcher" --level 12
