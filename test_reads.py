import re

with open('src/CrossPointSettings.cpp', 'r') as f:
    content = f.read()

load_block = re.search(r'bool CrossPointSettings::loadFromFile\(\).*?return true;\n}', content, re.DOTALL).group(0)

# Extract only the reads in the do-while loop
loop_block = re.search(r'do \{(.*?)\} while \(false\);', load_block, re.DOTALL).group(1)

lines = loop_block.split('\n')
read_idx = 0
for line in lines:
    m = re.search(r'(readAndValidate|serialization::readPod|serialization::readString)\(.*?(inputFile|file),\s*([a-zA-Z0-9_]+)', line)
    if m:
        var_name = m.group(3)
        read_idx += 1
        print(f"Read {read_idx}: {var_name}")
    
    if 'if (++settingsRead >=' in line:
        print(f"  --> Break Check! (settingsRead would become {read_idx})")

