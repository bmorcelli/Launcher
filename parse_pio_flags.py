import configparser, glob, os, sys
env = sys.argv[1]
root = os.path.dirname(__file__)
files = [os.path.join(root,'platformio.ini')]
files += glob.glob(os.path.join(root,'boards','*.ini'))
files += glob.glob(os.path.join(root,'boards','*','platformio.ini'))
config = configparser.ConfigParser(allow_no_value=True, delimiters=('='), interpolation=None)
config.read(files)

def expand(section, key):
    if section not in config or key not in config[section]:
        return []
    raw = config[section][key].splitlines()
    lines = []
    for l in raw:
        l = l.split(';',1)[0].strip()
        if l:
            if l.startswith('-D '):
                l = '-D' + l[3:]
            lines.append(l)
    result = []
    for line in lines:
        if line.startswith('${') and line.endswith('}'):
            ref_section, ref_key = line[2:-1].split('.')
            result.extend(expand(ref_section, ref_key))
        else:
            result.append(line)
    return result

flags = expand(f'env:{env}', 'build_flags')
libs = expand(f'env:{env}', 'lib_deps')
print('FLAGS=' + ';'.join(flags))
print('LIBS=' + ';'.join(libs))
