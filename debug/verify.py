import sys
import re

def parse_hex_bytes(line):
    """
    从形如 "Functional write marker: 00 10 00 ..." 的行中提取字节列表。
    """
    # 去掉行首尾空白，并分割冒号
    if ':' in line:
        data_part = line.split(':', 1)[1].strip()
    else:
        data_part = line.strip()
    # 按空白分割，转换为整数
    bytes_list = []
    for token in data_part.split():
        try:
            b = int(token, 16)
            if 0 <= b <= 255:
                bytes_list.append(b)
        except ValueError:
            # 忽略非十六进制字符（安全处理）
            pass
    return bytes_list

def main(logfile):
    mem = {}          # 内存模拟： address -> byte
    waiting = None    # 等待数据行时存储操作信息

    with open(logfile, 'r') as f:
        for line in f:
            line = line.rstrip('\n')

            # ---------- 如果正在等待数据行 ----------
            if waiting is not None:
                # 检测数据行类型
                if line.startswith('Functional write marker:'):
                    data = parse_hex_bytes(line)
                    addr = waiting['addr']
                    for i, b in enumerate(data):
                        mem[addr + i] = b
                    print(f"[WRITE] 0x{addr:x} 长度 {len(data)} 字节")
                    waiting = None

                elif line.startswith('Timing write marker:'):
                    data = parse_hex_bytes(line)
                    addr = waiting['addr']
                    # 可选：检查 size 是否匹配
                    if 'size' in waiting and waiting['size'] is not None:
                        if len(data) != waiting['size']:
                            print(f"警告: 写入数据长度 {len(data)} 与声明 size {waiting['size']} 不一致")
                    for i, b in enumerate(data):
                        mem[addr + i] = b
                    print(f"[WRITE] 0x{addr:x} 长度 {len(data)} 字节")
                    waiting = None

                elif line.startswith('Functional read marker:'):
                    data = parse_hex_bytes(line)
                    addr = waiting['addr']
                    # 从内存中读取等长数据
                    expected = []
                    missing = False
                    for i in range(len(data)):
                        val = mem.get(addr + i)
                        if val is None:
                            missing = True
                            val = 0  # 未写入的地址视为 0x00
                        expected.append(val)
                    if missing:
                        print(f"警告: 读取地址 0x{addr:x} 范围内存在从未写入的位置，已视为 0x00")
                    # 比较
                    if expected == data:
                        print(f"[READ]  0x{addr:x} 长度 {len(data)} 字节: 成功")
                    else:
                        print(f"[READ]  0x{addr:x} 长度 {len(data)} 字节: 失败")
                        print(f"  期望: {' '.join(f'{x:02x}' for x in expected)}")
                        print(f"  实际: {' '.join(f'{x:02x}' for x in data)}")
                    waiting = None

                elif line.startswith('Timing read marker:'):
                    data = parse_hex_bytes(line)
                    addr = waiting['addr']
                    if 'size' in waiting and waiting['size'] is not None:
                        if len(data) != waiting['size']:
                            print(f"警告: 读取数据长度 {len(data)} 与声明 size {waiting['size']} 不一致")
                    expected = []
                    missing = False
                    for i in range(len(data)):
                        val = mem.get(addr + i)
                        if val is None:
                            missing = True
                            val = 0
                        expected.append(val)
                    if missing:
                        print(f"警告: 读取地址 0x{addr:x} 范围内存在从未写入的位置，已视为 0x00")
                    if expected == data:
                        print(f"[READ]  0x{addr:x} 长度 {len(data)} 字节: 成功")
                    else:
                        print(f"[READ]  0x{addr:x} 长度 {len(data)} 字节: 失败")
                        print(f"  期望: {' '.join(f'{x:02x}' for x in expected)}")
                        print(f"  实际: {' '.join(f'{x:02x}' for x in data)}")
                    waiting = None

                else:
                    # 意外行，重置等待状态
                    print(f"警告: 期待数据行，但收到: {line}")
                    waiting = None
                continue

            # ---------- 识别操作行 ----------
            # 1. recv Functional: WriteReq 0x...
            if line.startswith('recv Functional: WriteReq 0x'):
                addr_str = line.split('0x', 1)[1].strip()
                try:
                    addr = int(addr_str, 16)
                except ValueError:
                    print(f"无效地址行: {line}")
                    continue
                waiting = {'addr': addr}
                continue

            # 2. marker accept TimingReq: request WriteReq addr 0x... size ...
            if line.startswith('marker accept TimingReq: request WriteReq'):
                match = re.search(r'addr 0x([0-9a-fA-F]+)', line)
                if not match:
                    print(f"无法提取地址: {line}")
                    continue
                addr = int(match.group(1), 16)
                size_match = re.search(r'size (\d+)', line)
                size = int(size_match.group(1)) if size_match else None
                waiting = {'addr': addr, 'size': size}
                continue

            # 3. recv Functional: ReadReq 0x...
            if line.startswith('recv Functional: ReadReq 0x'):
                addr_str = line.split('0x', 1)[1].strip()
                try:
                    addr = int(addr_str, 16)
                except ValueError:
                    print(f"无效地址行: {line}")
                    continue
                waiting = {'addr': addr}
                continue

            # 4. marker accept TimingReq: request ReadReq addr 0x... size ...
            if line.startswith('marker accept TimingReq: request ReadReq'):
                match = re.search(r'addr 0x([0-9a-fA-F]+)', line)
                if not match:
                    print(f"无法提取地址: {line}")
                    continue
                addr = int(match.group(1), 16)
                size_match = re.search(r'size (\d+)', line)
                size = int(size_match.group(1)) if size_match else None
                waiting = {'addr': addr, 'size': size}
                continue

            # 其他行忽略
            # （可在这里添加调试输出，例如 print(f"忽略: {line}")）

    print("验证结束。")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法: python verify.py <日志文件>")
        sys.exit(1)
    main(sys.argv[1])
