import argparse

import m5
from m5.objects import *

parser = argparse.ArgumentParser(
    description="A simple system with different operation mode of Memory Controller"
)

parser.add_argument(
    "--mem_operation_mode",
    type=str,
    default="normal",
    help="memory controller operation mode: normal, compresso, DyLeCT",
)

options = parser.parse_args()

# 创建系统
system = System()

# 设置时钟和 CPU
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# 使用 AtomicSimpleCPU
system.cpu = AtomicSimpleCPU()

# 内存系统
system.mem_mode = "atomic"  # 设定为 Atomic 模式
system.mem_ranges = [AddrRange("512MiB")]

# 创建简单的内存总线
system.membus = SystemXBar()

# CPU 连接到总线
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# create the interrupt controller for the CPU and connect to the membus
system.cpu.createInterruptController()

# For X86 only we make sure the interrupts care connect to memory.
# Note: these are directly connected to the memory bus and are not cached.
# For other ISA you should remove the following three lines.
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# 创建内存控制器
system.mem_ctrl = MemCtrl(operation_mode=options.mem_operation_mode)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# 运行二进制
thispath = os.path.dirname(os.path.realpath(__file__))
binary = os.path.join(
    thispath,
    "../../../",
    "tests/test-progs/hello/bin/x86/linux/hello",
    # "tests/test-progs/threads/bin/x86/linux/threads",
    # "tests/test-progs/hello/bin/x86/linux/hello",
    # "../../Mibench/mibench/security/sha/sha",
)

system.workload = SEWorkload.init_compatible(binary)

# 创建进程
process = Process()
process.cmd = [binary]
# process.cmd = [binary, "../../Mibench/mibench/security/sha/input_large.asc"]
system.cpu.workload = process
system.cpu.createThreads()

# 创建仿真对象
root = Root(full_system=False, system=system)

# 开始仿真
m5.instantiate()
print("Starting Simulation...")
exit_event = m5.simulate()
print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")
