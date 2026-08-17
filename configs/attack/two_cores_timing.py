import os
import shlex

import m5
from m5.objects import *


# ============================================================
# User config area
# ============================================================

# ----------------------------
# Memory controller config
# ----------------------------
MEM_OPERATION_MODE = "compresso"      # normal / compresso / DyLeCT
RECENCY_LIST_SIZE = 0              # only useful for DyLeCT
TICK_INTERVAL = 10                 # interval to take mem snapshots

# ----------------------------
# System config
# ----------------------------
MEM_SIZE = "2048MiB"
CPU_CLOCK = "1GHz"

# ----------------------------
# Process pid config
# ----------------------------
VICTIM_PID = 100
STRESS_PID = 101

# ----------------------------
# Core 0: victim program config
# ----------------------------
THIS_PATH = os.path.dirname(os.path.realpath(__file__))

# 如果你的 config 文件位置就是 configs/learning_gem5/part4/xxx.py，
# 下面这个相对路径会对应你之前写的：
#   thispath + "../../../" + "../../nginx/end-to-end3/victim/victim"
# VICTIM_BINARY = os.path.abspath(os.path.join(
#     THIS_PATH,
#     "../../../",
#     "../../nginx/end-to-end3/victim/victim",
# ))

VICTIM_BINARY = "/local/home/liuche/gem5/gem5/attack/nginx/end-to-end3/victim/victim"  # TODO

VICTIM_USER = "a"

#   passwd = "a" * 535 + "edcb" + "a" * 15
VICTIM_PREFIX_LEN = 535
VICTIM_LEAK_BYTES = "edcb"
VICTIM_SUFFIX_LEN = 15

#   process.cmd = [binary, usr_passwd, "aaaaaaa"]
# VICTIM_SECOND_ARG = "aaaaaaa"
VICTIM_SECOND_ARG = "11aaaaa"

VICTIM_CWD = None

VICTIM_INPUT = None

# ----------------------------
# Core 1: stress-ng config
# ----------------------------
STRESS_BINARY = "/local/home/liuche/stress-ng/stress-ng"

# Original command:
#   /local/home/liuche/stress-ng/stress-ng --cache 1 --timeout 1s
STRESS_ARGS = [
    "--cache", "1",
    "--timeout", "1s",
]

STRESS_CWD = None

STRESS_INPUT = None


# ============================================================
# Helper functions
# ============================================================

def make_process_from_argv(binary, argv, pid, cwd=None, stdin_file=None):
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

    return process


# ============================================================
# Build victim argv
# ============================================================

passwd = (
    "a" * VICTIM_PREFIX_LEN
    + VICTIM_LEAK_BYTES
    + "a" * VICTIM_SUFFIX_LEN
)

usr_passwd = VICTIM_USER + ":" + passwd

victim_argv = [
    usr_passwd,
    VICTIM_SECOND_ARG,
]

stress_argv = STRESS_ARGS


# ============================================================
# Create system
# ============================================================

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = CPU_CLOCK
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange(MEM_SIZE)]


# ============================================================
# Create two CPU cores
# ============================================================

system.cpu = [DerivO3CPU(cpu_id=i) for i in range(2)]


# ============================================================
# Create memory bus
# ============================================================

system.membus = SystemXBar()

for cpu in system.cpu:
    cpu.icache_port = system.membus.cpu_side_ports
    cpu.dcache_port = system.membus.cpu_side_ports

    cpu.createInterruptController()

    # X86 only.
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports


# ============================================================
# Memory controller
# ============================================================

system.mem_ctrl = MemCtrl(
    operation_mode=MEM_OPERATION_MODE,
    recency_list_size=RECENCY_LIST_SIZE,
    tick_interval=TICK_INTERVAL,
)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports


# ============================================================
# Create processes
# ============================================================

if VICTIM_PID == STRESS_PID:
    raise ValueError(f"VICTIM_PID and STRESS_PID must be different, got {VICTIM_PID}")

victim_process = make_process_from_argv(
    binary=VICTIM_BINARY,
    argv=victim_argv,
    pid=VICTIM_PID,
    cwd=VICTIM_CWD,
    stdin_file=VICTIM_INPUT,
)

stress_process = make_process_from_argv(
    binary=STRESS_BINARY,
    argv=stress_argv,
    pid=STRESS_PID,
    cwd=STRESS_CWD,
    stdin_file=STRESS_INPUT,
)

system.workload = SEWorkload.init_compatible(victim_process.executable)

# Core assignment:
#   core 0 -> victim
#   core 1 -> stress-ng
system.cpu[0].workload = victim_process
system.cpu[1].workload = stress_process

for cpu in system.cpu:
    cpu.createThreads()


# ============================================================
# Print config summary
# ============================================================

print("========== gem5 two-core SE config ==========")

print("[Core 0] victim")
print("  binary:", victim_process.executable)
print("  pid:", victim_process.pid)
print("  argv[1] length:", len(usr_passwd))
print("  argv[1] head:", usr_passwd[:32])
print("  argv[1] tail:", usr_passwd[-32:])
print("  argv[2]:", VICTIM_SECOND_ARG)

print("[Core 1] stress-ng")
print("  binary:", stress_process.executable)
print("  pid:", stress_process.pid)
print("  args:", " ".join(STRESS_ARGS))

print("[System]")
print("  cpu_clock:", CPU_CLOCK)
print("  mem_size:", MEM_SIZE)
print("  mem_operation_mode:", MEM_OPERATION_MODE)
print("  recency_list_size:", RECENCY_LIST_SIZE)
print("  tick_interval:", TICK_INTERVAL)


# ============================================================
# Run simulation
# ============================================================

root = Root(full_system=False, system=system)

m5.instantiate()
print("Starting Simulation...")

exit_event = m5.simulate()

print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")
