# Secure Hardware Memory Compression in gem5

This project reproduces prior hardware memory-compression designs in gem5 and introduces a new secure hardware memory-compression mechanism proposed in our paper, **XXX**.

## Memory-Controller Modes

The memory controller supports four operating modes:

| Mode | Description |
| --- | --- |
| `normal` | Original gem5 implementation without memory compression |
| `DyLeCT` | Implementation based on DyLeCT [1] |
| `compresso` | Implementation based on Compresso [2] |
| `secure` | Our proposed secure memory-compression design |

## Build

Build the X86 optimized binary with:

```bash
scons build/X86/gem5.opt -j8
```

## SPEC CPU2017 Evaluation

We use workloads from SPEC CPU2017 to evaluate the performance of the different designs. The related scripts are located in:

```text
scripts/SPEC
```

We use SimPoint to identify representative execution regions.

Before running the scripts, update all gem5, SimPoint, and SPEC directory paths to match your local environment.

### 1. Profile the Workload

Run the workload once using `NonCachingSimpleCPU` to collect Basic Block Vectors (BBVs):

```bash
python3 profile.py
```

### 2. Generate SimPoints

Run the external SimPoint clustering tool on the BBV file:

```bash
python3 build.py
```

This generates two files:

- `simpoints`
- `weights`

### 3. Create Checkpoints

Create a checkpoint before each representative region:

```bash
python3 checkpoint.py [operation mode]
```

### 4. Run Experiments

Restore checkpoints and run the final simulation:

```bash
python3 restore.py [operation mode]
```

## Side-Channel Attacks

We implemented attacks against both DyLeCT and Compresso. The victim programs are located in:

```text
attack
```

The following attack programs target Compresso:

| Program | Description |
| --- | --- |
| `side_channel_compresso_WE.cpp` | Exploits the write-expansion primitive to observe timing differences. |
| `side_channel_compresso_RA.cpp` | Exploits the read-amplification primitive to observe timing differences. |

Refer to `COMPILE.md` for example compilation commands.

We also evaluate an end-to-end attack against a real Nginx implementation. The victim program is located at:

```text
attack/nginx/end-to-end3/victim
```

### Running Attack Simulations

Attack configuration files are available under:

```text
configs/attack
```

For example, run the Compresso read-amplification timing attack with:

```bash
build/X86/gem5.opt configs/attack/timing_compresso.py --mem_operation_mode=compresso > stats/compresso.log
```

## Multi-Core Attack Evaluation

We also evaluate whether the attacks remain effective when other programs execute on separate CPU cores while sharing the same memory controller.

### Two-Core Simulation

One core runs the attack program, while the other runs `stress-ng` to generate cache pressure.

### Three-Core Simulation

One core runs the attack program, while the other two execute memory-intensive SPEC CPU2017 workloads:

- `mcf`
- `omnetpp`

The corresponding configuration files are available under:

```text
configs/attack
```

- `two_cores_timing.py`
- `three_cores_timing.py`

## Reference Results

The following simulation times were collected in our evaluation environment and are provided for reference.

### Write-Amplification Attack

| Secret value | Simulation time |
| --- | ---: |
| `0x1` | 178,000 ticks |
| `0x0` — triggers recompression in Compresso | 267,000 ticks |

### Read-Amplification Attack

| Cache-line condition | Simulation time |
| --- | ---: |
| Crosses a boundary | 4,196,733,000 ticks |
| Does not cross a boundary | 3,981,257,000 ticks |

### Single-Core End-to-End Nginx Attack

The following example leaks the first byte of the password:

| Password prefix | Simulation time |
| --- | ---: |
| `"a"` | 6,005,037,000 ticks |
| `"1"` | 6,050,143,000 ticks |

### Two-Core Nginx Attack with `stress-ng`

| Password prefix | Simulation time |
| --- | ---: |
| `"a"` | 6,446,821,000 ticks |
| `"1"` | 6,513,654,000 ticks |

### Three-Core Nginx Attack with `mcf` and `omnetpp`

| Password prefix | Simulation time |
| --- | ---: |
| `"a"` | 6,749,331,000 ticks |
| `"1"` | 6,788,317,000 ticks |

## References

[1] G. Panwar, M. Laghari, E. Choukse, and X. Jian, “DyLeCT: Achieving Huge-page-like Translation Performance for Hardware-compressed Memory,” *Proceedings of the 51st Annual International Symposium on Computer Architecture (ISCA)*, 2024, pp. 1129–1143. doi: [10.1109/ISCA59077.2024.00085](https://doi.org/10.1109/ISCA59077.2024.00085)

[2] E. Choukse, M. Erez, and A. R. Alameldeen, “Compresso: Pragmatic Main Memory Compression,” *Proceedings of the 51st Annual IEEE/ACM International Symposium on Microarchitecture (MICRO)*, 2018, pp. 546–558. doi: [10.1109/MICRO.2018.00051](https://doi.org/10.1109/MICRO.2018.00051)