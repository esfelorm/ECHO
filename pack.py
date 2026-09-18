import sys
import os

def pack(input_file, output_header):
    with open(input_file, 'rb') as f:
        data = f.read()

    name = os.path.splitext(os.path.basename(input_file))[0]
    array_name = name.replace('.', '_').replace('-', '_')

    with open(output_header, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <vector>\n')
        f.write('#include <windows.h>\n\n')
        f.write('static const unsigned char %s_data[] = {\n' % array_name)
        for i, b in enumerate(data):
            if i % 16 == 0:
                f.write('    ')
            f.write('0x%02X, ' % b)
            if i % 16 == 15:
                f.write('\n')
        if len(data) % 16 != 0:
            f.write('\n')
        f.write('};\n\n')
        f.write('static const size_t %s_size = %d;\n\n' % (array_name, len(data)))
        f.write('static std::vector<BYTE> GetDLLData() {\n')
        f.write('    return std::vector<BYTE>(%s_data, %s_data + %s_size);\n' % (array_name, array_name, array_name))
        f.write('}\n')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: pack.py <input> <output_header>')
        sys.exit(1)
    pack(sys.argv[1], sys.argv[2])