import os

import m5
from m5.objects import *


# ============================================================
# User configuration
# ============================================================

# Memory controller configuration.
MEM_OPERATION_MODE = "compresso"  # normal / compresso / DyLeCT
RECENCY_LIST_SIZE = 0              # Used only by DyLeCT.
TICK_INTERVAL = 2_000_000          # Memory snapshot interval.

# System configuration.
MEM_SIZE = "2048MiB"
CPU_CLOCK = "1GHz"

# Each SE-mode process must have a unique PID.
VICTIM_PID = 100
MCF_PID = 101
OMNETPP_PID = 102


# ----------------------------
# Core 0: victim program
# ----------------------------
VICTIM_BINARY = "/local/home/liuche/gem5/gem5/attack/nginx/end-to-end3/victim/victim"
# VICTIM_BINARY = "/local/home/liuche/nginx/end-to-end3/victim/victim"
VICTIM_USER = "a"
PASSWD = "a" * 535 + "edcb" + "a" * 15
VICTIM_SECOND_ARG = "aaaaaaa"
# VICTIM_SECOND_ARG = "111aaaa"
VICTIM_CWD = None
VICTIM_INPUT = None


# ----------------------------
# Core 1: SPEC CPU 2017 605.mcf_s
# ----------------------------
MCF_RUN_DIR = (
    "/local/home/liuche/SPEC/benchspec/CPU/605.mcf_s/"
    "run/run_base_refspeed_mytest-m64.0000"
)
# Exact --cmd and --options paths from the mcf checkpoint command.
MCF_BINARY = (
    "/local/home/liuche/SPEC/benchspec/CPU/605.mcf_s/"
    "run/run_base_refspeed_mytest-m64.0000/mcf_s_base.mytest-m64"
)
MCF_ARGS = [
    "/local/home/liuche/SPEC/benchspec/CPU/605.mcf_s/"
    "run/run_base_refspeed_mytest-m64.0000/inp.in"
]
MCF_CWD = MCF_RUN_DIR
MCF_INPUT = None
MCF_ENV = "/local/home/liuche/gem5/gem5/scripts/SPEC/env.txt"

# Reference information for the one-core SimPoint checkpoint generation.
# These fields are retained for reproducibility; this three-core script runs
# the executable directly and does not generate or restore SimPoint checkpoints.
MCF_SIMPOINT_INTERVAL = 1_000_000_000
MCF_SIMPOINT_FILE = "/local/home/liuche/new_gem5_results/SPEC/build300/mcf/simpoints"
MCF_WEIGHT_FILE = "/local/home/liuche/new_gem5_results/SPEC/build300/mcf/weights.txt"
MCF_CHECKPOINT_PATH = "/local/home/liuche/new_gem5_results/SPEC/checkpoint/compresso/mcf/"
MCF_WARMUP_INTERVAL = 0


# ----------------------------
# Core 2: SPEC CPU 2017 620.omnetpp_s
# ----------------------------
OMNETPP_RUN_DIR = (
    "/local/home/liuche/SPEC/benchspec/CPU/620.omnetpp_s/"
    "run/run_base_refspeed_mytest-m64.0000"
)
# Exact --cmd path from the omnetpp checkpoint command.
OMNETPP_BINARY = (
    "/local/home/liuche/SPEC/benchspec/CPU/620.omnetpp_s/"
    "run/run_base_refspeed_mytest-m64.0000/omnetpp_s_base.mytest-m64"
)
OMNETPP_ARGS = ["-c", "General", "-r", "0"]
OMNETPP_CWD = OMNETPP_RUN_DIR
OMNETPP_INPUT = None
OMNETPP_ENV = "/local/home/liuche/gem5/gem5/scripts/SPEC/env.txt"

# Reference information for the one-core SimPoint checkpoint generation.
OMNETPP_SIMPOINT_INTERVAL = 1_000_000_000
OMNETPP_SIMPOINT_FILE = "/local/home/liuche/new_gem5_results/SPEC/build300/omnetpp/simpoints"
OMNETPP_WEIGHT_FILE = "/local/home/liuche/new_gem5_results/SPEC/build300/omnetpp/weights.txt"
OMNETPP_CHECKPOINT_PATH = "/local/home/liuche/new_gem5_results/SPEC/checkpoint/compresso/omnetpp/"
OMNETPP_WARMUP_INTERVAL = 0


