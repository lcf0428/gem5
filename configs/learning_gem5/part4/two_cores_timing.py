import argparse
import os
import shlex

import m5
from m5.objects import *


parser = argparse.ArgumentParser(
    description="gem5 SE mode: two-core multiprogram workload config"
)

# ------------------------------------------------------------
# Your custom memory-controller options
# ------------------------------------------------------------
parser.add_argument(
    "--mem_operation_mode",
    type=str,
    default="normal",
    help="memory controller operation mode: normal, compresso, DyLeCT",
)

parser.add_argument(
    "--recency_list_size",
    type=int,
    default=0,
    help="determine the size of recency list, only helpful in DyLeCT mode",
)

parser.add_argument(
    "--tick_interval",
    type=int,
    default=10,
    help="the interval to take mem snapshots",
)

# ------------------------------------------------------------
# Program options
# ------------------------------------------------------------
parser.add_argument(
    "--cmd0",
    type=str,
    required=True,
    help='command running on core 0, e.g. "/path/to/prog0 arg1 arg2"',
)

parser.add_argument(
    "--cmd1",
    type=str,
    required=True,
    help='command running on core 1, e.g. "/path/to/prog1 arg1 arg2"',
)

parser.add_argument(
    "--cwd0",
    type=str,
    default=None,
    help="working directory for program on core 0",
)

parser.add_argument(
    "--cwd1",
    type=str,
    default=None,
    help="working directory for program on core 1",
)

parser.add_argument(
    "--input0",
    type=str,
    default=None,
    help="stdin input file for program on core 0",
)

parser.add_argument(
    "--input1",
    type=str,
    default=None,
    help="stdin input file for program on core 1",
)

# Important: two Process objects must have different pids.
parser.add_argument(
    "--pid0",
    type=int,
    default=100,
    help="simulated pid for program on core 0",
)

parser.add_argument(
    "--pid1",
    type=int,
    default=101,
    help="simulated pid for program on core 1",
)

# ------------------------------------------------------------
# System options
# ------------------------------------------------------------
parser.add_argument(
    "--mem_size",
    type=str,
    default="512MiB",
    help="system memory size, e.g. 512MiB, 1GiB, 2GiB",
)

parser.add_argument(
    "--cpu_clock",
    type=str,
    default="1GHz",
    help="CPU clock, e.g. 1GHz, 2GHz",
)

options = parser.parse_args()


def make_process(cmd_string, pid, cwd=None, stdin_file=None):
    """
    Create one SE-mode process from a shell-like command string.

    cmd_string example:
        "/path/to/stress-ng --cache 1 --timeout 1s"

    shlex.split() supports quoted arguments.
    """
    cmd = shlex.split(cmd_string)

    if len(cmd) == 0:
        raise ValueError("empty command string")

    binary = os.path.abspath(cmd[0])
    if not os.path.exists(binary):
        raise FileNotFoundError(f"binary not found: {binary}")

    process = Process()

    # This is the key fix for:
    #   fatal: _pid 100 is already used
    #
    # In gem5 SE mode, Process() defaults to pid 100.
    # If we create two Process objects and do not set different pids,
    # gem5 will reject the second one during initialization.
    process.pid = pid

    # Treat each workload as a root process in SE mode.
    # This avoids parent-signal corner cases when the simulated program exits.
    process.ppid = 0

    process.executable = binary

    # argv[0] uses absolute binary path.
    # Other arguments are kept as provided.
    process.cmd = [binary] + cmd[1:]

    if cwd is not None:
        process.cwd = os.path.abspath(cwd)

    if stdin_file is not None:
        process.input = os.path.abspath(stdin_file)

    return process


# ------------------------------------------------------------
# Create system
# ------------------------------------------------------------
system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = options.cpu_clock
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange(options.mem_size)]

# ------------------------------------------------------------
# Create two CPU cores
# ------------------------------------------------------------
num_cpus = 2
system.cpu = [DerivO3CPU(cpu_id=i) for i in range(num_cpus)]

# ------------------------------------------------------------
# Create memory bus
# ------------------------------------------------------------
system.membus = SystemXBar()

# Connect each CPU to the memory bus
for cpu in system.cpu:
    cpu.icache_port = system.membus.cpu_side_ports
    cpu.dcache_port = system.membus.cpu_side_ports

    cpu.createInterruptController()

    # X86 only.
    # If you are not using X86, remove the following three assignments.
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# ------------------------------------------------------------
# Memory controller
# ------------------------------------------------------------
system.mem_ctrl = MemCtrl(
    operation_mode=options.mem_operation_mode,
    recency_list_size=options.recency_list_size,
    tick_interval=options.tick_interval,
)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

# ------------------------------------------------------------
# Create processes
# ------------------------------------------------------------
if options.pid0 == options.pid1:
    raise ValueError(f"pid0 and pid1 must be different, got {options.pid0}")

process0 = make_process(
    cmd_string=options.cmd0,
    pid=options.pid0,
    cwd=options.cwd0,
    stdin_file=options.input0,
)

process1 = make_process(
    cmd_string=options.cmd1,
    pid=options.pid1,
    cwd=options.cwd1,
    stdin_file=options.input1,
)

# Initialize SE workload using one compatible binary.
# The two programs should be built for the same ISA/ABI, e.g. both x86 Linux.
system.workload = SEWorkload.init_compatible(process0.executable)

system.cpu[0].workload = process0
system.cpu[1].workload = process1

for cpu in system.cpu:
    cpu.createThreads()

print("Core 0 command:", " ".join(process0.cmd), "pid:", process0.pid)
print("Core 1 command:", " ".join(process1.cmd), "pid:", process1.pid)

# ------------------------------------------------------------
# Run simulation
# ------------------------------------------------------------
root = Root(full_system=False, system=system)

m5.instantiate()
print("Starting Simulation...")

exit_event = m5.simulate()

print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")
