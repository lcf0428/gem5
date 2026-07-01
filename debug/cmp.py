#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import argparse

def compare_logs(file1_path, file2_path, max_diff):
    try:
        with open(file1_path, 'r', encoding='utf-8') as f1, \
             open(file2_path, 'r', encoding='utf-8') as f2:

            line_num = 1
            diff_count = 0

            while True:
                line1 = f1.readline()
                line2 = f2.readline()

                if line1 == '' and line2 == '':
                    print("两个文件均读取完毕，无更多行。")
                    break

                if line1 == '' or line2 == '':
                    ended_file = file1_path if line1 == '' else file2_path
                    print(f"文件 {ended_file} 已结束，停止比较。")
                    break

                # 修复点：使用 rstrip() 去除尾部所有空白（包括 \r 和空格）
                if line1.rstrip() != line2.rstrip():
                    diff_count += 1
                    print(f"\n差异 #{diff_count} 位于行 {line_num}:")
                    print(f"  {file1_path}: {line1.rstrip()}")
                    print(f"  {file2_path}: {line2.rstrip()}")

                    if diff_count >= max_diff:
                        print(f"\n已达到最大差异数 {max_diff}，停止比较。")
                        break

                line_num += 1

    except FileNotFoundError as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"发生未知错误: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="逐行比较两个日志文件，输出最多 n 行差异（行号从 1 开始）。"
                    "任一文件结束则停止比较。"
    )
    parser.add_argument("file1", help="第一个日志文件路径")
    parser.add_argument("file2", help="第二个日志文件路径")
    parser.add_argument("-n", "--max-diff", type=int, default=20,
                        help="最大差异行数（默认 10）")
    args = parser.parse_args()
    compare_logs(args.file1, args.file2, args.max_diff)