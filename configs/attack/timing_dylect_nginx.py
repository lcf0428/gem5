import argparse

import m5
from m5.objects import *


def read_hex_as_string(filename: str) -> str:
    characters: list[str] = []

    with open(filename, "r", encoding="utf-8") as file:
        for line_number, line in enumerate(file, start=1):
            text = line.strip()

            if not text:
                continue

            try:
                value = int(text, 16)
            except ValueError as exc:
                raise ValueError(
                    f'Line {line_number}: "{text}" is not valid hexadecimal'
                ) from exc

            if not 0 <= value <= 0xFF:
                raise ValueError(
                    f'Line {line_number}: "{text}" is outside the uint8 range'
                )

            characters.append(chr(value))

    return "".join(characters)

parser = argparse.ArgumentParser(
    description="A simple system with different operation mode of Memory Controller"
)

parser.add_argument(
    "--mem_operation_mode",
    type=str,
    default="DyLeCT",
    help="memory controller operation mode: normal, compresso, DyLeCT",
)

parser.add_argument(
    "--recency_list_size",
    type=int,
    default=100,
    help="determine the size of recency list, only helpful in DyLeCT mode",
)

parser.add_argument(
    "--tick_interval",
    type=int,
    default=10,
    help="the interval to take mem snapshots",
)

options = parser.parse_args()

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses
system.mem_ranges = [AddrRange("8MiB")]  # Create an address range

system.cpu = DerivO3CPU()

system.membus = SystemXBar()

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

system.mem_ctrl = MemCtrl(operation_mode=options.mem_operation_mode, recency_list_size=options.recency_list_size, tick_interval=options.tick_interval)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

thispath = os.path.dirname(os.path.realpath(__file__))
binary = os.path.join(
    thispath,
    "../../",
    "attack/nginx_dylect/end-to-end3/victim/victim",
)

# REAL_PASSWD = "aaaaaaa"
REAL_PASSWD = "AAaaaaa"

# leak the first byte
passwd = read_hex_as_string("/root/gem5/gem5/configs/attack/sequence.txt")

usr_passwd = "a:" + passwd

system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary, usr_passwd, REAL_PASSWD]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)

# start simulation
m5.instantiate()
print("Starting Simulation...")
exit_event = m5.simulate()
print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")
