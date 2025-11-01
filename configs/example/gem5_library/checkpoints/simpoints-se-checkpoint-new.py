import argparse
from pathlib import Path

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.memory import SingleChannelDDR4_2400
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    SimpointResource,
    CustomResource,
)
from gem5.resources.workload import Workload
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.exit_event_generators import save_checkpoint_generator
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

requires(isa_required=ISA.X86)

parser = argparse.ArgumentParser(
    description="SimPoint-based checkpoint generation from simpoint and weight files."
)

parser.add_argument("--checkpoint-path", type=str, default="se_checkpoint_folder/",
                    help="Directory to store the checkpoints.")
parser.add_argument("--cmd", type=str, required=True, help="Path to binary.")
parser.add_argument("--options", nargs=argparse.REMAINDER, help="Arguments to binary.")
parser.add_argument("--simpoint_interval", type=int, required=True, help="Simpoint interval.")
parser.add_argument("--simpoint_file", type=str, required=True,
                    help="Path to simpoints file (format: 'entry_point checkpoint_id').")
parser.add_argument("--weight_file", type=str, required=True,
                    help="Path to weights file (format: 'weight, checkpoint_id').")
parser.add_argument("--warmup_interval", type=int, required=True, help="Warmup instruction count.")
parser.add_argument("--stdin_file", type=str, help="File to feed to the program's standard input")
parser.add_argument("--env", type=str, help="env file with enviormant variables")
parser.add_argument("--mem_operation_mode", type=str, default="normal",
                    help="Memory controller mode: normal, compresso, DyLeCT, new")
parser.add_argument("--tick_interval", type=int, default=10,
                    help="the interval between memory snapshots (x 10^7)")
args = parser.parse_args()

# === read simpoints file：format [(a1, b1), (a2, b2), ...]， a_i is entry point ===
def read_simpoints(filepath):
    simpoints = []
    with open(filepath, "r") as f:
        for line in f:
            if line.strip():
                entry = float(line.strip().split()[0])
                simpoints.append(int(entry))
    return simpoints

# === read weight.txt format [(c1, d1), (c2, d2), ...]，c_i is weight ===
def read_weights(filepath):
    weights = []
    with open(filepath, "r") as f:
        for line in f:
            if line.strip():
                weight = float(line.strip().split()[0])
                weights.append(weight)
    return weights

def save_and_exit_generator(dir, num_checkpoints):
    count = 0
    for event in save_checkpoint_generator(dir):
        count += 1
        if count >= num_checkpoints:
            print(f"All {num_checkpoints} checkpoints saved. Exiting simulation...")
            yield True
        else:
            yield event

simpoints_list = read_simpoints(args.simpoint_file)
weights = read_weights(args.weight_file)
env_list = {}

if args.env:
    with open(args.env) as f:
            env_list = [line.rstrip() for line in f]

print("args.env is: ", args.env)

print("simpoint is: ", simpoints_list)

print("weights is: ", weights)

print("the option is: ", args.options)

print("the tick interval is ", args.tick_interval)

assert len(simpoints_list) == len(weights), "Simpoints and weights must have the same length."
assert abs(sum(weights) - 1.0) < 1e-3, "Weights must sum to 1.0."

cache_hierarchy = NoCache()
memory = SingleChannelDDR4_2400(size="20GiB")
for ctrl in memory.mem_ctrl:
    ctrl.dram.enable_dram_powerdown = True
    ctrl.operation_mode = args.mem_operation_mode
    ctrl.tick_interval = args.tick_interval

processor = SimpleProcessor(
    cpu_type=CPUTypes.ATOMIC,
    isa=ISA.X86,
    # SimPoints only works with one core
    num_cores=1,
)

board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)


# === set workload ===
board.set_se_simpoint_workload(
    binary=CustomResource(args.cmd),
    arguments=args.options,
    simpoint=SimpointResource(
        simpoint_interval=args.simpoint_interval,
        simpoint_list=list(simpoints_list),
        weight_list=list(weights),
        warmup_interval=args.warmup_interval,
    ),
    stdin_file=CustomResource(args.stdin_file) if args.stdin_file else None,
    env_list=env_list if env_list else None,
)

# === start simulator ===
dir = Path(args.checkpoint_path)

simulator = Simulator(
    board=board,
    on_exit_event={
        # ExitEvent.SIMPOINT_BEGIN: save_checkpoint_generator(dir)
        ExitEvent.SIMPOINT_BEGIN: save_and_exit_generator(dir, len(simpoints_list))
    },
)

simulator.run()