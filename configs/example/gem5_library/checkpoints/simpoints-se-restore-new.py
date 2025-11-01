# Copyright (c) 2022 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
This configuration script shows an example of how to restore a checkpoint that
was taken for SimPoints in the
configs/example/gem5_library/checkpoints/simpoints-se-checkpoint.py.
The SimPoints, SimPoints interval length, and the warmup instruction length
are passed into the SimPoint module, so the SimPoint object will store and
calculate the warmup instruction length for each SimPoints based on the
available instructions before reaching the start of the SimPoint. With the
Simulator module, exit event will be generated to stop when the warmup session
ends and the SimPoints interval ends.

This script builds a more complex board than the board used for taking
checkpoint.

Usage
-----

```
scons build/X86/gem5.opt
./build/X86/gem5.opt \
    configs/example/gem5_library/checkpoints/simpoints-se-checkpoint.py

./build/X86/gem5.opt \
    configs/example/gem5_library/checkpoints/simpoints-se-restore.py
```

"""

import argparse
from pathlib import Path

from m5.stats import (
    dump,
    reset,
)

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.memory import SingleChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    SimpointResource,
    obtain_resource,
    CustomResource,
)
from gem5.resources.workload import Workload
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

requires(isa_required=ISA.X86)

parser = argparse.ArgumentParser(
    description="OoO core with DRAM energy stats and full cache hierarchy"
)

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

parser.add_argument("--checkpoint", type=str, required=True, help="the target checkpoint file that wish to be restored")

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

simpoint_list = read_simpoints(args.simpoint_file)
weight_list = read_weights(args.weight_file)
env_list = {}

if args.env:
    with open(args.env) as f:
            env_list = [line.rstrip() for line in f]

print("args.env is: ", args.env)

print("simpoint is: ", simpoint_list)

print("weights is: ", weight_list)

print("the option is: ", args.options)

assert len(simpoint_list) == len(weight_list), "Simpoints and weights must have the same length."
assert abs(sum(weight_list) - 1.0) < 1e-3, "Weights must sum to 1.0."

# The cache hierarchy can be different from the cache hierarchy used in taking
# the checkpoints
cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
    l1d_size="32KiB",
    l1i_size="32KiB",
    l2_size="256KiB",
)

# The memory structure can be different from the memory stqructure used in
# taking the checkpoints, but the size of the memory must be maintained
memory = SingleChannelDDR4_2400(size="20GiB")
for ctrl in memory.mem_ctrl:
    ctrl.dram.enable_dram_powerdown = True
    ctrl.operation_mode = args.mem_operation_mode
    ctrl.tick_interval = args.tick_interval

processor = SimpleProcessor(
    # cpu_type=CPUTypes.TIMING,
    cpu_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=1,
)

board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Here we obtain the workload from gem5 resources, the checkpoint in this
# workload was generated from
# `configs/example/gem5_library/checkpoints/simpoints-se-checkpoint.py`.
# board.set_workload(
#    Workload("x86-print-this-15000-with-simpoints-and-checkpoint")
#
# **Note: This has been removed until we update the resources.json file to
# encapsulate the new Simpoint format.
# Below we set the simpount manually.
#
# This loads a single checkpoint as an example of using simpoints to simulate
# the function of a single simpoint region.

# board.set_se_simpoint_workload(
#     binary=CustomResource("xz_s_base.mytest-m64"),
#     arguments=["/local/home/liuche/SPEC/benchspec/CPU/502.gcc_r/data/test/input/t1.c", "-O3", "-finline-limit=0", "-fif-conversion", "-fif-conversion2"],
#     simpoint=SimpointResource(
#         simpoint_interval=1000000,
#         simpoint_list=[6, 0],
#         weight_list=[1, 0],
#         warmup_interval=1000000,
#     ),
#     checkpoint=CustomResource(
#         "/local/home/liuche/se/test/gem5/m5out/gcc_r/test/checkpoint/cpt.3540281508"
#     ),
# )

board.set_se_simpoint_workload(
    binary=CustomResource(args.cmd),
    arguments=args.options,
    simpoint=SimpointResource(
        simpoint_interval=args.simpoint_interval,
        simpoint_list=list(simpoint_list),
        weight_list=list(weight_list),
        warmup_interval=args.warmup_interval,
    ),
    stdin_file=CustomResource(args.stdin_file) if args.stdin_file else None,
    env_list=env_list if env_list else None,
    checkpoint=CustomResource(args.checkpoint),
)


def max_inst():
    warmed_up = False
    while True:
        if warmed_up:
            print("end of SimPoint interval")
            yield True
        else:
            print("end of warmup, starting to simulate SimPoint")
            warmed_up = True
            # Schedule a MAX_INSTS exit event during the simulation
            simulator.schedule_max_insts(
                board.get_simpoint().get_simpoint_interval()
            )
            dump()
            reset()
            yield False


simulator = Simulator(
    board=board,
    on_exit_event={ExitEvent.MAX_INSTS: max_inst()},
)

# Schedule a MAX_INSTS exit event before the simulation begins the
# schedule_max_insts function only schedule event when the instruction length
# is greater than 0.
# In here, it schedules an exit event for the first SimPoint's warmup
# instructions
simulator.schedule_max_insts(board.get_simpoint().get_warmup_list()[0])
simulator.run()
