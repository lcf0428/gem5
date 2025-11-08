import glob
import optparse
import os
import sys

programs = [
    # "perlbench",
    "gcc",
    "mcf",
    "omnetpp",
    "xalancbmk",
    "x264",
    "deepsjeng",
    "leela",
    "exchange2",
    "xz",
    "bwaves",
    "cactuBSSN",
    "lbm",
    "wrf",
    # # "cam4",
    # "pop2",
    "imagick",
    "nab",
    "fotonik3d",
    "roms",
]

benchmarks_dir = [
    # "benchspec/CPU/600.perlbench_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/602.gcc_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/605.mcf_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/620.omnetpp_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/623.xalancbmk_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/625.x264_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/631.deepsjeng_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/641.leela_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/648.exchange2_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/657.xz_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/603.bwaves_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/607.cactuBSSN_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/619.lbm_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/621.wrf_s/run/run_base_refspeed_mytest-m64.0000",
    # # "benchspec/CPU/627.cam4_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/628.pop2_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/638.imagick_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/644.nab_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/649.fotonik3d_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/654.roms_s/run/run_base_refspeed_mytest-m64.0000",
]

binaries = [
    # "./perlbench_s_base.mytest-m64",
    "./sgcc_base.mytest-m64",
    "./mcf_s_base.mytest-m64",
    "./omnetpp_s_base.mytest-m64",
    "./xalancbmk_s_base.mytest-m64",
    "./x264_s_base.mytest-m64",
    "./deepsjeng_s_base.mytest-m64",
    "./leela_s_base.mytest-m64",
    "./exchange2_s_base.mytest-m64",
    "./xz_s_base.mytest-m64",
    "./speed_bwaves_base.mytest-m64",
    "./cactuBSSN_s_base.mytest-m64",
    "./lbm_s_base.mytest-m64",
    "./wrf_s_base.mytest-m64",
    # # "./cam4_s_base.mytest-m64",
    # "./speed_pop2_base.mytest-m64",
    "./imagick_s_base.mytest-m64",
    "./nab_s_base.mytest-m64",
    "./fotonik3d_s_base.mytest-m64",
    "./sroms_base.mytest-m64",
]

args = [
    # "-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1",
    "gcc-pp.c -O5 -fipa-pta -o gcc-pp.opts-O5_-fipa-pta.s",
    "inp.in",
    "-c General -r 0",
    "-v t5.xml xalanc.xsl",
    "--pass 1 --stats x264_stats.log --bitrate 1000 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720",
    "ref.txt",
    "ref.sgf",
    "6",
    "cpu2006docs.tar.xz 6643 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 1036078272 1111795472 4",
    "bwaves_1",
    "spec_ref.par",
    "2000 reference.dat 0 0 200_200_260_ldc.of",
    "",
    # # "",
    # "",
    "-limit disk 0 refspeed_input.tga -resize 817% -rotate -2.76 -shave 540x375 -alpha remove -auto-level -contrast-stretch 1x1% -colorspace Lab -channel R -equalize +channel -colorspace sRGB -define histogram:unique-colors=false -adaptive-blur 0x5 -despeckle -auto-gamma -adaptive-sharpen 55 -enhance -brightness-contrast 10x10 -resize 30% refspeed_output.tga",
    " 3j1n 20140317 220",
    "",
    "",
]

stdin_file = [
    # "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "bwaves_1.in",
    "",
    "",
    "",
    # # "",
    # "",
    "",
    "",
    "",
    "ocean_benchmark3.in",
]

tick_intervals = [
    #  0,
    1000000,
    6,
    1000000,
    2000000,
    2000000,
    800000,
    1200000,
    1800000,
    60000,
    1800000,
    1800000,
    1000000,
    120000,
    # 0,
    # 0,
    410000,
    5000,
    2,
    2400000,
]

recency_list_sizes = [
#     0,
    1256174,
    3324,
    42878,
    51053,
    27590,
    1232714,
    2611,
    225,
    2751166,
    0, # bwaves ?
    892346,
    577706,
    46355.4,
#     0,
#     0,
    302402,
    3469,
    878, # fotonik3d ?
    1853443
]

operation_mode = sys.argv[1]
operation_mode = str(operation_mode)

HOME = "/local/home/liuche"
BASE_DIR = f"{HOME}/SPEC"  # TODO: change the SPEC directory
GEM5_DIR = f"{HOME}/gem5/gem5"  # TODO: change the gem5 directory
BUILD_DIR = f"{HOME}/gem5_results/SPEC/build"
RESULTS_DIR = f"{HOME}/gem5_results/SPEC/restore"
SIMULATE_DIR = f"{GEM5_DIR}/scripts/SPEC"

restore_list = [0, 1, 2, 3, 4, 9, 10, 16]

# for i in range(len(programs)):
# for i in range (2):
for i in restore_list:
    program = programs[i]

    path = f"{BASE_DIR}/{benchmarks_dir[i]}"
    print("Directoy changed to: " + path)
    os.chdir(path)

    binary = binaries[i]
    arg = args[i]
    tick_interval = str(tick_intervals[i])

    gem5_out = f"{RESULTS_DIR}/{program}_{operation_mode}/"
    sim_out = f"restore_{program}_{operation_mode}.out"

    # checkpoint = "{}/gem5_results/SPEC/checkpoint/{}/{}/cpt".format(HOME, operation_mode, program)
    base_path = "{}/gem5_results/SPEC/checkpoint/{}/{}".format(
        HOME, operation_mode, program
    )
    cpt_folders = glob.glob(os.path.join(base_path, "cpt.*"))

    if cpt_folders:
        checkpoint = cpt_folders[0]
        print(f"find the directory {checkpoint}")
    else:
        exit(1)

    cur_input = stdin_file[i]
    recency_list_size = recency_list_sizes[i]

    if cur_input:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-restore-new.py \
                    --cmd ./{} --simpoint_interval 1000000000  --simpoint_file {}/gem5_results/SPEC/build/{}/simpoints --weight_file {}/gem5_results/SPEC/build/{}/weights.txt \
                    --checkpoint {} --warmup_interval 100000 --stdin_file {} --mem_operation_mode={} --recency_list_size={} --tick_interval={} --options {} \
                    > {}/gem5_results/SPEC/restore/m5out/{} 2>&1 &".format(
            GEM5_DIR,
            gem5_out,
            GEM5_DIR,
            binary,
            HOME,
            program,
            HOME,
            program,
            checkpoint,
            cur_input,
            operation_mode,
            recency_list_size,
            tick_interval,
            arg,
            HOME,
            sim_out,
        )
    else:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-restore-new.py \
                    --cmd ./{} --simpoint_interval 1000000000  --simpoint_file {}/gem5_results/SPEC/build/{}/simpoints --weight_file {}/gem5_results/SPEC/build/{}/weights.txt \
                    --checkpoint {} --warmup_interval 100000 --mem_operation_mode={} --recency_list_size={} --tick_interval={} --options {} \
                    > {}/gem5_results/SPEC/restore/m5out/{} 2>&1 &".format(
            GEM5_DIR,
            gem5_out,
            GEM5_DIR,
            binary,
            HOME,
            program,
            HOME,
            program,
            checkpoint,
            operation_mode,
            recency_list_size,
            tick_interval,
            arg,
            HOME,
            sim_out,
        )

    print(gem5_cmd)
    os.system(gem5_cmd)
