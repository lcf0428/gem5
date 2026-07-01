#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import os
import argparse
from pathlib import Path

def extract_interesting_lines(file_path, ignore_recv=False, ignore_func_marker=False):
    """
    从日志文件中提取关注行的序列。
    返回 (内容列表, 原始行号列表)，两者长度相同，一一对应。
    参数：
      ignore_recv        : 是否忽略以 "recv Functional" 开头的行（规则1）
      ignore_func_marker : 是否忽略以 "Functional write marker" 或 "Functional read marker" 开头的行（规则2）
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    content_list = []
    line_num_list = []
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i].rstrip('\n')
        line_num = i + 1

        # 规则1：recv Functional
        if line.startswith("recv Functional"):
            if not ignore_recv:
                content_list.append(line)
                line_num_list.append(line_num)
            i += 1
            continue

        # 规则2：Functional write/read marker
        if (line.startswith("Functional write marker") or 
            line.startswith("Functional read marker")):
            if not ignore_func_marker:
                content_list.append(line)
                line_num_list.append(line_num)
            i += 1
            continue

        # 规则3：marker accept TimingReq
        if line.startswith("marker accept TimingReq"):
            content_list.append(line)               # 该行本身加入
            line_num_list.append(line_num)
            if i + 1 < n:
                xxx = lines[i + 1].rstrip('\n')     # 下一行作为 marker 值
                # 从 i+2 开始查找第一个 marker:XXX
                for j in range(i + 2, n):
                    if lines[j].startswith("marker:" + xxx):
                        # 将 marker 行的下一行加入关注序列
                        if j + 1 < n:
                            content_list.append(lines[j + 1].rstrip('\n'))
                            line_num_list.append(j + 2)   # 原始行号
                        break
            i += 1
            continue

        # 其他行跳过
        i += 1

    return content_list, line_num_list


def write_cleaned_log(original_path, output_dir, cleaned_lines):
    """将关注行（仅内容）写入输出目录，文件名加 _cleaned.log"""
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    stem = Path(original_path).stem
    out_file = out_dir / f"{stem}_cleaned.log"
    with open(out_file, 'w', encoding='utf-8') as f:
        for line in cleaned_lines:
            f.write(line + '\n')
    print(f"清洗日志已写入：{out_file}")


def compare_logs(log1_path, log2_path, n=1, cleaned_dir=None,
                 ignore_recv=False, ignore_func_marker=False):
    """
    比较两个日志文件的关注行序列，输出前 n 个差异。
    差异信息包含：清洗后行号、原始文件行号、行内容。
    若指定 cleaned_dir，则将两个清洗后文件（仅内容）写入该目录。
    """
    seq1_content, seq1_nums = extract_interesting_lines(log1_path, ignore_recv, ignore_func_marker)
    seq2_content, seq2_nums = extract_interesting_lines(log2_path, ignore_recv, ignore_func_marker)

    # 若需要输出清洗文件
    if cleaned_dir:
        write_cleaned_log(log1_path, cleaned_dir, seq1_content)
        write_cleaned_log(log2_path, cleaned_dir, seq2_content)

    diff_count = 0
    max_len = max(len(seq1_content), len(seq2_content))

    for idx in range(max_len):
        if diff_count >= n:
            break

        clean_line_no = idx + 1   # 清洗后行号
        if idx >= len(seq1_content):
            print(f"差异位置 (清洗后行号 {clean_line_no}):")
            print(f"  左侧: (文件结束)")
            print(f"  右侧: 原始行 {seq2_nums[idx]} -> {seq2_content[idx]}")
            diff_count += 1
        elif idx >= len(seq2_content):
            print(f"差异位置 (清洗后行号 {clean_line_no}):")
            print(f"  左侧: 原始行 {seq1_nums[idx]} -> {seq1_content[idx]}")
            print(f"  右侧: (文件结束)")
            diff_count += 1
        else:
            if seq1_content[idx] != seq2_content[idx]:
                print(f"差异位置 (清洗后行号 {clean_line_no}):")
                print(f"  左侧: 原始行 {seq1_nums[idx]} -> {seq1_content[idx]}")
                print(f"  右侧: 原始行 {seq2_nums[idx]} -> {seq2_content[idx]}")
                diff_count += 1

    if diff_count == 0:
        print("两个日志的关注行序列完全一致。")
    else:
        print(f"共发现 {diff_count} 处差异（显示前 {n} 个）。")


def main():
    parser = argparse.ArgumentParser(
        description="比较两个日志文件中关注行的差异，并可输出清洗后的日志。"
    )
    parser.add_argument("log1", help="第一个日志文件路径")
    parser.add_argument("log2", help="第二个日志文件路径")
    parser.add_argument("--n", type=int, default=1,
                        help="显示前 n 个差异（默认 1）")
    parser.add_argument("--cleaned-dir", "-c", default=None,
                        help="输出清洗日志的目录（可选），文件名自动添加 _cleaned")
    parser.add_argument("--ignore-recv", action="store_true",
                        help="忽略以 'recv Functional' 开头的行（规则1）")
    parser.add_argument("--ignore-marker", action="store_true",
                        help="忽略以 'Functional write/read marker' 开头的行（规则2）")

    args = parser.parse_args()

    for f in [args.log1, args.log2]:
        if not os.path.isfile(f):
            print(f"错误：文件不存在 - {f}")
            sys.exit(1)

    compare_logs(
        args.log1,
        args.log2,
        n=args.n,
        cleaned_dir=args.cleaned_dir,
        ignore_recv=args.ignore_recv,
        ignore_func_marker=args.ignore_marker
    )


if __name__ == "__main__":
    main()