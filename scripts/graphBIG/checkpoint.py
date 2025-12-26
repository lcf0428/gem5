import glob
import optparse
import os
import sys

programs = [
    "BFS",
    "connectedComp",
    "degreeCentr",
    "DFS",
    "pageRank",
    "graphColor",
    "shortestPath",
    "kCore",
    "triangleCount",
]

benchmarks_dir = [
    "benchmark/bench_BFS",
    "benchmark/bench_connectedComp",
    "benchmark/bench_degreeCentr",
    "benchmark/bench_DFS",
    "benchmark/bench_pageRank",
    "benchmark/bench_graphColoring",
    "benchmark/bench_shortestPath",
    "benchmark/bench_kCore",
    "benchmark/bench_triangleCount"
]

binaries = [
    "./bfs",
    "./connectedcomponent",
    "./dc",
    "./dfs",
    "./pagerank",
    "./graphcoloring",
    "./sssp",
    "./kcore",
    "./tc"
]

args = [
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --quad 0.001 --damp 0.85 --maxiter 100",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --kcore 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
]

stdin_file = [
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
]

tick_intervals = [
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
]

recency_list_sizes = [
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
]

small_recency_list_sizes = [
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
]


operation_mode = sys.argv[1]
operation_mode = str(operation_mode)

HOME = "/local/home/liuche"
BASE_DIR = f"{HOME}/graphBIG/graphBIG"  # TODO: change the SPEC directory
GEM5_DIR = f"{HOME}/gem5/gem5"  # TODO: change the gem5 directory
# BUILD_DIR = f"{HOME}/gem5_results/graphBIG/build"
BUILD_DIR = f"{HOME}/gem5_results/graphBIG/build_memory"
RESULTS_DIR = f"{HOME}/gem5_results/graphBIG/checkpoint/{operation_mode}"
SIMULATE_DIR = f"{GEM5_DIR}/scripts/graphBIG"


index_list = [0, 1, 2]

# for i in range(len(programs)):
# for i in range(2, 3):
for i in index_list:
    program = programs[i]

    path = f"{BASE_DIR}/{benchmarks_dir[i]}"
    print("Directoy changed to: " + path)
    os.chdir(path)

    binary = binaries[i]
    arg = args[i]
    tick_interval = str(tick_intervals[i])

    gem5_out = f"{RESULTS_DIR}/{program}/"
    sim_out = f"checkpoint_{operation_mode}_small.out"
    cur_input = stdin_file[i]

    recency_list_size = small_recency_list_sizes[i]

    if cur_input:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-checkpoint-new.py \
                --simpoint_interval 1000000000 --simpoint_file {}/{}/simpoints --weight_file {}/{}/weights.txt --warmup_interval 100000 \
                --checkpoint-path {} --env {}/env.txt --cmd ./{}  --stdin_file {} --mem_operation_mode={} --recency_list_size={} --tick_interval={} \
                --options {} > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, BUILD_DIR, program, BUILD_DIR, program, gem5_out, SIMULATE_DIR, binary,  cur_input, operation_mode, recency_list_size, tick_interval, arg, sim_out)
    else:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-checkpoint-new.py \
                --simpoint_interval 1000000000 --simpoint_file {}/{}/simpoints --weight_file {}/{}/weights.txt --warmup_interval 100000 \
                --checkpoint-path {} --env {}/env.txt --cmd ./{} --mem_operation_mode={} --recency_list_size={} --tick_interval={} \
                --options {} > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, BUILD_DIR, program, BUILD_DIR, program, gem5_out, SIMULATE_DIR, binary, operation_mode, recency_list_size, tick_interval, arg, sim_out)

    print(gem5_cmd)
    os.system(gem5_cmd)