def make_process_from_argv(
    binary, argv, pid, cwd=None, stdin_file=None, env_file=None
):
    """Create one SE-mode process from an executable and its arguments."""
    binary = os.path.abspath(binary)
    if not os.path.exists(binary):
        raise FileNotFoundError(f"binary not found: {binary}")

    process = Process()
    process.pid = pid
    process.ppid = 0
    process.executable = binary
    process.cmd = [binary] + argv

    if cwd is not None:
        process.cwd = os.path.abspath(cwd)
    if stdin_file is not None:
        process.input = os.path.abspath(stdin_file)
    if env_file is not None:
        env_file = os.path.abspath(env_file)
        if not os.path.exists(env_file):
            raise FileNotFoundError(f"environment file not found: {env_file}")
        with open(env_file, encoding="utf-8") as file:
            process.env = [line.rstrip() for line in file if line.strip()]

    return process


# Build command line for the victim program.
victim_argv = [VICTIM_USER + ":" + PASSWD, VICTIM_SECOND_ARG]

if len({VICTIM_PID, MCF_PID, OMNETPP_PID}) != 3:
    raise ValueError("VICTIM_PID, MCF_PID, and OMNETPP_PID must be different")

victim_process = make_process_from_argv(
    VICTIM_BINARY, victim_argv, VICTIM_PID, VICTIM_CWD, VICTIM_INPUT
)
mcf_process = make_process_from_argv(
    MCF_BINARY, MCF_ARGS, MCF_PID, MCF_CWD, MCF_INPUT, MCF_ENV
)
omnetpp_process = make_process_from_argv(
    OMNETPP_BINARY,
    OMNETPP_ARGS,
    OMNETPP_PID,
    OMNETPP_CWD,
    OMNETPP_INPUT,
    OMNETPP_ENV,
)


# ============================================================
# Create the three-core timing-mode system
# ============================================================
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = CPU_CLOCK
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "timing"
system.mem_ranges = [AddrRange(MEM_SIZE)]

system.cpu = [DerivO3CPU(cpu_id=i) for i in range(3)]
system.membus = SystemXBar()

for cpu in system.cpu:
    cpu.icache_port = system.membus.cpu_side_ports
    cpu.dcache_port = system.membus.cpu_side_ports
    cpu.createInterruptController()

    # X86 only. Remove these three lines for other ISAs.
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

system.mem_ctrl = MemCtrl(
    operation_mode=MEM_OPERATION_MODE,
    recency_list_size=RECENCY_LIST_SIZE,
    tick_interval=TICK_INTERVAL,
)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

# All three binaries must use the same ISA/ABI.
system.workload = SEWorkload.init_compatible(victim_process.executable)

# One process per core.
system.cpu[0].workload = victim_process
system.cpu[1].workload = mcf_process
system.cpu[2].workload = omnetpp_process

for cpu in system.cpu:
    cpu.createThreads()


print("========== gem5 three-core SE config ==========")
for core_id, name, process in [
    (0, "victim", victim_process),
    (1, "SPEC 605.mcf_s", mcf_process),
    (2, "SPEC 620.omnetpp_s", omnetpp_process),
]:
    print(f"[Core {core_id}] {name}")
    print("  binary:", process.executable)
    print("  pid:", process.pid)
    print("  command:", " ".join(process.cmd))

print("[System]")
print("  cpu_clock:", CPU_CLOCK)
print("  mem_size:", MEM_SIZE)
print("  mem_operation_mode:", MEM_OPERATION_MODE)
print("  recency_list_size:", RECENCY_LIST_SIZE)
print("  tick_interval:", TICK_INTERVAL)


root = Root(full_system=False, system=system)
m5.instantiate()
print("Starting Simulation...")

exit_event = m5.simulate()
print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")

